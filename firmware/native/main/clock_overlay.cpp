#include "clock_overlay.h"

#include "esp_heap_caps.h"

#include <algorithm>
#include <cstring>

namespace espframe::native {
namespace {

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
    const unsigned opacity = alpha * 17;
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
  atlas_ = atlas;
  shown_ = settings.show;
  twelve_hour_ = settings.twelve_hour();
  set_rotation(rotation);
  if (snapshots)
    underlays_ = static_cast<Underlays *>(heap_caps_malloc(sizeof(Underlays), MALLOC_CAP_SPIRAM));
  return !snapshots || underlays_ != nullptr;
}

bool ClockOverlay::set_shown(bool shown) {
  if (shown == shown_)
    return false;
  shown_ = shown;
  return true;
}

void ClockOverlay::set_rotation(ScreenRotation rotation) {
  rotation_ = rotation;
  latest_painted_ = pinned_painted_ = false;
  const Viewport viewport{rotation};
  const int top = viewport.height - first_column - columns;
  const int first = viewport.offset(first_row + (viewport.x_stride < 0 ? rows - 1 : 0),
                                    top + (viewport.y_stride < 0 ? columns - 1 : 0));
  physical_row_ = first / panel_width;
  physical_column_ = first % panel_width;
  physical_rows_ = viewport.portrait ? columns : rows;
  physical_columns_ = viewport.portrait ? rows : columns;
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
  return shown_;
}

void ClockOverlay::draw(uint16_t *frame, bool remember_underlay, uint8_t opacity) {
  const bool painted = visible() && opacity != 0;
  if (remember_underlay) {
    latest_painted_ = painted && underlays_;
    if (latest_painted_) {
      for (int row = 0; row < physical_rows_; ++row)
        std::memcpy(underlays_->latest.data() + row * physical_columns_,
                    frame + (physical_row_ + row) * panel_width + physical_column_, physical_columns_ * sizeof(*frame));
    }
  }
  if (!painted)
    return;
  const Viewport viewport{rotation_};
  for (const bool shadow : {true, false}) {
    const auto &palette = shadow ? shadow_blends : face_blends;
    std::array<const GlyphBlend *, 16> blends;
    for (unsigned alpha = 0; alpha < blends.size(); ++alpha)
      blends[alpha] = &palette[(alpha * opacity + 127) / 255];
    const bool opaque_face = !shadow && opacity == 255;
    const int padding = shadow ? shadow_padding : 0;
    const int height = shadow ? shadow_height : glyph_height;
    const int width = shadow ? shadow_width : glyph_width;
    const int stride = height / 2;
    const auto *bitmap = atlas_ + (shadow ? face_atlas_size : 0);
    const int top = viewport.height - 1 - face_top_column - padding;
    int left = face_first_row - padding;
    for (const char ch : text_) {
      if (ch == '\0')
        break;
      if (ch == ' ')
        continue;
      const int digit = ch == ':' ? 10 : ch - '0';
      const auto *mask = bitmap + digit * width * stride;
      const int advance = ch == ':' ? colon_advance : digit_advance;
      for (int x = 0; x < advance + 2 * padding; ++x) {
        auto *column = frame + viewport.offset(left + x, top);
        const auto *ink = mask + x * stride;
        for (int pair = 0; pair < stride; ++pair) {
          const auto packed = ink[pair];
          if (packed == 0)
            continue;
          for (int half = 0; half < 2; ++half) {
            const unsigned alpha = half == 0 ? packed >> 4 : packed & 15;
            if (alpha == 0)
              continue;
            auto &pixel = column[(2 * pair + half) * viewport.y_stride];
            pixel = opaque_face && alpha == 15 ? 0xffff : blends[alpha]->apply(pixel);
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

void ClockOverlay::restore_slide_slice(uint16_t *frame, int source, int target, int count) const {
  if (!pinned_painted_)
    return;
  if (portrait_screen(rotation_)) {
    const int begin = std::max(source, physical_column_);
    const int end = std::min(source + count, physical_column_ + physical_columns_);
    if (begin >= end)
      return;
    for (int row = 0; row < physical_rows_; ++row)
      std::memcpy(frame + (physical_row_ + row) * panel_width + target + begin - source,
                  underlays_->pinned.data() + row * physical_columns_ + begin - physical_column_,
                  (end - begin) * sizeof(*frame));
  } else {
    const int end = std::min(source + count, physical_row_ + physical_rows_);
    for (int row = std::max(source, physical_row_); row < end; ++row)
      std::memcpy(frame + (target + row - source) * panel_width + physical_column_,
                  underlays_->pinned.data() + (row - physical_row_) * physical_columns_, physical_columns_ * sizeof(*frame));
  }
}

void ClockOverlay::restore_blend(uint16_t *frame, const uint16_t *target, uint8_t opacity) const {
  if (!pinned_painted_)
    return;
  for (int row = 0; row < physical_rows_; ++row) {
    const auto offset = (physical_row_ + row) * panel_width + physical_column_;
    for (int column = 0; column < physical_columns_; ++column)
      frame[offset + column] = mix_rgb565(underlays_->pinned[row * physical_columns_ + column], target[offset + column], opacity);
  }
}

}  // namespace espframe::native
