#include "firmware_update.h"

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>

#ifndef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "Build with the native OTA partition table and rollback configuration"
#endif

namespace espframe::native {
namespace {

constexpr const char *tag = "firmware-update";
constexpr size_t header_bytes = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
constexpr size_t prelude_bytes = header_bytes + sizeof(esp_app_desc_t);
constexpr size_t buffer_size = 4096;
constexpr int64_t upload_time_us = 90'000'000;
constexpr int64_t trial_time_us = 15'000'000;
static_assert(prelude_bytes <= buffer_size);

std::atomic<bool> checking_boot{};
esp_timer_handle_t boot_timer{};

bool matches_board(const uint8_t *bytes) {
  esp_image_header_t image{};
  esp_app_desc_t app{};
  std::memcpy(&image, bytes, sizeof(image));
  std::memcpy(&app, bytes + header_bytes, sizeof(app));
  constexpr std::string_view expected = "espframe_native_" NATIVE_BOARD;
  return image.magic == ESP_IMAGE_HEADER_MAGIC && image.chip_id == ESP_CHIP_ID_ESP32P4 && image.hash_appended == 1 &&
         app.magic_word == ESP_APP_DESC_MAGIC_WORD &&
         std::string_view(app.project_name, strnlen(app.project_name, sizeof(app.project_name))) == expected;
}

bool read_exact(FirmwareRead read, void *context, uint8_t *buffer, size_t length, int64_t deadline) {
  size_t offset = 0;
  while (offset < length && esp_timer_get_time() < deadline) {
    const int bytes = read(context, buffer + offset, length - offset);
    if (bytes <= 0 || static_cast<size_t>(bytes) > length - offset)
      return false;
    offset += bytes;
  }
  return offset == length;
}

class OtaWrite {
 public:
  ~OtaWrite() {
    if (active_)
      esp_ota_abort(handle_);
  }

  bool begin(const esp_partition_t *partition) {
    active_ = esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &handle_) == ESP_OK;
    return active_;
  }

  bool write(const uint8_t *data, size_t size) const { return esp_ota_write(handle_, data, size) == ESP_OK; }

  bool verify() {
    // esp_ota_end releases the handle whether validation succeeds or fails.
    active_ = false;
    return esp_ota_end(handle_) == ESP_OK;
  }

 private:
  esp_ota_handle_t handle_{};
  bool active_{};
};

bool boot_trial(const esp_partition_t *running) {
  esp_ota_img_states_t state;
  return running != nullptr && esp_ota_get_state_partition(running, &state) == ESP_OK &&
         state == ESP_OTA_IMG_PENDING_VERIFY;
}

void reject_boot() {
  ESP_LOGW(tag, "Firmware trial failed the display and touch startup check");
  const auto result = esp_ota_mark_app_invalid_rollback_and_reboot();
  ESP_LOGE(tag, "Rollback request returned %s", esp_err_to_name(result));
  esp_restart();
}

void trial_timeout(void *) {
  if (checking_boot.exchange(false))
    reject_boot();
}

}  // namespace

bool can_update_firmware() { return esp_ota_get_next_update_partition(nullptr) != nullptr; }

FirmwareUpdateResult stage_firmware(size_t length, FirmwareRead read, void *context) {
  using Result = FirmwareUpdateResult;
  const auto *running = esp_ota_get_running_partition();
  const auto *target = esp_ota_get_next_update_partition(nullptr);
  const auto *boot = esp_ota_get_boot_partition();
  if (running == nullptr || target == nullptr || boot == nullptr || target->address == running->address)
    return Result::Unavailable;
  if (running->address != boot->address || boot_trial(running))
    return Result::RestartPending;
  if (length < prelude_bytes || length > target->size)
    return Result::Size;

  const std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[buffer_size]);
  if (!buffer)
    return Result::WriteFailed;
  const int64_t deadline = esp_timer_get_time() + upload_time_us;
  if (!read_exact(read, context, buffer.get(), prelude_bytes, deadline))
    return Result::Interrupted;
  if (!matches_board(buffer.get()))
    return Result::WrongBoard;

  OtaWrite image;
  if (!image.begin(target) || !image.write(buffer.get(), prelude_bytes))
    return Result::WriteFailed;
  for (size_t remaining = length - prelude_bytes; remaining > 0;) {
    const size_t count = std::min(remaining, buffer_size);
    if (!read_exact(read, context, buffer.get(), count, deadline))
      return Result::Interrupted;
    if (!image.write(buffer.get(), count))
      return Result::WriteFailed;
    remaining -= count;
  }
  if (!image.verify())
    return Result::Invalid;

  // The existing boot partition is selected until the entire image passes
  // validation. Interrupted uploads only touch the inactive OTA slot.
  return esp_ota_set_boot_partition(target) == ESP_OK ? Result::Staged : Result::WriteFailed;
}

void start_firmware_boot_check() {
  if (!boot_trial(esp_ota_get_running_partition()))
    return;
  checking_boot = true;
  esp_timer_create_args_t config{};
  config.callback = trial_timeout;
  config.name = "ota-trial";
  if (esp_timer_create(&config, &boot_timer) != ESP_OK || esp_timer_start_once(boot_timer, trial_time_us) != ESP_OK)
    reject_boot();
}

void confirm_firmware_boot() {
  if (!checking_boot.load() || !checking_boot.exchange(false))
    return;
  esp_timer_stop(boot_timer);
  esp_timer_delete(boot_timer);
  if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
    reject_boot();
    return;
  }
  ESP_LOGI(tag, "Firmware trial passed display and touch startup checks");
}

}  // namespace espframe::native
