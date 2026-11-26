#pragma once

#include "frame_slots.h"
#include "slide_geometry.h"
#if SWIPE_LINKED_SCANOUT
#include "scanout_frame.h"
#endif

#include <cstdint>
#include <optional>

#include "esp_lcd_mipi_dsi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace espframe::native {

struct ScanoutFrame;

class Display {
 public:
  void init(bool request_snapshot = false);
  void init_backlight(uint8_t brightness);
  void set_backlight(uint8_t brightness) const;
  static unsigned nominal_frame_period_us();
  std::optional<int64_t> poll_presented();
  void wait_for_frame_or_tick() const;
  PixelPosition presented_position() const { return position_; }
  // A queued frame can still reach the panel after touch arrives. Start
  // from that frame so taking over doesn't visibly undo the pending swap.
  PixelPosition handoff_position() const { return slots_.queued() ? pending_position_ : position_; }
  uint16_t *writable_buffer();
  uint16_t *preparation_buffer();
  uint16_t *reserve_preparation_buffer();
  void release_preparation_buffer(const uint16_t *buffer);
  bool submit(PixelPosition position, const ScanoutFrame *frame = nullptr, std::optional<SlideRange> sources = {},
              const uint16_t *prepared = nullptr);
  bool borrowing() const {
#if SWIPE_LINKED_SCANOUT
    return sources_.active();
#else
    return false;
#endif
  }
  SlideRange retain_sources(SlideRange range) const {
#if SWIPE_LINKED_SCANOUT
    return sources_.retain(range);
#else
    return range;
#endif
  }
  bool can_pin_frame() const { return slots_.can_pin(); }
  const uint16_t *pin_handoff();
  void unpin() { slots_.unpin(); }

 private:
  static bool frame_done_(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *event, void *context);

  esp_lcd_panel_handle_t panel_{};
  uint16_t *buffers_[3]{};
  FrameSlots slots_;
  TaskHandle_t renderer_{};
  portMUX_TYPE boundary_lock_ = portMUX_INITIALIZER_UNLOCKED;
  int64_t latched_us_{};
#if SWIPE_LINKED_SCANOUT
  unsigned awaiting_index_{FrameSlots::none};
  ScanoutSources sources_;
#else
  bool awaiting_latch_{};
#endif
  PixelPosition pending_position_{};
  PixelPosition position_{-1};
};

}  // namespace espframe::native
