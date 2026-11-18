#pragma once

#include <algorithm>
#include <cstdlib>

namespace esphome::espframe {

inline bool is_slideshow_swipe(int horizontal_delta, int vertical_delta, int visible_width) {
  const int minimum_distance = std::max(48, visible_width / 20);
  const int horizontal_distance = std::abs(horizontal_delta);
  return horizontal_distance >= minimum_distance &&
         horizontal_distance * 2 > std::abs(vertical_delta) * 3;
}

}  // namespace esphome::espframe
