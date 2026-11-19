#pragma once

#include <atomic>
#include <cstdint>

#include "esp_lcd_mipi_dsi.h"

namespace espframe::native {

inline constexpr int panel_width = 800;
inline constexpr int panel_height = 1280;
inline constexpr int slide_width = panel_height;
inline constexpr int slide_height = panel_width;
inline constexpr int pixel_count = panel_width * panel_height;

class Display {
 public:
  void init();
  void enable_backlight() const;
  bool poll_presented();
  int presented_position() const { return position_; }
  uint16_t *writable_buffer();
  void submit(int position);

 private:
  static bool frame_done_(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *event, void *context);

  esp_lcd_panel_handle_t panel_{};
  uint16_t *buffers_[2]{};
  std::atomic<uint32_t> frame_count_{};
  uint32_t submitted_at_{};
  unsigned front_{};
  int pending_position_{};
  int position_{-1};
  bool pending_{};
};

}  // namespace espframe::native
