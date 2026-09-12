#pragma once

#include "esphome/core/defines.h"

#ifdef ESPFRAME_MEMORY_DIAGNOSTICS
#include "esphome/core/component.h"

namespace esphome::espframe {
// Opt-in observers bracket LVGL's PROCESSOR setup priority. They do not
// participate in normal builds or change the allocator's placement policy.
class MemorySetupProbe : public Component {
 public:
  explicit MemorySetupProbe(bool before) : before_(before) {}
  float get_setup_priority() const override {
    return setup_priority::PROCESSOR + (before_ ? 1.0f : -1.0f);
  }
  void setup() override;

 protected:
  bool before_;
};

void record_loop_stack(const char *phase);
}  // namespace esphome::espframe
#endif
