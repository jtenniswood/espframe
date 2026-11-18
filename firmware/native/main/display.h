#pragma once

#include "frame_slots.h"
#include "slide_geometry.h"

#include <cstdint>
#include <optional>

#include "esp_lcd_mipi_dsi.h"
#include "freertos/FreeRTOS.h"

namespace espframe::native {

class Display {
 public:
  void init(bool request_snapshot = false);
  void enable_backlight() const;
  std::optional<int64_t> poll_presented();
  PixelPosition presented_position() const { return position_; }
  // A queued frame can still reach the panel after touch arrives. Start
  // from that frame so taking over doesn't visibly undo the pending swap.
  PixelPosition handoff_position() const { return slots_.queued() ? pending_position_ : position_; }
  uint16_t *writable_buffer();
  void submit(PixelPosition position);
  bool can_pin_frame() const { return slots_.can_pin(); }
  const uint16_t *pin_handoff();
  void unpin() { slots_.unpin(); }

 private:
  static bool frame_done_(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *event, void *context);

  esp_lcd_panel_handle_t panel_{};
  uint16_t *buffers_[3]{};
  FrameSlots slots_;
  portMUX_TYPE boundary_lock_ = portMUX_INITIALIZER_UNLOCKED;
  int64_t latched_us_{};
  bool awaiting_latch_{};
  PixelPosition pending_position_{};
  PixelPosition position_{-1};
};

}  // namespace espframe::native
