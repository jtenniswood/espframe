#include "screen_tone.h"

#include "warm_tone_luts.h"

#include <algorithm>
#include <array>

namespace espframe::native {

float ScreenToneSettings::warmth(float night_fraction) const {
  const auto base = base_enabled ? base_amount : 0;
  const auto night = night_override ? night_amount
                   : night_enabled ? night_amount * std::clamp(night_fraction, 0.0f, 1.0f) : 0.0f;
  return (base + night) / 100.0f;
}

bool tint_photo(std::span<uint16_t> pixels, float warmth, PreparationCheckpoint checkpoint) {
  if (warmth < 0.01f)
    return true;
  WarmToneLuts lut;
  build_warm_tone_luts(0.0f, warmth, lut);
  std::array<uint16_t, 32> red{};
  std::array<uint16_t, 64> green{};
  for (size_t i = 0; i < red.size(); ++i)
    red[i] = static_cast<uint16_t>(lut.r[i] << 11);
  for (size_t i = 0; i < green.size(); ++i)
    green[i] = static_cast<uint16_t>(lut.g[i] << 5);
  constexpr size_t batch = 8 * 1024;
  while (!pixels.empty()) {
    if (!checkpoint())
      return false;
    const auto chunk = pixels.first(std::min(batch, pixels.size()));
    for (auto &pixel : chunk)
      pixel = red[pixel >> 11] | green[(pixel >> 5) & 63] | lut.b[pixel & 31];
    pixels = pixels.subspan(chunk.size());
  }
  return true;
}

}  // namespace espframe::native
