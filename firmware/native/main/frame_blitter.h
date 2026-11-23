#pragma once

#include "slide_blit.h"

#include "esp_async_color_convert.h"

namespace espframe::native {

// The player owns the input slides and the writable display buffer until
// each DMA2D copy finishes. Overlays are painted on the finished output.
class FrameBlitter {
 public:
  static constexpr unsigned burst_bytes = 128;

  void init();
  void slides(uint16_t *output, SlideSpan span, bool across_columns = false);
  void copy_panel_rows(uint16_t *output, const uint16_t *source, int rows) {
    copy_rows_(output, source, panel_width, rows);
  }
  void edge(uint16_t *output, const uint16_t *photo, int pull, bool across_columns = false);
  void push(uint16_t *output, const uint16_t *from, const uint16_t *to, int dx, int dy, uint8_t progress,
            ScreenRotation rotation);

 private:
  bool copy_split_(uint16_t *output, SlideSpan span);
  void copy_rows_(uint16_t *destination, const uint16_t *source, int width, int rows);
  void copy_block_(uint16_t *destination, const uint16_t *source, int stride, int rows,
                   int dest_x, int source_x, int width);
  void disable_dma_(esp_err_t result);

  async_color_convert_handle_t dma_{};
};

}  // namespace espframe::native
