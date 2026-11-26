#pragma once

#include "slide_blit.h"

#include <array>

namespace espframe::native {

class PhotoControls {
 public:
  static constexpr int open = 1024;
  static constexpr int toolbar_bottom = 96;
  static constexpr int cached_steps = 11;
  static constexpr int64_t resize_duration_us = 200'000;

  static constexpr bool supports_cached_steps(int64_t refresh_us) {
    return refresh_us > 0 && (resize_duration_us + refresh_us - 1) / refresh_us == cached_steps;
  }

  static constexpr int cached_progress(int step) {
    constexpr auto denominator = cached_steps * cached_steps * cached_steps;
    return (open * step * step * (3 * cached_steps - 2 * step) + denominator / 2) / denominator;
  }

  static constexpr int cached_step(int progress) {
    for (int step = 1; step <= cached_steps; ++step) {
      if (cached_progress(step) == progress)
        return step;
    }
    return 0;
  }
  static constexpr uint8_t opaque = 255;

  struct Frame {
    int progress;
    int64_t elapsed_us;
  };

  Frame next_frame(int64_t now, int64_t refresh_us, bool clip_ready = false) const;
  void submitted(Frame frame);
  void presented(int64_t now);
  uint8_t text_opacity(int64_t now) const;
  bool animating() const { return this->submitted_progress_ != this->target_; }
  bool target_open() const { return this->target_ == open; }
  void animate(bool show, int64_t now, bool text_visible = true);
  void reset() {
    this->start_ = this->target_ = 0;
    this->text_from_ = opaque;
    this->restore_text_ = false;
  }

  // Prepare once, then draw disjoint panel rows before painting the toolbar.
  void prepare(uint16_t *output, SlideSpan span, int pull, ScreenRotation rotation, int progress);
  int split_row() const { return this->first_row_ + this->rows_ / 2; }
  void draw_rows(int first, int end) const;
  void draw_toolbar() const;

 private:
  int64_t text_duration() const;

  int start_{};
  int target_{};
  int64_t started_us_{};
  int64_t duration_us_{};
  int64_t submitted_elapsed_us_{};
  int submitted_progress_{};
  int64_t text_started_us_{};
  uint8_t text_from_{opaque};
  bool restore_text_{};
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
  uint32_t column_step_{};
  uint32_t column_origin_{};
  static constexpr int radius = 24;
  std::array<int, radius> corners_{};
  std::array<uint16_t, panel_width> source_columns_{};
};

}  // namespace espframe::native
