#include "screen_tone.h"
#include "warm_tone_luts.h"

#include <array>
#include <cassert>
#include <cmath>
#include <numeric>

using namespace espframe::native;

int main() {
  ScreenToneSettings settings;
  assert(settings.warmth(1.0f) == 0.0f);
  assert(!settings.needs_time());
  settings.night_override = true;
  assert(settings.warmth(0.0f) == 0.5f && settings.needs_time());
  settings.base_enabled = true;
  settings.base_amount = 25;
  assert(settings.warmth(0.0f) == 0.75f);
  settings.night_override = false;
  assert(settings.warmth(1.0f) == 0.25f);
  settings.night_enabled = true;
  assert(settings.warmth(0.0f) == 0.25f);
  assert(settings.warmth(0.5f) == 0.5f);
  assert(settings.warmth(1.0f) == 0.75f);
  settings.base_amount = settings.night_amount = 100;
  assert(settings.warmth(1.0f) == 2.0f);

  std::array<uint16_t, 65'536> pixels{};
  std::iota(pixels.begin(), pixels.end(), uint32_t{0});
  const auto raw = pixels;
  tint_photo(pixels, 0.0f);
  tint_photo(pixels, 0.005f);
  assert(raw == pixels);
  tint_photo(pixels, settings.warmth(1.0f));
  assert(pixels[0] == 0);
  assert((pixels.back() >> 11) == 31);
  assert(((pixels.back() >> 5) & 63) < 63);
  assert((pixels.back() & 31) < 31);

  WarmToneLuts legacy{};
  build_warm_tone_luts(0, 2.0f, legacy);
  for (size_t i = 0; i < pixels.size(); ++i) {
    assert(pixels[i] == ((legacy.r[i >> 11] << 11) | (legacy.g[(i >> 5) & 63] << 5) | legacy.b[i & 31]));
  }
  auto interrupted = raw;
  int chunks{};
  assert(!tint_photo(interrupted, settings.warmth(1.0f), {&chunks, [](void *context) {
    return ++*static_cast<int *>(context) == 1;
  }}));
  assert(interrupted[1023] == pixels[1023] && interrupted[1023] != raw[1023]);
  assert(interrupted.back() == raw.back());
}
