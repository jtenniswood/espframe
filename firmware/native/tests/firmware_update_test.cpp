#include "firmware_update.h"

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

using espframe::native::FirmwareUpdateResult;
using espframe::native::FirmwareImage;
using espframe::native::firmware_boot_status;
using espframe::native::last_reset_reason;
using espframe::native::request_frame_restart;
using espframe::native::run_coprocessor_update;
using espframe::native::stage_firmware;

namespace {

constexpr size_t partition_size = 4 * 1024 * 1024;
constexpr size_t prelude_size = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
const esp_partition_t factory{0x10000, partition_size};
const esp_partition_t slot{0x420000, partition_size};
const esp_partition_t alternate_slot{0x820000, partition_size};
std::vector<uint8_t> expected;

struct Fake {
  const esp_partition_t *running{&factory};
  const esp_partition_t *boot{&factory};
  const esp_partition_t *next{&slot};
  const esp_partition_t *invalid{};
  std::vector<uint8_t> written;
  unsigned begins{};
  unsigned ends{};
  unsigned aborts{};
  unsigned boot_selections{};
  unsigned accepted{};
  unsigned rejected{};
  unsigned restarts{};
  bool pending{};
  bool verified{};
  bool fail_write{};
  bool timer_armed{};
  esp_reset_reason_t last_reset{ESP_RST_POWERON};
  int64_t current_time{};
  esp_ota_img_states_t invalid_state{ESP_OTA_IMG_INVALID};
  esp_timer_create_args_t timer{};
} fake;

struct Stream {
  const std::vector<uint8_t> &contents;
  size_t available;
  size_t offset{};
  bool slow{};
  bool request_restart{};
};

int receive(void *context, uint8_t *output, size_t length) {
  auto &stream = *static_cast<Stream *>(context);
  if (stream.request_restart && fake.begins != 0) {
    stream.request_restart = false;
    std::thread restart(request_frame_restart);
    restart.join();
    assert(fake.restarts == 0);
  }
  const size_t count = std::min({length, stream.available - stream.offset, size_t{257}});
  std::memcpy(output, stream.contents.data() + stream.offset, count);
  stream.offset += count;
  if (stream.slow)
    fake.current_time += 4'000'000;
  return static_cast<int>(count);
}

}  // namespace

esp_err_t esp_ota_begin(const esp_partition_t *target, size_t mode, esp_ota_handle_t *handle) {
  assert(target == &slot && mode == OTA_WITH_SEQUENTIAL_WRITES);
  ++fake.begins;
  fake.written.clear();
  *handle = 1;
  return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size) {
  assert(handle == 1);
  if (fake.fail_write)
    return -1;
  const auto *bytes = static_cast<const uint8_t *>(data);
  fake.written.insert(fake.written.end(), bytes, bytes + size);
  return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t handle) {
  assert(handle == 1);
  ++fake.ends;
  return fake.written == expected ? ESP_OK : -1;
}
esp_err_t esp_ota_abort(esp_ota_handle_t handle) {
  assert(handle == 1);
  ++fake.aborts;
  return ESP_OK;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *target) {
  assert(target == &slot && fake.ends == 1 && fake.written == expected);
  fake.boot = target;
  ++fake.boot_selections;
  return ESP_OK;
}
esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition, esp_ota_img_states_t *state) {
  if (partition == &factory)
    return -1;
  *state = partition == fake.invalid ? fake.invalid_state
      : fake.pending ? ESP_OTA_IMG_PENDING_VERIFY : fake.verified ? ESP_OTA_IMG_VALID : 0;
  return ESP_OK;
}
esp_err_t esp_ota_mark_app_valid_cancel_rollback() {
  ++fake.accepted;
  fake.pending = false;
  fake.verified = true;
  return ESP_OK;
}
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot() {
  ++fake.rejected;
  fake.pending = false;
  fake.verified = false;
  fake.invalid = &slot;
  fake.boot = &factory;
  return ESP_OK;
}
const esp_partition_t *esp_ota_get_running_partition() { return fake.running; }
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *) { return fake.next; }
const esp_partition_t *esp_ota_get_boot_partition() { return fake.boot; }

esp_err_t esp_ota_get_partition_description(const esp_partition_t *partition, esp_app_desc_t *app) {
  assert(partition == &factory || partition == &slot || partition == &alternate_slot);
  std::memcpy(app, expected.data() + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(*app));
  return ESP_OK;
}
esp_reset_reason_t esp_reset_reason() { return fake.last_reset; }
void esp_restart() {
  ++fake.restarts;
  fake.last_reset = ESP_RST_SW;
}

int64_t esp_timer_get_time() { return fake.current_time; }
esp_err_t esp_timer_create(const esp_timer_create_args_t *config, esp_timer_handle_t *timer) {
  fake.timer = *config;
  *timer = &fake.timer;
  return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t duration) {
#if NATIVE_LEGACY_MIGRATION
  constexpr uint64_t timeout = 90'000'000;
#else
  constexpr uint64_t timeout = 15'000'000;
#endif
  assert(timer == &fake.timer && duration == timeout);
  fake.timer_armed = true;
  return ESP_OK;
}
esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
  assert(timer == &fake.timer);
  fake.timer_armed = false;
  return ESP_OK;
}
esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
  assert(timer == &fake.timer && !fake.timer_armed);
  return ESP_OK;
}

int main(int argc, char **argv) {
#if SWIPE_NATIVE_OTA_TEST
  assert(espframe::native::is_firmware_test_build());
#else
  assert(!espframe::native::is_firmware_test_build());
#endif
  const bool reject_only = argc == 3 && std::string_view(argv[1]) == "--reject";
  assert(argc == 2 || reject_only);
  std::ifstream image(argv[reject_only ? 2 : 1], std::ios::binary);
  expected.assign(std::istreambuf_iterator<char>(image), {});
  assert(expected.size() > prelude_size);

  using Result = FirmwareUpdateResult;
  Stream stream{expected, expected.size()};
  if (reject_only) {
    assert(stage_firmware(expected.size(), receive, &stream) == Result::WrongBoard);
    assert(fake.begins == 0 && fake.boot_selections == 0 && fake.boot == &factory);
    return 0;
  }
  stream.request_restart = true;
  FirmwareImage staged;
  assert(stage_firmware(expected.size(), receive, &stream, &staged) == Result::Staged);
  const auto before_boot = firmware_boot_status();
  // Reinstalling the same build still requires the target slot to start.
  assert(staged.offset == slot.address && before_boot.running != staged && !before_boot.boot_verified);
  assert(before_boot.running && before_boot.running->elf_sha256 == staged.elf_sha256);
  assert(!stream.request_restart);
  assert(fake.running == &factory && fake.boot == &slot);
  assert(fake.begins == 1 && fake.ends == 1 && fake.aborts == 0 && fake.boot_selections == 1);
  assert(stage_firmware(expected.size(), receive, &stream) == Result::RestartPending);
  unsigned coprocessor_installs = 0;
  const auto install_coprocessor = [](void *context) {
    ++*static_cast<unsigned *>(context);
    return true;
  };
  assert(!run_coprocessor_update(install_coprocessor, &coprocessor_installs));
  assert(coprocessor_installs == 0);
  request_frame_restart();
  assert(fake.restarts == 0);
  fake.running = &slot;
  fake.pending = true;
  const auto pending = firmware_boot_status();
  assert(pending.running == staged && !pending.boot_verified && !pending.rejected);
  request_frame_restart();
  assert(fake.restarts == 0);
  fake.pending = false;
  assert(last_reset_reason() == "power-on event");
  assert(!run_coprocessor_update([](void *) { return false; }, nullptr));
  assert(fake.restarts == 0);
  assert(run_coprocessor_update(install_coprocessor, &coprocessor_installs));
  assert(coprocessor_installs == 1 && fake.restarts == 1);
  request_frame_restart();
  assert(fake.restarts == 2 && last_reset_reason() == "software via esp_restart");
  fake.last_reset = ESP_RST_TASK_WDT;
  assert(last_reset_reason() == "task watchdog");
  fake.last_reset = ESP_RST_UNKNOWN;
  assert(last_reset_reason() == "unknown source");

  fake = {};
  assert(stage_firmware(partition_size + 1, receive, &stream) == Result::Size);
  fake.next = &factory;
  assert(stage_firmware(expected.size(), receive, &stream) == Result::Unavailable);
  assert(fake.begins == 0);

  auto wrong_board = expected;
  wrong_board[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) +
              offsetof(esp_app_desc_t, project_name)] = 'x';
  fake = {};
  Stream wrong{wrong_board, wrong_board.size()};
  assert(stage_firmware(wrong.available, receive, &wrong) == Result::WrongBoard);
  assert(fake.begins == 0 && fake.boot == &factory);

  fake = {};
  Stream partial{expected, prelude_size + 9000};
  assert(stage_firmware(expected.size(), receive, &partial) == Result::Interrupted);
  assert(fake.aborts == 1 && fake.ends == 0 && fake.boot_selections == 0);
  Stream retry{expected, expected.size()};
  assert(stage_firmware(expected.size(), receive, &retry) == Result::Staged);
  assert(fake.begins == 2 && fake.ends == 1 && fake.aborts == 1 && fake.boot == &slot);

  fake = {};
  Stream slow{expected, expected.size(), 0, true};
  assert(stage_firmware(expected.size(), receive, &slow) == Result::Interrupted);
  assert(fake.aborts == 1 && fake.ends == 0 && fake.boot == &factory);

  fake = {};
  auto corrupt = expected;
  corrupt.back() ^= 1;
  Stream damaged{corrupt, corrupt.size()};
  assert(stage_firmware(damaged.available, receive, &damaged) == Result::Invalid);
  assert(fake.ends == 1 && fake.aborts == 0 && fake.boot_selections == 0);

  fake = {};
  fake.fail_write = true;
  Stream good{expected, expected.size()};
  assert(stage_firmware(expected.size(), receive, &good) == Result::WriteFailed);
  assert(fake.aborts == 1 && fake.boot == &factory);

  fake = {};
  espframe::native::start_firmware_boot_check();
  assert(!fake.timer_armed);
  fake.running = fake.boot = &slot;
  fake.pending = true;
  espframe::native::start_firmware_boot_check();
  assert(fake.timer_armed);
  espframe::native::confirm_firmware_boot();
  espframe::native::confirm_firmware_boot();
#if SWIPE_NATIVE_OTA_TEST_FAIL
  assert(fake.accepted == 0 && fake.timer_armed && !firmware_boot_status().boot_verified);
  fake.timer.callback(nullptr);
  assert(fake.rejected == 1 && fake.restarts == 1 && fake.boot == &factory && fake.accepted == 0);
  return 0;
#endif
  assert(fake.accepted == 1 && !fake.timer_armed && fake.rejected == 0);
  assert(firmware_boot_status().boot_verified && firmware_boot_status().running == staged);

  fake.pending = true;
  espframe::native::start_firmware_boot_check();
  assert(fake.timer_armed);
  fake.timer.callback(nullptr);
  espframe::native::confirm_firmware_boot();
  assert(fake.rejected == 1 && fake.restarts == 1 && fake.boot == &factory && fake.accepted == 1);
  fake.running = &factory;
  assert(firmware_boot_status().rejected == staged && firmware_boot_status().running == before_boot.running);
  fake.invalid_state = ESP_OTA_IMG_ABORTED;
  assert(firmware_boot_status().rejected == staged);
  fake.running = &alternate_slot;
  fake.verified = true;
  const auto returned_to_ota = firmware_boot_status();
  assert(returned_to_ota.running && returned_to_ota.running->offset == alternate_slot.address &&
         returned_to_ota.boot_verified && returned_to_ota.rejected == staged);
  fake.invalid_state = ESP_OTA_IMG_VALID;
  assert(!firmware_boot_status().rejected);

  fake.pending = true;
  espframe::native::start_firmware_boot_check();
  espframe::native::reject_pending_firmware_boot();
  espframe::native::reject_pending_firmware_boot();
  fake.timer.callback(nullptr);
  espframe::native::confirm_firmware_boot();
  assert(fake.rejected == 2 && fake.restarts == 2 && fake.accepted == 1);
}
