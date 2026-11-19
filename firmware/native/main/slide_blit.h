#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace espframe::native {

// The panel scans landscape x along its native rows after a 90-degree
// rotation. Adjacent clipped slides therefore need at most two contiguous
// copies. The cache stores each slide in that same native order.
inline void blit_slides(uint16_t *output, const uint16_t *left, const uint16_t *right,
                        int native_width, int native_height, int offset) {
  const size_t stride = static_cast<size_t>(native_width);
  const size_t left_pixels = static_cast<size_t>(native_height - offset) * stride;
  std::memcpy(output, left + offset * stride, left_pixels * sizeof(*output));
  if (offset != 0)
    std::memcpy(output + left_pixels, right, offset * stride * sizeof(*output));
}

}  // namespace espframe::native
