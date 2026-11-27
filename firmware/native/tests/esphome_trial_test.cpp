#include "esphome_trial.h"
#include "esphome_v1_layout.h"

#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"

#include <array>
#include <cassert>
#include <cstring>

using espframe::native::start_esphome_trial;
using espframe_native_ota::esphome_migration_readiness;
using espframe_native_ota::esphome_v1_trial_readiness;
using espframe_native_ota::Readiness;

namespace {

constexpr std::array<esp_partition_t, 2> apps{{{0x10000, 0x7c0000}, {0x7d0000, 0x7c0000}}};
constexpr std::array<esp_partition_t, 3> data{{{0x9000, 0x2000}, {0xb000, 0x1000}, {0xf90000, 0x70000}}};
const esp_partition_t native_nvs{0x9000, 0x4000};

struct Fixture {
  unsigned running{};
  bool legacy_layout{true};
  bool wrong_target{};
  const char *running_project{"immich-frame-10inch"};
  const char *last_project{"immich-frame-10inch"};
  int running_state{ESP_OTA_IMG_PENDING_VERIFY};
  int previous_state{ESP_OTA_IMG_VALID};
  bool rollback_available{true};
  bool timer_fails{};
  bool rollback_fails{};
  unsigned rollback_calls{};
  unsigned restarts{};
  const esp_partition_t *selected{};
  uint64_t duration{};
  esp_timer_create_args_t timer{};
} fixture;

}  // namespace

const esp_partition_t *esp_ota_get_running_partition() { return &apps.at(fixture.running); }
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *) {
  return fixture.wrong_target ? nullptr : &apps.at(1 - fixture.running);
}
const esp_partition_t *esp_ota_get_boot_partition() {
  return fixture.selected ? fixture.selected : esp_ota_get_running_partition();
}

const esp_partition_t *esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype,
                                                const char *label) {
  assert(!label);
  if (type == ESP_PARTITION_TYPE_APP)
    return subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY ? nullptr : &apps.at(subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0);
  assert(type == ESP_PARTITION_TYPE_DATA);
  if (subtype == ESP_PARTITION_SUBTYPE_DATA_NVS && !fixture.legacy_layout)
    return &native_nvs;
  return &data.at(subtype);
}

esp_err_t esp_ota_get_partition_description(const esp_partition_t *partition, esp_app_desc_t *app) {
  std::strcpy(app->project_name, partition == esp_ota_get_running_partition() ? fixture.running_project
                                                                           : fixture.last_project);
  return ESP_OK;
}

esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition, esp_ota_img_states_t *state) {
  *state = partition == esp_ota_get_running_partition() ? fixture.running_state : fixture.previous_state;
  return ESP_OK;
}

bool esp_ota_check_rollback_is_possible() { return fixture.rollback_available; }

esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition) {
  fixture.selected = partition;
  return ESP_OK;
}

esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot() {
  ++fixture.rollback_calls;
  if (fixture.rollback_fails)
    return -1;
  fixture.selected = &apps.at(1 - fixture.running);
  return ESP_OK;
}

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *handle) {
  fixture.timer = *args;
  *handle = &fixture.timer;
  return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t duration) {
  assert(timer == &fixture.timer);
  if (fixture.timer_fails)
    return -1;
  fixture.duration = duration;
  return ESP_OK;
}

void esp_restart() { ++fixture.restarts; }

int main() {
  for (const unsigned running : {0u, 1u}) {
    fixture = {.running = running};
    assert(start_esphome_trial());
    assert(fixture.duration == 180'000'000);
    assert(!fixture.selected && !fixture.rollback_calls);
    fixture.timer.callback(nullptr);
    assert(fixture.rollback_calls == 1 && fixture.selected == &apps.at(1 - running));
  }

  fixture = {.legacy_layout = false};
  assert(!start_esphome_trial());
  assert(fixture.restarts == 1 && !fixture.duration && !fixture.selected);

  fixture = {.last_project = "espframe_native_v1"};
  assert(!start_esphome_trial());
  assert(fixture.restarts == 1 && !fixture.duration && !fixture.selected);

  fixture = {.previous_state = 0};
  assert(!start_esphome_trial());
  assert(fixture.restarts == 1 && !fixture.duration && !fixture.selected);

  fixture = {.rollback_available = false};
  assert(!start_esphome_trial());
  assert(fixture.restarts == 1 && !fixture.duration && !fixture.selected);

  fixture = {.running_state = 0};
  assert(!start_esphome_trial());
  assert(fixture.restarts == 1 && !fixture.duration && fixture.selected == &apps.at(1));

  fixture = {.timer_fails = true};
  assert(!start_esphome_trial());
  assert(fixture.rollback_calls == 1 && fixture.selected == &apps.at(1) && !fixture.duration);

  fixture = {.rollback_fails = true};
  assert(start_esphome_trial());
  fixture.timer.callback(nullptr);
  assert(fixture.rollback_calls == 1 && fixture.restarts == 1 && !fixture.selected);

  fixture = {};
  assert(start_esphome_trial(300));
  assert(fixture.duration == 300'000'000);
  espframe::native::end_esphome_trial();
  assert(fixture.rollback_calls == 1 && fixture.selected == &apps[1]);

  for (const unsigned running : {0u, 1u}) {
    fixture = {.running = running};
    assert(std::strcmp(esphome_v1_trial_readiness(true), "Waiting for ESPHome confirmation") == 0);
    fixture.running_state = ESP_OTA_IMG_VALID;
    assert(std::strcmp(esphome_v1_trial_readiness(true), "Ready") == 0);
    assert(std::strcmp(esphome_v1_trial_readiness(false), "Bootloader rollback unverified") == 0);
    fixture.selected = &apps.at(1 - running);
    assert(std::strcmp(esphome_v1_trial_readiness(true), "Another boot is staged") == 0);
  }
  fixture = {.legacy_layout = false};
  assert(std::strcmp(esphome_v1_trial_readiness(true), "Unsupported V1 partition layout") == 0);
  fixture = {.wrong_target = true};
  assert(std::strcmp(esphome_v1_trial_readiness(true), "Unsupported V1 partition layout") == 0);
  fixture = {.running_project = "immich-frame-10inch-v2"};
  assert(std::strcmp(esphome_v1_trial_readiness(true), "Unexpected ESPHome app") == 0);

  for (const auto *project : {"immich-frame-10inch", "immich-frame-10inch-v2", "immich-frame-10inch-v3"}) {
    for (const unsigned running : {0u, 1u}) {
      fixture = {.running = running, .running_project = project};
      assert(esphome_migration_readiness(project, true) == Readiness::UnconfirmedApp);
      fixture.running_state = ESP_OTA_IMG_VALID;
      assert(esphome_migration_readiness(project, true) == Readiness::Ready);
      assert(esphome_migration_readiness(project, false) == Readiness::UnverifiedRollback);
      fixture.selected = &apps.at(1 - running);
      assert(esphome_migration_readiness(project, true) == Readiness::StagedBoot);
    }
  }
  fixture = {.running_project = "immich-frame-10inch-v3"};
  assert(esphome_migration_readiness("immich-frame-10inch-v2", true) == Readiness::UnexpectedApp);
}
