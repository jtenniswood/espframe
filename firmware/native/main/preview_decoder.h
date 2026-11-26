#pragma once

#include "photo_framing.h"
#include "photo_orientation.h"
#include "preparation_checkpoint.h"
#include "slide_geometry.h"

#include <cstddef>
#include <cstdint>

namespace espframe::native {

class P4JpegDecoder;
enum class PreviewTarget { Slide, PairPane, OptionalPair, RequiredPair };

struct PreviewLayout {
  PreviewTarget target{PreviewTarget::Slide};
  DisplayMode photo{DisplayMode::Fit};
  DisplayMode pair{DisplayMode::Fit};
  ScreenRotation rotation{ScreenRotation::Normal};
};
inline bool is_jpeg_preview(const uint8_t *encoded, size_t length) {
  return length >= 2 && encoded[0] == 0xff && encoded[1] == 0xd8;
}
bool is_webp_preview(const uint8_t *encoded, size_t length);

// Reuse the displayed pixels if the original compressed preview has been evicted.
bool reframe_cached_photo(uint16_t *destination, const uint16_t *source, ScreenRotation from,
                          ScreenRotation to, bool paired, DisplayMode mode, PreparationCheckpoint checkpoint);

void paint_portrait_pair_divider(uint16_t *panel, ScreenRotation rotation, PairDivider divider);

// Worker-owned packed RGB565 preview. Retain the primary until the optional
// companion is ready so a single photo can still fill the slide on failure.
class DecodedPreview {
 public:
  enum class Result { Ready, Invalid, NoMemory, Interrupted, OrientationMismatch };

  DecodedPreview() = default;
  ~DecodedPreview();
  DecodedPreview(const DecodedPreview &) = delete;
  DecodedPreview &operator=(const DecodedPreview &) = delete;

  Result decode(const uint8_t *encoded, size_t length, PreviewLayout layout = {},
                P4JpegDecoder *hardware = nullptr, PreparationCheckpoint checkpoint = {},
                PhotoOrientation orientation = PhotoOrientation::Any);
  bool portrait() const { return this->portrait_; }
  bool hardware_jpeg() const { return this->hardware_jpeg_; }
  bool render(uint16_t *panel, const DecodedPreview *companion = nullptr, DisplayMode pair_mode = DisplayMode::Fit,
              ScreenRotation rotation = ScreenRotation::Normal, PreparationCheckpoint checkpoint = {}) const;

 private:
  enum class HorizontalAlignment { Left, Center, Right };
  bool paint_(uint16_t *panel, int x, int width, DisplayMode mode, HorizontalAlignment alignment, ScreenRotation rotation,
              PreparationCheckpoint checkpoint) const;

  uint16_t *pixels_{};
  int width_{};
  int height_{};
  DisplayMode display_mode_{DisplayMode::Fit};
  bool portrait_{};
  bool hardware_jpeg_{};
};

}  // namespace espframe::native
