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

// At a cold cache boundary, move the photo a short distance under the
// finger. Repeat its edge pixels in the uncovered strip to keep the panel filled.
inline void blit_slide_edge(uint16_t *output, const uint16_t *photo, int native_width, int native_height, int offset) {
  const size_t stride = static_cast<size_t>(native_width);
  const size_t rows = static_cast<size_t>(offset < 0 ? -offset : offset);
  const size_t retained = static_cast<size_t>(native_height) - rows;
  const auto *edge = offset < 0 ? photo : photo + (native_height - 1) * stride;
  auto *strip = offset < 0 ? output : output + retained * stride;
  auto *body = offset < 0 ? output + rows * stride : output;
  const auto *first_row = offset < 0 ? photo : photo + rows * stride;
  std::memcpy(body, first_row, retained * stride * sizeof(*output));
  for (size_t row = 0; row < rows; ++row)
    std::memcpy(strip + row * stride, edge, stride * sizeof(*output));
}

}  // namespace espframe::native
