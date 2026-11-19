#pragma once

#include <algorithm>

namespace espframe::native {

enum class DisplayMode { Fit, Fill };
enum class PairFraming { FollowPhotos, Fit, Fill };

inline const char *display_mode_name(DisplayMode mode) { return mode == DisplayMode::Fill ? "Fill" : "Fit"; }

inline const char *pair_framing_name(PairFraming choice) {
  switch (choice) {
    case PairFraming::Fit: return "Fit";
    case PairFraming::Fill: return "Fill";
    case PairFraming::FollowPhotos: return "Follow photo framing";
  }
  return "Follow photo framing";
}

struct PhotoRect {
  int x;
  int y;
  int width;
  int height;
};

struct PhotoFraming {
  PhotoRect source;
  int width;
  int height;
  bool portrait;
};

inline PhotoFraming frame_photo(int width, int height, int max_width, int max_height, DisplayMode mode) {
  const bool wide = width * max_height >= height * max_width;
  const bool portrait = height > width;
  if (mode == DisplayMode::Fill) {
    const int crop_width = wide ? std::max(1, height * max_width / max_height) : width;
    const int crop_height = wide ? height : std::max(1, width * max_height / max_width);
    return {{(width - crop_width) / 2, (height - crop_height) / 2, crop_width, crop_height},
            max_width, max_height, portrait};
  }
  return {{0, 0, width, height}, wide ? max_width : std::max(1, width * max_height / height),
          wide ? std::max(1, height * max_width / width) : max_height, portrait};
}

}  // namespace espframe::native
