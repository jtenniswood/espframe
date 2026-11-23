#pragma once

#include "clock_settings.h"
#include "slide_geometry.h"

#include <array>
#include <ctime>

namespace espframe::native {

// The panel's native rows are landscape x coordinates. The clock is painted
// after the photo, so it keeps its place while the image underneath moves.
class ClockOverlay {
 public:
  ClockOverlay() = default;
  ~ClockOverlay();
  ClockOverlay(const ClockOverlay &) = delete;
  ClockOverlay &operator=(const ClockOverlay &) = delete;

  static constexpr int glyph_width = 88;
  static constexpr int glyph_height = 116;
  static constexpr int digit_advance = 86;
  static constexpr int colon_advance = 37;
  static constexpr int shadow_padding = 10;
  static constexpr int shadow_width = glyph_width + 2 * shadow_padding;
  static constexpr int shadow_height = glyph_height + 2 * shadow_padding;
  static constexpr size_t face_atlas_size = 11 * glyph_width * glyph_height / 2;
  static constexpr size_t atlas_size = face_atlas_size + 11 * shadow_width * shadow_height / 2;
  static constexpr int face_first_row = 45;
  static constexpr int face_top_column = 175;
  static constexpr int first_row = face_first_row - shadow_padding;
  static constexpr int first_column = face_top_column + shadow_padding - shadow_height + 1;
  static constexpr int rows = 4 * digit_advance + colon_advance + 2 * shadow_padding;
  static constexpr int columns = shadow_height;

  bool init(const ClockSettings &settings, const uint8_t *atlas, bool snapshots,
            ScreenRotation rotation = ScreenRotation::Normal);
  void set_rotation(ScreenRotation rotation);
  void set_twelve_hour(bool enabled) { twelve_hour_ = enabled; }
  bool set_shown(bool shown);
  bool update(const std::optional<std::tm> &local);
  bool visible() const { return shown_ && text_[0] != '\0'; }
  void draw(uint16_t *frame, bool remember_underlay = false, uint8_t opacity = 255);
  void pin_underlay();
  void restore_slide_slice(uint16_t *frame, int source, int target, int count) const;
  void restore_blend(uint16_t *frame, const uint16_t *target, uint8_t opacity) const;

 private:
  struct Underlays {
    std::array<uint16_t, rows * columns> latest;
    std::array<uint16_t, rows * columns> pinned;
  };

  const uint8_t *atlas_{};
  Underlays *underlays_{};
  std::array<char, 6> text_{};
  ScreenRotation rotation_{};
  int physical_row_{first_row};
  int physical_column_{first_column};
  int physical_rows_{rows};
  int physical_columns_{columns};
  bool twelve_hour_{};
  bool shown_{};
  bool latest_painted_{};
  bool pinned_painted_{};
};

}  // namespace espframe::native
