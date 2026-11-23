#pragma once

#include "photo_metadata.h"
#include "slide_geometry.h"

#include <algorithm>
#include <array>
#include <ctime>
#include <span>
#include <string>

namespace espframe::native {

std::string photo_caption_date(const PhotoMetadata &metadata, const PhotoMetadataSettings &settings,
                               const std::optional<std::tm> &local);

struct PhotoCaptionLine {
  // Two 64-byte location fields plus the separator.
  std::array<char, 130> text{};
  int width{};
  uint8_t length{};
  bool ellipsis{};
};

struct PhotoCaption {
  PhotoCaptionLine location;
  PhotoCaptionLine date;

  int band_rows() const;
  bool fits_beside_clock(ScreenRotation rotation) const;
};

PhotoCaption prepare_photo_caption(std::span<const uint8_t> locations, std::span<const uint8_t> dates,
                                   const PhotoMetadata &metadata, const PhotoMetadataSettings &settings,
                                   bool show_clock, const std::optional<std::tm> &local = {},
                                   ScreenRotation rotation = ScreenRotation::Normal);
void paint_photo_caption(uint16_t *frame, std::span<const uint8_t> locations, std::span<const uint8_t> dates,
                         const PhotoCaption &caption, ScreenRotation rotation, uint8_t opacity);

struct PhotoCaptionSample {
  SlideIndex slide{};
  uint8_t opacity{};
  bool operator==(const PhotoCaptionSample &) const = default;
};

class PhotoCaptionFade {
 public:
  static constexpr uint8_t opaque = 15;
  static constexpr int64_t still_delay_us = 250'000;
  static constexpr int64_t fade_in_us = 140'000;
  static constexpr int64_t fade_out_us = 70'000;

  PhotoCaptionSample update(int64_t now, std::optional<SlideIndex> settled) {
    if (settled != settled_) {
      if (settled_) {
        const auto opacity = resting_opacity(now);
        if (opacity) {
          outgoing_ = {*settled_, opacity};
          fade_out_started_ = now;
        }
      }
      settled_ = settled;
      still_since_ = now;
    }
    if (settled_) {
      const auto opacity = resting_opacity(now);
      if (opacity)
        return {*settled_, opacity};
    }
    const auto remaining = std::max<int64_t>(0, fade_out_us - (now - fade_out_started_));
    const auto opacity = static_cast<uint8_t>(outgoing_.opacity * remaining / fade_out_us);
    return opacity ? PhotoCaptionSample{outgoing_.slide, opacity} : PhotoCaptionSample{};
  }

 private:
  uint8_t resting_opacity(int64_t now) const {
    const auto elapsed = std::clamp<int64_t>(now - still_since_ - still_delay_us, 0, fade_in_us);
    return static_cast<uint8_t>(opaque * elapsed / fade_in_us);
  }

  std::optional<SlideIndex> settled_;
  PhotoCaptionSample outgoing_;
  int64_t still_since_{};
  int64_t fade_out_started_{};
};

}  // namespace espframe::native
