// SPDX-License-Identifier: GPL-3.0-or-later
// GSL3680 register sequence and data format from components/gsl3680.
#include "touch.h"

#include "slide_geometry.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gsl3680_firmware.h"
#include "gsl_point_id.h"

#include <algorithm>
#include <array>

namespace espframe::native {
namespace {

constexpr gpio_num_t reset_pin = GPIO_NUM_22;
constexpr int transfer_timeout_ms = 12;
constexpr TickType_t touch_period = pdMS_TO_TICKS(5);
constexpr int touch_core = 1;
constexpr UBaseType_t touch_priority = 5;

class Touch {
 public:
  void init();
  esp_err_t read(TouchReport &report) const;

 private:
  esp_err_t write_(uint8_t address, uint32_t value, bool one_byte = false) const;
  esp_err_t read_(uint8_t address, uint8_t *data, size_t size) const;

  i2c_master_dev_handle_t handle_{};
};

esp_err_t Touch::write_(uint8_t address, uint32_t value, bool one_byte) const {
  const uint8_t data[]{
      address, static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
      static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24),
  };
  return i2c_master_transmit(this->handle_, data, one_byte ? 2 : sizeof(data), transfer_timeout_ms);
}

esp_err_t Touch::read_(uint8_t address, uint8_t *data, size_t size) const {
  return i2c_master_transmit_receive(this->handle_, &address, 1, data, size, transfer_timeout_ms);
}

void Touch::init() {
  i2c_master_bus_config_t bus_config{};
  bus_config.i2c_port = I2C_NUM_0;
  bus_config.sda_io_num = GPIO_NUM_7;
  bus_config.scl_io_num = GPIO_NUM_8;
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  i2c_master_bus_handle_t bus{};
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus));

  i2c_device_config_t device{};
  device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device.device_address = 0x40;
  device.scl_speed_hz = 400'000;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &device, &this->handle_));

  ESP_ERROR_CHECK(gpio_reset_pin(reset_pin));
  ESP_ERROR_CHECK(gpio_set_direction(reset_pin, GPIO_MODE_OUTPUT));
  ESP_ERROR_CHECK(gpio_set_level(reset_pin, 0));
  vTaskDelay(pdMS_TO_TICKS(5));
  ESP_ERROR_CHECK(gpio_set_level(reset_pin, 1));
  vTaskDelay(pdMS_TO_TICKS(60));

  uint8_t marker[4]{};
  ESP_ERROR_CHECK(this->read_(0xf0, marker, sizeof(marker)));
  ESP_ERROR_CHECK(this->write_(0xf0, 0x00563412));
  vTaskDelay(pdMS_TO_TICKS(20));
  ESP_ERROR_CHECK(this->read_(0xf0, marker, sizeof(marker)));
  ESP_ERROR_CHECK(marker[0] == 0x12 ? ESP_OK : ESP_ERR_INVALID_RESPONSE);

  ESP_ERROR_CHECK(this->write_(0xe0, 0x88, true));
  vTaskDelay(pdMS_TO_TICKS(20));
  ESP_ERROR_CHECK(this->write_(0x88, 0x01, true));
  vTaskDelay(pdMS_TO_TICKS(20));
  ESP_ERROR_CHECK(this->write_(0xe4, 0x04, true));
  vTaskDelay(pdMS_TO_TICKS(20));
  ESP_ERROR_CHECK(this->write_(0xe0, 0x00, true));
  vTaskDelay(pdMS_TO_TICKS(20));

  ESP_ERROR_CHECK(gpio_set_level(reset_pin, 0));
  vTaskDelay(pdMS_TO_TICKS(20));
  ESP_ERROR_CHECK(gpio_set_level(reset_pin, 1));
  vTaskDelay(pdMS_TO_TICKS(20));
  ESP_ERROR_CHECK(this->write_(0xe0, 0x88, true));
  vTaskDelay(pdMS_TO_TICKS(10));
  ESP_ERROR_CHECK(this->write_(0xe4, 0x04, true));
  vTaskDelay(pdMS_TO_TICKS(10));
  ESP_ERROR_CHECK(this->write_(0xbc, 0x00));
  vTaskDelay(pdMS_TO_TICKS(10));

  for (const auto &reg : esphome::gsl3680::GSLX680_FW)
    ESP_ERROR_CHECK(this->write_(reg.offset, reg.val, reg.offset == 0xf0));
  ESP_ERROR_CHECK(this->write_(0xe0, 0x00, true));
  vTaskDelay(pdMS_TO_TICKS(40));
  ESP_ERROR_CHECK(this->read_(0xb0, marker, sizeof(marker)));
  ESP_ERROR_CHECK(std::all_of(std::begin(marker), std::end(marker), [](uint8_t value) {
    return value == 0x5a;
  }) ? ESP_OK : ESP_ERR_INVALID_RESPONSE);
  gsl_DataInit(esphome::gsl3680::gsl_config_data_id);
}

esp_err_t Touch::read(TouchReport &sample) const {
  std::array<uint8_t, 24> report{};
  const esp_err_t status = this->read_(0x80, report.data(), report.size());
  if (status != ESP_OK)
    return status;

  gsl_touch_info info{};
  info.finger_num = static_cast<int>(uint32_t{report[0]} | (uint32_t{report[1]} << 8) |
                                    (uint32_t{report[2]} << 16) | (uint32_t{report[3]} << 24));
  for (int point = 0; point < 2; ++point) {
    const int offset = 4 + point * 4;
    info.x[point] = ((report[offset + 3] & 0x0f) << 8) | report[offset + 2];
    info.y[point] = (report[offset + 1] << 8) | report[offset];
    info.id[point] = report[offset + 3] >> 4;
  }
  gsl_alg_id_main(&info);
  const auto mask = gsl_mask_tiaoping();
  if (mask != 0 && mask != 0xffffffffU) {
    esp_err_t mask_status = this->write_(0xf0, 0x0a);
    if (mask_status == ESP_OK)
      mask_status = this->write_(0x08, mask);
    if (mask_status != ESP_OK)
      return mask_status;
  }

  sample.contacts = static_cast<uint8_t>(std::clamp(info.finger_num, 0, 2));
  if (sample.contacts != 0) {
    sample.x = info.x[0];
    sample.y = info.y[0];
  }
  return ESP_OK;
}

}  // namespace

void TouchReader::start() {
  this->reports_ = xQueueCreate(8, sizeof(TouchReport));
  ESP_ERROR_CHECK(this->reports_ != nullptr ? ESP_OK : ESP_ERR_NO_MEM);
  const auto result = xTaskCreatePinnedToCore(run_, "touch-reader", 3072, this, touch_priority, nullptr, touch_core);
  ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

bool TouchReader::poll(TouchReport &report, ScreenRotation rotation) const {
  if (xQueueReceive(this->reports_, &report, 0) != pdTRUE)
    return false;
  if (report.valid && report.contacts != 0)
    map_gsl_touch(report, report.x, report.y, rotation);
  return true;
}

void TouchReader::run_(void *context) {
  auto &reader = *static_cast<TouchReader *>(context);
  Touch touch;
  touch.init();
  uint32_t sequence = 0;
  while (true) {
    auto started = xTaskGetTickCount();
    TouchReport report;
    report.valid = touch.read(report) == ESP_OK;
    report.sequence = ++sequence;
    report.sampled_us = esp_timer_get_time();
    if (xQueueSend(reader.reports_, &report, 0) != pdPASS) {
      // Keep input current after a long stall. The sequence gap makes the
      // consumer cancel any gesture whose release could have been dropped.
      xQueueReset(reader.reports_);
      xQueueSend(reader.reports_, &report, 0);
    }
    xTaskDelayUntil(&started, touch_period);
  }
}

}  // namespace espframe::native
