#include <cassert>
#include <iostream>
#include <vector>

#include "components/espframe/automation_controller.h"
#include "components/espframe/slideshow_component.h"

using namespace esphome::espframe;

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

static void test_rotation() {
  std::vector<int> calls;
  struct Display {
    std::vector<int> &calls;
    void set_rotation(int rotation) { calls.push_back(rotation); }
  } display{calls};
  struct Pairing {
    std::vector<int> &calls;
    void turn_on() { calls.push_back(1); }
    void turn_off() { calls.push_back(-1); }
  } pairing{calls};
  const std::array<std::string, 4> options{"0", "90", "180", "270"};
  // Both current panels use the offset mapping; the helper must also preserve
  // board substitutions when another panel supplies a different mapping.
  for (const auto &mapping : {std::array<int, 4>{90, 180, 270, 0}, std::array<int, 4>{0, 90, 180, 270}}) {
    for (size_t i = 0; i < options.size(); ++i) {
      calls.clear();
      apply_rotation_plan(rotation_plan(options[i], mapping), &display, &pairing);
      assert((calls == std::vector<int>{mapping[i], i % 2 == 0 ? 1 : -1}));
    }
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

int main() {
  test_recovery_stop_order();
  test_rotation();
  test_slot_fetch_guards();
  std::cout << "Automation controller tests passed\n";
}
