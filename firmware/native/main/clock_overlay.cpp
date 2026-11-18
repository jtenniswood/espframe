#include "clock_overlay.h"

#include "esp_heap_caps.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace espframe::native {
namespace {

constexpr int shadow = 2;

uint16_t mix_rgb565(uint16_t from, uint16_t to, unsigned alpha) {
  const auto channel = [alpha](unsigned a, unsigned b) { return (a * (255 - alpha) + b * alpha + 127) / 255; };
  return static_cast<uint16_t>((channel(from >> 11, to >> 11) << 11) |
      (channel((from >> 5) & 63, (to >> 5) & 63) << 5) | channel(from & 31, to & 31));
}

}  // namespace

ClockOverlay::~ClockOverlay() { heap_caps_free(underlays_); }

bool ClockOverlay::init(const ClockSettings &settings, const uint8_t *atlas, bool snapshots) {
  if (!settings.show)
    return true;
  const char *timezone = clock_timezone(settings.timezone);
  if (!timezone || setenv("TZ", timezone, 1) != 0)
    return true;
  tzset();
  atlas_ = atlas;
  twelve_hour_ = settings.format == "12 Hour";
  if (snapshots)
    underlays_ = static_cast<Underlays *>(heap_caps_malloc(sizeof(Underlays), MALLOC_CAP_SPIRAM));
  return !snapshots || underlays_ != nullptr;
}

bool ClockOverlay::update(std::time_t now) {
  if (!atlas_)
    return false;
  std::array<char, 6> next{};
  std::tm local{};
  if (now >= 1'690'000'000 && localtime_r(&now, &local)) {
    int hour = local.tm_hour;
    if (twelve_hour_)
      hour = (hour + 11) % 12 + 1;
    next = {twelve_hour_ && hour < 10 ? ' ' : static_cast<char>('0' + hour / 10),
            static_cast<char>('0' + hour % 10), ':', static_cast<char>('0' + local.tm_min / 10),
            static_cast<char>('0' + local.tm_min % 10), '\0'};
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
                    frame + (first_row + row) * panel_width + first_column, columns * sizeof(*frame));
    }
  }
  if (text_[0] == '\0')
    return;
  for (int layer : {shadow, 0}) {
    const uint16_t color = layer == 0 ? 0xffff : 0;
    int left = first_row + layer;
    for (const char ch : text_) {
      if (ch == '\0')
        break;
      if (ch == ' ')
        continue;
      const int digit = ch == ':' ? 10 : ch - '0';
      const auto *mask = atlas_ + digit * glyph_width * glyph_height / 2;
      const int advance = ch == ':' ? glyph_width / 2 : glyph_width;
      for (int x = 0; x < advance; ++x) {
        auto *column = frame + (left + x) * panel_width + first_column + columns - 1 - layer;
        for (int y = 0; y < glyph_height; ++y) {
          const auto packed = mask[y * (glyph_width / 2) + x / 2];
          const unsigned opacity = (x % 2 ? packed & 15 : packed >> 4) * (layer == 0 ? 17 : 11);
          if (opacity)
            column[-y] = mix_rgb565(column[-y], color, opacity);
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
  const int end = std::min(source_row + count, first_row + rows);
  for (int row = std::max(source_row, first_row); row < end; ++row)
    std::memcpy(frame + (target_row + row - source_row) * panel_width + first_column,
                underlays_->pinned.data() + (row - first_row) * columns, columns * sizeof(*frame));
}

void ClockOverlay::restore_blend(uint16_t *frame, const uint16_t *target, uint8_t opacity) const {
  if (!pinned_painted_)
    return;
  for (int row = 0; row < rows; ++row) {
    const auto offset = (first_row + row) * panel_width + first_column;
    for (int column = 0; column < columns; ++column)
      frame[offset + column] = mix_rgb565(underlays_->pinned[row * columns + column], target[offset + column], opacity);
  }
}

}  // namespace espframe::native
