#pragma once

#include "photo_framing.h"
#include "photo_orientation.h"
#include "preparation_checkpoint.h"
#include "slide_geometry.h"

#include <cstddef>
#include <cstdint>

namespace espframe::native {

class P4JpegDecoder;
bool is_webp_preview(const uint8_t *encoded, size_t length);

// Worker-owned packed RGB565 preview. Retain the primary until the optional
// companion is ready so a single photo can still fill the slide on failure.
class DecodedPreview {
 public:
  enum class Result { Ready, Invalid, NoMemory, Interrupted, OrientationMismatch };

  DecodedPreview() = default;
  ~DecodedPreview();
  DecodedPreview(const DecodedPreview &) = delete;
  DecodedPreview &operator=(const DecodedPreview &) = delete;

  Result decode(const uint8_t *encoded, size_t length, bool companion = false, P4JpegDecoder *hardware = nullptr,
                DisplayMode mode = DisplayMode::Fit, PreparationCheckpoint checkpoint = {},
                PhotoOrientation orientation = PhotoOrientation::Any);
  bool portrait() const { return this->portrait_; }
  bool hardware_jpeg() const { return this->hardware_jpeg_; }
  bool render(uint16_t *panel, const DecodedPreview *companion = nullptr, DisplayMode mode = DisplayMode::Fit,
              ScreenRotation rotation = ScreenRotation::Normal, PreparationCheckpoint checkpoint = {}) const;

 private:
  bool paint_(uint16_t *panel, int x, int width, DisplayMode mode, ScreenRotation rotation,
              PreparationCheckpoint checkpoint) const;

  uint16_t *pixels_{};
  int width_{};
  int height_{};
  bool portrait_{};
  bool hardware_jpeg_{};
};

}  // namespace espframe::native
