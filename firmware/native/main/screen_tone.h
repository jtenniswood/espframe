#pragma once

#include "preparation_checkpoint.h"

#include <atomic>
#include <cstdint>
#include <span>

namespace espframe::native {

inline std::atomic<bool> active_warm_tone_override{};

struct ScreenToneSettings {
  bool base_enabled{};
  uint8_t base_amount{};
  bool night_enabled{};
  uint8_t night_amount{50};
  bool night_override{};

  bool needs_time() const { return night_enabled || night_override; }
  float warmth(float night_fraction) const;
};

bool tint_photo(std::span<uint16_t> pixels, float warmth, PreparationCheckpoint checkpoint = {});

}  // namespace espframe::native
