#include <cassert>
#include <iostream>
#include <limits>
#include <vector>

#include "components/espframe/automation_controller.h"
#include "components/espframe/firmware_version.h"
#include "components/espframe/slideshow_component.h"

using namespace esphome::espframe;

static void test_automatic_firmware_version_order() {
  assert(firmware_version_is_newer("v1.18.0", "v1.17.2"));
  assert(!firmware_version_is_newer("v1.17.2", "v1.18.0"));
  assert(!firmware_version_is_newer("v1.17.2", "v1.17.2"));
  assert(firmware_version_is_newer("v1.18.0", "v1.18.0-beta.4"));
  assert(!firmware_version_is_newer("v1.18.0-beta.4", "v1.18.0"));
  assert(firmware_version_is_newer("v1.18.0-beta.10", "v1.18.0-beta.9"));
  assert(!firmware_version_is_newer("v1.17.2", "v1.18.0-hosted.2"));
  assert(!firmware_version_is_newer("v1.17.2", "dev"));
  assert(!firmware_version_is_newer("v1.18.0-beta..1", "v1.17.2"));
}

static void test_recovery_stop_order() {
  std::vector<int> calls;
  struct Worker {
    int index;
    std::vector<int> &calls;
    bool running = true;
    void stop() { calls.push_back(index); running = false; }
  };
  Worker first{1, calls}, second{2, calls}, third{3, calls};
  stop_scripts_in_order(&first, &second, &third);
  assert((calls == std::vector<int>{1, 2, 3}));
  assert(!first.running && !second.running && !third.running);
  // Recovery is also safe after workers have already stopped.
  stop_scripts_in_order(&first, &second, &third);
  assert((calls == std::vector<int>{1, 2, 3, 1, 2, 3}));
}

static void test_rotation_settings_order() {
  std::vector<std::string> ordered{"developer", "pairing", "rotation"};
  do {
    auto writes = ordered;
    const auto developer = std::find(writes.begin(), writes.end(), "developer") - writes.begin();
    const auto rotation = std::find(writes.begin(), writes.end(), "rotation") - writes.begin();
    const auto pairing = std::find(writes.begin(), writes.end(), "pairing") - writes.begin();
    order_rotation_settings(writes, developer, rotation, pairing);
    assert((writes == std::vector<std::string>{"developer", "rotation", "pairing"}));
  } while (std::next_permutation(ordered.begin(), ordered.end()));
  assert(rotation_requires_developer_mode("90", false));
  assert(!rotation_requires_developer_mode("90", true));
  assert(!rotation_requires_developer_mode("0", false));

  std::vector<std::string> pairing_first{"pairing", "unrelated", "rotation"};
  order_rotation_settings(pairing_first, pairing_first.size(), 2, 0);
  assert((pairing_first == std::vector<std::string>{"unrelated", "rotation", "pairing"}));
  std::vector<std::string> rotation_first{"rotation", "unrelated", "pairing"};
  order_rotation_settings(rotation_first, rotation_first.size(), 0, 2);
  assert((rotation_first == std::vector<std::string>{"rotation", "unrelated", "pairing"}));
  std::vector<std::string> developer_and_pairing{"pairing", "developer"};
  order_rotation_settings(developer_and_pairing, 1, developer_and_pairing.size(), 0);
  assert((developer_and_pairing == std::vector<std::string>{"developer", "pairing"}));
  std::vector<std::string> pairing_only{"pairing", "unrelated"};
  order_rotation_settings(pairing_only, pairing_only.size(), pairing_only.size(), 0);
  assert((pairing_only == std::vector<std::string>{"pairing", "unrelated"}));
}

static void test_schedule_wake_timeout() {
  assert(schedule_wake_timeout_ms(std::numeric_limits<float>::quiet_NaN()) == 60000);
  assert(schedule_wake_timeout_ms(0) == 60000);
  assert(schedule_wake_timeout_ms(-1) == 60000);
  assert(schedule_wake_timeout_ms(1) == 10000);
  assert(schedule_wake_timeout_ms(10.5f) == 10500);
  assert(schedule_wake_timeout_ms(7200) == 3600000);
}

static void test_configuration_ntp_validation() {
  for (const char *key : {"ntp_server_1", "ntp_server_2", "ntp_server_3"}) {
    assert(validate_configuration_text(key, ""));
    assert(validate_configuration_text(key, "  pool.ntp.org  "));
    assert(validate_configuration_text(key, "2001:db8::1"));
    assert(!validate_configuration_text(key, "two hosts"));
    assert(!validate_configuration_text(key, "https://pool.ntp.org"));
  }
  assert(validate_configuration_text("immich_url", "https://immich.example/path"));
}

static void test_rotation() {
  std::vector<int> calls;
  struct Display {
    std::vector<int> &calls;
    void set_rotation(int rotation) { calls.push_back(rotation); }
  } display{calls};
  struct Pairing {
    std::vector<int> &calls;
    bool state = false;
    void turn_on() { calls.push_back(1); state = true; }
    void turn_off() { calls.push_back(-1); state = false; }
  } pairing{calls};
  for (const std::string rotation : {"", "0", "180"}) {
    assert(resolve_portrait_pairing(rotation, true));
    assert(!resolve_portrait_pairing(rotation, false));
  }
  for (const std::string rotation : {"90", "270"}) {
    assert(!resolve_portrait_pairing(rotation, true));
    assert(!resolve_portrait_pairing(rotation, false));
  }
  const std::array<std::string, 4> options{"0", "90", "180", "270"};
  // Both current panels use the offset mapping; the helper must also preserve
  // board substitutions when another panel supplies a different mapping.
  for (const auto &mapping : {std::array<int, 4>{90, 180, 270, 0}, std::array<int, 4>{0, 90, 180, 270}}) {
    for (size_t i = 0; i < options.size(); ++i) {
      for (bool enabled : {false, true}) {
        pairing.state = enabled;
        // Boot and repeated rotation/developer-setting applications must keep
        // an explicit OFF choice. Portrait rotation still disables pairing.
        for (int repeat = 0; repeat < 3; ++repeat) {
          calls.clear();
          apply_rotation_plan(rotation_plan(options[i], mapping), &display, &pairing);
          const bool landscape = i % 2 == 0;
          assert(pairing.state == (landscape && enabled));
          assert((calls == (landscape ? std::vector<int>{mapping[i]} : std::vector<int>{mapping[i], -1})));
        }
      }
    }
    // Returning from portrait to landscape must not enable pairing either.
    pairing.state = true;
    apply_rotation_plan(rotation_plan("90", mapping), &display, &pairing);
    apply_rotation_plan(rotation_plan("0", mapping), &display, &pairing);
    assert(!pairing.state);
    for (const std::string invalid : {"", "45", "360", "invalid"}) {
      calls.clear();
      apply_rotation_plan(rotation_plan(invalid, mapping), &display, &pairing);
      assert(calls.empty());
    }
  }
}

static void test_slot_fetch_guards() {
  size_t cases = 0;
  for (int active = 0; active < 3; ++active) {
    for (int target = 0; target < 3; ++target) {
      for (int busy = 0; busy < 8; ++busy) {
        for (int inputs = 0; inputs < 64; ++inputs) {
          SlideshowRuntimeState state;
          state.active_slot = active;
          state.target_slot = target;
          for (int slot = 0; slot < 3; ++slot) state.slot_flags.fetch_in_flight[slot] = (busy & (1 << slot)) != 0;
          const bool wifi = inputs & 1, configured = inputs & 2, paused = inputs & 4;
          const bool cooldown = inputs & 8, image_busy = inputs & 16;
          state.portrait.workflow_busy = inputs & 32;
          // Ordered predicates from the previous YAML. First matching guard wins,
          // including cases where several independent reasons block a request.
          const std::array<bool, 8> blocked{
            !wifi, !configured, paused, cooldown,
            target != active && state.portrait.workflow_busy,
            busy != 0 && (busy & (1 << target)) == 0,
            (busy & (1 << target)) != 0, image_busy,
          };
          const std::array<SlotFetchDecision, 8> reasons{
            SlotFetchDecision::WIFI_DISCONNECTED, SlotFetchDecision::MISSING_CONFIG,
            SlotFetchDecision::PAUSED, SlotFetchDecision::COOLDOWN,
            SlotFetchDecision::PORTRAIT_BUSY, SlotFetchDecision::ANOTHER_FETCH_BUSY,
            SlotFetchDecision::SLOT_BUSY, SlotFetchDecision::IMAGE_BUSY,
          };
          auto expected = SlotFetchDecision::ALLOW;
          bool deferred = false;
          for (size_t i = 0; i < blocked.size(); ++i) {
            if (!blocked[i]) continue;
            expected = reasons[i];
            deferred = i == 4 || i == 5 || i == 7;
            break;
          }
          const auto actual = slot_fetch_decision(state, wifi, configured, paused, cooldown, image_busy);
          assert(actual == expected);
          assert(slot_fetch_needs_defer(actual) == deferred);
          assert(state.active_slot == active && state.target_slot == target);
          for (int slot = 0; slot < 3; ++slot)
            assert(state.slot_flags.fetch_in_flight[slot] == ((busy & (1 << slot)) != 0));
          ++cases;
        }
      }
    }
  }
  for (int invalid : {-1, 3, 127}) {
    SlideshowRuntimeState state;
    state.target_slot = invalid;
    assert(slot_fetch_decision(state, true, true, false, false, false) == SlotFetchDecision::INVALID_SLOT);
    assert(slot_fetch_decision(state, false, true, false, false, false) == SlotFetchDecision::WIFI_DISCONNECTED);
    assert(!slot_fetch_needs_defer(SlotFetchDecision::INVALID_SLOT));
  }
  std::cout << "Checked " << cases << " valid-slot guard combinations\n";
}

static void test_failed_portrait_preload() {
  for (bool pairs_only : {false, true}) {
    for (int slot : {0, 1}) {
      EspFrameSlideshow slideshow;
      auto &state = slideshow.state();
      state.active_slot = 0;
      state.target_slot = 2;
      state.portrait_preload_slot = slot;
      state.portrait_preload_right_ready = true;
      state.preload_noncritical_in_flight = true;
      state.noncritical_remote_updates_in_flight = 1;
      slideshow.on_portrait_preload_error(EspFrameSlideshow::PortraitSide::RIGHT, 100, pairs_only);
      assert(!state.portrait_preload_right_ready);
      assert(!state.preload_noncritical_in_flight);
      assert(state.noncritical_remote_updates_in_flight == 0);
      assert(state.diagnostic_reason == "portrait preload right error");

      SlideshowCommand command;
      assert(slideshow.pop_command(command));
      assert(command.kind == SLIDESHOW_COMMAND_LOG_DIAG);
      const bool should_recover = slot >= 0 && (pairs_only || slot == 0);
      assert(slideshow.pop_command(command) == should_recover);
      if (!should_recover) continue;
      assert(command.slot == slot);
      assert(command.kind == (pairs_only ? SLIDESHOW_COMMAND_REFETCH_REJECTED_SLOT
                                         : SLIDESHOW_COMMAND_DISPLAY_CURRENT));
      assert(state.portrait_preload_slot == -1);
      assert(state.target_slot == 2);
      assert(!slideshow.pop_command(command));
    }
  }
}

static void test_active_preload_presentation() {
  for (bool pairs_only : {false, true}) {
    EspFrameSlideshow slideshow;
    auto &state = slideshow.state();
    state.active_slot = 1;
    state.portrait_preload_slot = 2;
    state.portrait_preload_left_ready = true;
    state.portrait_preload_right_ready = true;
    assert(!slideshow.active_preloaded_pair_ready());

    state.portrait_preload_slot = 1;
    state.portrait_preload_right_ready = false;
    assert(!slideshow.active_preloaded_pair_ready());
    state.portrait_preload_right_ready = true;
    assert(slideshow.active_preloaded_pair_ready());

    slideshow.display_completed_active_preload();
    SlideshowCommand command;
    assert(slideshow.pop_command(command));
    assert(command.kind == SLIDESHOW_COMMAND_DISPLAY_PRELOADED_PAIR && command.slot == 1);

    slideshow.recover_active_preloaded_pair(100, pairs_only);
    assert(slideshow.pop_command(command));
    assert(command.slot == 1);
    assert(command.kind == (pairs_only ? SLIDESHOW_COMMAND_REFETCH_REJECTED_SLOT
                                       : SLIDESHOW_COMMAND_DISPLAY_CURRENT));
    assert(!slideshow.active_preloaded_pair_ready());
  }
}

static void test_live_portrait_presentation() {
  using Presentation = EspFrameSlideshow::LivePairPresentation;
  for (bool pairs_only : {false, true}) {
    EspFrameSlideshow slideshow;
    auto &state = slideshow.state();
    state.active_slot = 1;
    state.portrait.left_ready = true;
    assert(slideshow.prepare_live_portrait_pair(true) == Presentation::WAIT);
    state.portrait.right_ready = true;
    assert(slideshow.prepare_live_portrait_pair(true) == Presentation::READY);
    state.portrait.is_pair = true;
    state.portrait.workflow_busy = true;
    assert(slideshow.recover_live_portrait_pair(100, pairs_only) == !pairs_only);
    assert(!state.portrait.left_ready && !state.portrait.right_ready);
    assert(!state.portrait.is_pair && !state.portrait.workflow_busy);
    assert(state.active_slot_displayed == !pairs_only);
    SlideshowCommand command;
    assert(slideshow.pop_command(command) == pairs_only);
    if (pairs_only) assert(command.kind == SLIDESHOW_COMMAND_REFETCH_REJECTED_SLOT && command.slot == 1);

    state.portrait.left_ready = true;
    assert(slideshow.prepare_live_portrait_pair(false) == Presentation::HIDE);
    assert(!state.portrait.left_ready);
  }
}

int main() {
  test_live_portrait_presentation();
  test_active_preload_presentation();
  test_failed_portrait_preload();
  test_recovery_stop_order();
  test_rotation();
  test_rotation_settings_order();
  test_schedule_wake_timeout();
  test_configuration_ntp_validation();
  test_slot_fetch_guards();
  test_automatic_firmware_version_order();
  std::cout << "Automation controller tests passed\n";
}
