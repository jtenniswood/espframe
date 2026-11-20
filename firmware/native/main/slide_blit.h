#pragma once

#include "slide_geometry.h"

#include <algorithm>
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
inline SlideSpan orient_slide_span(const uint16_t *left, const uint16_t *right, int native_height,
                                   int offset, ScreenRotation rotation) {
  if (rotation == ScreenRotation::UpsideDown && offset != 0)
    return {right, left, native_height - offset};
  return {left, right, offset};
}

// The panel scans landscape x along its native rows after a 90-degree
// rotation. Adjacent clipped slides therefore need at most two contiguous
// copies. The cache stores each slide in that same native order.
template <class CopyRows>
void blit_slides(uint16_t *output, const uint16_t *left, const uint16_t *right,
                 int native_width, int native_height, int offset, CopyRows copy_rows, uint16_t blank = 0) {
  const size_t stride = static_cast<size_t>(native_width);
  const int first_rows = native_height - offset;
  if (left)
    copy_rows(output, left + offset * stride, native_width, first_rows);
  else
    std::fill_n(output, first_rows * stride, blank);
  if (offset != 0) {
    auto *tail = output + first_rows * stride;
    if (right)
      copy_rows(tail, right, native_width, offset);
    else
      std::fill_n(tail, offset * stride, blank);
  }
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
