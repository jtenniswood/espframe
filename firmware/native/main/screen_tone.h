#pragma once

#include <cstdint>
#include <span>

namespace espframe::native {

struct ScreenToneSettings {
  bool base_enabled{};
  uint8_t base_amount{};
  bool night_enabled{};
  uint8_t night_amount{50};

  float warmth(float night_fraction) const;
};

void tint_photo(std::span<uint16_t> pixels, float warmth);

}  // namespace espframe::native
