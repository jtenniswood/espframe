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
  static constexpr size_t atlas_size = 11 * glyph_width * glyph_height / 2;
  static constexpr int first_row = 45;
  static constexpr int first_column = 58;
  static constexpr int rows = 4 * digit_advance + colon_advance + 4;
  static constexpr int columns = glyph_height + 2;

  bool init(const ClockSettings &settings, const uint8_t *atlas, bool snapshots,
            ScreenRotation rotation = ScreenRotation::Normal);
  void set_twelve_hour(bool enabled) { twelve_hour_ = enabled; }
  bool update(const std::optional<std::tm> &local);
  void draw(uint16_t *frame, bool remember_underlay = false);
  void pin_underlay();
  void restore_rows(uint16_t *frame, int source_row, int target_row, int count) const;
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
  bool twelve_hour_{};
  bool latest_painted_{};
  bool pinned_painted_{};
};

}  // namespace espframe::native
