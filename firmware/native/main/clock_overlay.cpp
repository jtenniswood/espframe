#include "clock_overlay.h"

#include "esp_heap_caps.h"

#include <algorithm>
#include <cstring>

namespace espframe::native {
namespace {

constexpr int shadow = 2;

constexpr unsigned mix_channel(unsigned from, unsigned to, unsigned alpha) {
  return (from * (255 - alpha) + to * alpha + 127) / 255;
}

uint16_t mix_rgb565(uint16_t from, uint16_t to, unsigned alpha) {
  return static_cast<uint16_t>((mix_channel(from >> 11, to >> 11, alpha) << 11) |
      (mix_channel((from >> 5) & 63, (to >> 5) & 63, alpha) << 5) | mix_channel(from & 31, to & 31, alpha));
}

struct GlyphBlend {
  std::array<uint8_t, 32> red_and_blue;
  std::array<uint8_t, 64> green;

  uint16_t apply(uint16_t from) const {
    return static_cast<uint16_t>((red_and_blue[from >> 11] << 11) |
        (green[(from >> 5) & 63] << 5) | red_and_blue[from & 31]);
  }
};

constexpr auto glyph_blends(bool shadow_layer) {
  std::array<GlyphBlend, 16> blends{};
  for (unsigned alpha = 0; alpha < blends.size(); ++alpha) {
    const unsigned opacity = shadow_layer ? (alpha * 17 + 2) / 5 : alpha * 17;
    auto &blend = blends[alpha];
    for (unsigned c = 0; c < blend.red_and_blue.size(); ++c)
      blend.red_and_blue[c] = mix_channel(c, shadow_layer ? 0 : 31, opacity);
    for (unsigned c = 0; c < blend.green.size(); ++c)
      blend.green[c] = mix_channel(c, shadow_layer ? 0 : 63, opacity);
  }
  return blends;
}

// The atlas has 16 coverage levels, all blended against white or black.
// Keep blend arithmetic out of the per-frame clock draw during a swipe.
constexpr auto face_blends = glyph_blends(false);
constexpr auto shadow_blends = glyph_blends(true);

}  // namespace

ClockOverlay::~ClockOverlay() { heap_caps_free(underlays_); }

bool ClockOverlay::init(const ClockSettings &settings, const uint8_t *atlas, bool snapshots,
                        ScreenRotation rotation) {
  if (!settings.show)
    return true;
  atlas_ = atlas;
  twelve_hour_ = settings.twelve_hour();
  rotation_ = rotation;
  const bool upside_down = rotation == ScreenRotation::UpsideDown;
  physical_row_ = upside_down ? panel_height - first_row - rows : first_row;
  physical_column_ = upside_down ? panel_width - first_column - columns : first_column;
  if (snapshots)
    underlays_ = static_cast<Underlays *>(heap_caps_malloc(sizeof(Underlays), MALLOC_CAP_SPIRAM));
  return !snapshots || underlays_ != nullptr;
}

bool ClockOverlay::update(const std::optional<std::tm> &local) {
  if (!atlas_)
    return false;
  std::array<char, 6> next{};
  if (local) {
    int hour = local->tm_hour;
    if (twelve_hour_)
      hour = (hour + 11) % 12 + 1;
    next = {twelve_hour_ && hour < 10 ? ' ' : static_cast<char>('0' + hour / 10),
            static_cast<char>('0' + hour % 10), ':', static_cast<char>('0' + local->tm_min / 10),
            static_cast<char>('0' + local->tm_min % 10), '\0'};
  }
  if (next == text_)
    return false;
  text_ = next;
  return true;
}

void ClockOverlay::draw(uint16_t *frame, bool remember_underlay) {
  if (remember_underlay) {
    latest_painted_ = text_[0] != '\0' && underlays_;
    if (latest_painted_) {
      for (int row = 0; row < rows; ++row)
        std::memcpy(underlays_->latest.data() + row * columns,
                    frame + (physical_row_ + row) * panel_width + physical_column_, columns * sizeof(*frame));
    }
  }
  if (text_[0] == '\0')
    return;
  const int column_step = -axis_direction(rotation_);
  for (int layer : {shadow, 0}) {
    const auto &blends = layer == 0 ? face_blends : shadow_blends;
    int left = first_row + layer;
    for (const char ch : text_) {
      if (ch == '\0')
        break;
      if (ch == ' ')
        continue;
      const int digit = ch == ':' ? 10 : ch - '0';
      const auto *mask = atlas_ + digit * glyph_width * glyph_height / 2;
      const int advance = ch == ':' ? colon_advance : digit_advance;
      for (int x = 0; x < advance; ++x) {
        auto *column = frame + panel_offset(left + x, first_column + columns - 1 - layer, rotation_);
        const auto *ink = mask + x * (glyph_height / 2);
        for (int pair = 0; pair < glyph_height / 2; ++pair) {
          const auto packed = ink[pair];
          if (packed == 0)
            continue;
          for (int half = 0; half < 2; ++half) {
            const unsigned alpha = half == 0 ? packed >> 4 : packed & 15;
            if (alpha == 0)
              continue;
            auto &pixel = column[(2 * pair + half) * column_step];
            pixel = layer == 0 && alpha == 15 ? 0xffff : blends[alpha].apply(pixel);
          }
        }
      }
      left += advance;
    }
  }
}

void ClockOverlay::pin_underlay() {
  pinned_painted_ = latest_painted_;
  if (pinned_painted_)
    underlays_->pinned = underlays_->latest;
}

void ClockOverlay::restore_rows(uint16_t *frame, int source_row, int target_row, int count) const {
  if (!pinned_painted_)
    return;
  const int end = std::min(source_row + count, physical_row_ + rows);
  for (int row = std::max(source_row, physical_row_); row < end; ++row)
    std::memcpy(frame + (target_row + row - source_row) * panel_width + physical_column_,
                underlays_->pinned.data() + (row - physical_row_) * columns, columns * sizeof(*frame));
}

void ClockOverlay::restore_blend(uint16_t *frame, const uint16_t *target, uint8_t opacity) const {
  if (!pinned_painted_)
    return;
  for (int row = 0; row < rows; ++row) {
    const auto offset = (physical_row_ + row) * panel_width + physical_column_;
    for (int column = 0; column < columns; ++column)
      frame[offset + column] = mix_rgb565(underlays_->pinned[row * columns + column], target[offset + column], opacity);
  }
}

}  // namespace espframe::native
