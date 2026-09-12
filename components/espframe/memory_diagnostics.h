#pragma once

#include "esphome/core/defines.h"

#include "esphome/core/component.h"

namespace esphome::espframe {
// Bracket pinned ESPHome LVGL setup so only its setup-task allocations
// request PSRAM. Optional diagnostics report the same allocation window.
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
