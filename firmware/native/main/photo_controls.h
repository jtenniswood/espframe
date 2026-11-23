#pragma once

#include "slide_blit.h"

#include <array>

namespace espframe::native {

class PhotoControls {
 public:
  static constexpr int open = 256;
  static constexpr int toolbar_bottom = 96;
  static constexpr uint8_t opaque = 255;

  int progress(int64_t now) const;
  uint8_t text_opacity(int64_t now) const;
  bool animating(int progress) const { return progress != this->target_; }
  bool target_open() const { return this->target_ == open; }
  void animate(bool show, int64_t now, bool text_visible = true);
  void reset() {
    this->start_ = this->target_ = 0;
    this->text_from_ = opaque;
  }

  // Prepare once, then draw disjoint panel rows before painting the toolbar.
  void prepare(uint16_t *output, SlideSpan span, int pull, ScreenRotation rotation, int progress);
  void draw_rows(int first, int end) const;
  void draw_toolbar() const;

 private:
  int64_t text_duration() const;

  int start_{};
  int target_{};
  int64_t started_us_{};
  int64_t duration_us_{};
  int64_t text_started_us_{};
  uint8_t text_from_{opaque};
  uint16_t *output_{};
  SlideSpan span_{};
  int pull_{};
  ScreenRotation rotation_{};
  int progress_{};
  int first_row_{};
  int first_column_{};
  int rows_{};
  int columns_{};
  int source_split_{};
  int rounding_{};
  static constexpr int radius = 24;
  std::array<int, radius> corners_{};
  std::array<uint16_t, panel_width> source_columns_{};
};

}  // namespace espframe::native
