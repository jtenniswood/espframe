#pragma once

#include "slide_geometry.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace espframe::native {

struct SlideSpan {
  const uint16_t *first;
  const uint16_t *second;
  int offset;
};

// Cached pixels already face the viewer. Rotating the whole swipe swaps the
// order of its two slices and mirrors their split, without another pixel pass.
inline SlideSpan orient_slide_span(const uint16_t *left, const uint16_t *right, int viewport_width,
                                   int offset, ScreenRotation rotation) {
  if (axis_direction(rotation) < 0 && offset != 0)
    return {right, left, viewport_width - offset};
  return {left, right, offset};
}

// The panel scans landscape x along its native rows after a 90-degree
// rotation. Adjacent clipped slides therefore need at most two contiguous
// copies. The cache stores each slide in that same native order.
template <class CopyRows>
void blit_slides(uint16_t *output, const uint16_t *left, const uint16_t *right,
                 int native_width, int native_height, int offset, CopyRows copy_rows) {
  const size_t stride = static_cast<size_t>(native_width);
  copy_rows(output, left + offset * stride, native_width, native_height - offset);
  if (offset != 0)
    copy_rows(output + (native_height - offset) * stride, right, native_width, offset);
}

// At a cold cache boundary, move the photo a short distance under the
// finger. Repeat its edge pixels in the uncovered strip to keep the panel filled.
template <class CopyRows>
void blit_slide_edge(uint16_t *output, const uint16_t *photo, int native_width, int native_height, int offset,
                     CopyRows copy_rows) {
  const size_t stride = static_cast<size_t>(native_width);
  const size_t rows = static_cast<size_t>(offset < 0 ? -offset : offset);
  const size_t retained = static_cast<size_t>(native_height) - rows;
  const auto *edge = offset < 0 ? photo : photo + (native_height - 1) * stride;
  auto *strip = offset < 0 ? output : output + retained * stride;
  auto *body = offset < 0 ? output + rows * stride : output;
  const auto *first_row = offset < 0 ? photo : photo + rows * stride;
  copy_rows(body, first_row, native_width, static_cast<int>(retained));
  for (size_t row = 0; row < rows; ++row)
    std::memcpy(strip + row * stride, edge, stride * sizeof(*output));
}

inline void copy_slide_rows(uint16_t *destination, const uint16_t *source, int width, int rows) {
  std::memcpy(destination, source, static_cast<size_t>(width) * rows * sizeof(*destination));
}

inline void blit_slides(uint16_t *output, const uint16_t *left, const uint16_t *right,
                        int native_width, int native_height, int offset) {
  blit_slides(output, left, right, native_width, native_height, offset, copy_slide_rows);
}

inline void blit_slide_edge(uint16_t *output, const uint16_t *photo, int native_width, int native_height, int offset) {
  blit_slide_edge(output, photo, native_width, native_height, offset, copy_slide_rows);
}

}  // namespace espframe::native
