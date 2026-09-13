#pragma once

#include "../components/espframe/automation_controller.h"
#include <cstdio>
#include <cstdlib>

// Only the hardware endpoints are replaced. Rotation actions and select callbacks
// come from production YAML; scripts, switches and delays use ESPHome's runtime.
struct AutomationTestDisplay {
  int rotation = -1;
  void set_rotation(int value) { rotation = value; }
};
struct AutomationTestCore {
  template<typename Display, typename Pairing>
  void apply_screen_rotation(const std::string &option, const std::array<int, 4> &rotations,
                             Display *display, Pairing *pairing) {
    esphome::espframe::apply_rotation_plan(esphome::espframe::rotation_plan(option, rotations), display, pairing);
  }
};
struct AutomationTestState {
  int target_slot = 0, active_slot = 0;
  struct { bool workflow_busy = false; } portrait;
  struct { bool fetch_in_flight[3]{}; } slot_flags;
};
inline void automation_require(bool passed, const char *message) {
  if (passed) return;
  std::fprintf(stderr, "Automation runtime FAIL: %s\n", message);
  std::exit(1);
}
