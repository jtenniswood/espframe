#include "usb_setup.h"

#include "improv_serial.h"
#include "native_network.h"
#include "settings.h"
#include "stock_settings.h"

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_log.h"
#include "esp_netif_ip_addr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <cstdio>
#include <string_view>

namespace espframe::native {
namespace {

constexpr auto uart = UART_NUM_0;
constexpr std::string_view prefix = "ESPFRAME_SETUP 1 ";
constexpr size_t challenge_size = 36;

bool uuid(std::string_view id) {
  if (id.size() != challenge_size)
    return false;
  for (size_t i = 0; i < id.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (id[i] != '-')
        return false;
    } else if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) {
      return false;
    }
  }
  return true;
}

void reply(std::string_view status, std::string_view challenge, std::string_view detail = {}) {
  std::string line = "\r\n" + std::string(prefix) + std::string(status) + " " + std::string(challenge);
  if (!detail.empty()) {
    line += ' ';
    line += detail;
  }
  line += "\r\n";
  uart_write_bytes(uart, line.data(), line.size());
}

void report_address(const NativeNetwork &network, std::string_view challenge) {
  const auto address = network.address();
  if (!address.ipv4) {
    reply("OFFLINE", challenge);
    return;
  }
  const esp_ip4_addr_t ip{address.ipv4};
  char detail[24]{};
  std::snprintf(detail, sizeof(detail), IPSTR " %u", IP2STR(&ip), static_cast<unsigned>(address.web));
  reply("CONNECTED", challenge, detail);
}

void listen(void *context) {
  auto &network = *static_cast<NativeNetwork *>(context);
  const auto reset_epoch = [] {
    Settings settings;
    settings.read();
    return settings.reset_epoch;
  }();
  ImprovSerial improv(network);
  static std::array<char, Settings::max_json_size + 96> line{};
  std::array<char, 128> chunk;
  std::string challenge;
  int64_t expires_us = 0;
  size_t length = 0;
  bool overflow = false;
  while (true) {
    const int read = uart_read_bytes(uart, chunk.data(), chunk.size(), pdMS_TO_TICKS(100));
    const auto now = esp_timer_get_time();
    improv.poll(now);
    for (int index = 0; index < read; ++index) {
      const char byte = chunk[index];
      if (improv.consume(static_cast<uint8_t>(byte), length == 0, now))
        continue;
      if (byte != '\n') {
        if (length == line.size() - 1)
          overflow = true;
        else
          line[length++] = byte;
        continue;
      }

      if (length && line[length - 1] == '\r')
        --length;
      line[length] = '\0';
      std::string_view command(line.data(), overflow ? 0 : length);
      overflow = false;
      length = 0;
      if (!command.starts_with(prefix) || command.find('\0') != std::string_view::npos)
        continue;
      command.remove_prefix(prefix.size());
      if (command.starts_with("STATUS ") && uuid(command.substr(7))) {
        report_address(network, command.substr(7));
        continue;
      }
      if (command.starts_with("HELLO ") && uuid(command.substr(6))) {
        challenge = command.substr(6);
        expires_us = esp_timer_get_time() + 15'000'000;
        reply("READY", challenge);
        continue;
      }
      const bool migrate = command.starts_with("MIGRATE ");
      const bool import_connections = command.starts_with("IMPORT ");
      if ((!migrate && !import_connections && !command.starts_with("SET ")) ||
          challenge.empty() || esp_timer_get_time() >= expires_us)
        continue;
      command.remove_prefix(migrate ? 8 : import_connections ? 7 : 4);
      if (!command.starts_with(challenge) || command.size() <= challenge.size() || command[challenge.size()] != ' ')
        continue;
      command.remove_prefix(challenge.size() + 1);
      StockSettingsRequest request{.pairing = migrate};
      if (import_connections) {
        // One digit encodes Wi-Fi (1), Immich (2) and Home Assistant (4).
        if (command.size() < 3 || command[0] < '1' || command[0] > '7' || command[1] != ' ') {
          reply("REJECTED", challenge);
          challenge.clear();
          continue;
        }
        const auto flags = command[0] - '0';
        request = {.wifi = (flags & 1) != 0, .immich = (flags & 2) != 0, .pairing = (flags & 4) != 0};
        command.remove_prefix(2);
      }
      using Result = Settings::UsbSaveResult;
      const auto result = improv.busy() || network.busy()
                              ? Result::Rejected
                              : Settings::save_usb(command.data(), reset_epoch, request, read_stock_settings);
      if (result != Result::Saved) {
        switch (result) {
          case Result::WifiUnavailable: reply("WIFI_UNAVAILABLE", challenge); break;
          case Result::ImmichUnavailable: reply("IMMICH_UNAVAILABLE", challenge); break;
          case Result::PairingUnavailable: reply("PAIRING_UNAVAILABLE", challenge); break;
          default: reply("REJECTED", challenge); break;
        }
        challenge.clear();
        continue;
      }
      reply("SAVED", challenge);
      uart_wait_tx_done(uart, pdMS_TO_TICKS(250));
      vTaskDelay(pdMS_TO_TICKS(150));
      esp_restart();
    }
  }
}

}  // namespace

void start_usb_setup(NativeNetwork &network) {
  if (uart_driver_install(uart, 2048, 0, 0, nullptr, 0) != ESP_OK) {
    ESP_LOGW("usb-setup", "Could not start setup on the USB console");
    return;
  }
  uart_vfs_dev_use_driver(uart);
  if (xTaskCreatePinnedToCore(listen, "usb-setup", 6144, &network, 1, nullptr, 0) != pdPASS)
    ESP_LOGW("usb-setup", "Could not allocate the USB setup task");
}

}  // namespace espframe::native
