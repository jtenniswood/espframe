#pragma once

#include "slide_geometry.h"

#include <atomic>
#include <cstdint>

#include "esp_lcd_mipi_dsi.h"

namespace espframe::native {

class Display {
 public:
  void init();
  void enable_backlight() const;
  bool poll_presented();
  PixelPosition presented_position() const { return position_; }
  uint16_t *writable_buffer();
  void submit(PixelPosition position);

 private:
  static bool frame_done_(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *event, void *context);

  esp_lcd_panel_handle_t panel_{};
  uint16_t *buffers_[2]{};
  std::atomic<uint32_t> frame_count_{};
  uint32_t submitted_at_{};
  unsigned front_{};
  PixelPosition pending_position_{};
  PixelPosition position_{-1};
  bool pending_{};
};

}  // namespace espframe::native
