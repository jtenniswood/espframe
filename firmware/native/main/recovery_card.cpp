#include "recovery_card.h"

#include <algorithm>
#include <string_view>

namespace espframe::native {
namespace {

constexpr int glyph_width = 32;
constexpr int glyph_height = 36;
constexpr int glyph_bytes = 1 + glyph_width * glyph_height / 2;
constexpr uint16_t background = 0x1105;
constexpr uint16_t muted = 0xaeb5;
constexpr uint16_t white = 0xffff;

uint16_t masked(uint16_t color, unsigned opacity) {
  const auto channel = [opacity](unsigned from, unsigned to) { return (from * (15 - opacity) + to * opacity + 7) / 15; };
  return static_cast<uint16_t>((channel(background >> 11, color >> 11) << 11) |
      (channel((background >> 5) & 63, (color >> 5) & 63) << 5) | channel(background & 31, color & 31));
}

void text(uint16_t *frame, const uint8_t *atlas, ScreenRotation rotation, int x, int y,
          std::string_view value, uint16_t color) {
  const int step = -axis_direction(rotation);
  for (const unsigned char character : value) {
    const auto *glyph = atlas + (character - ' ') * glyph_bytes;
    const int advance = *glyph++;
    for (int column = 0; column < glyph_width; ++column) {
      auto *pixels = frame + panel_offset(x + column, panel_width - 1 - y, rotation);
      for (int row = 0; row < glyph_height; ++row) {
        const auto mask = glyph[row * (glyph_width / 2) + column / 2];
        const unsigned opacity = column % 2 ? mask & 15 : mask >> 4;
        if (opacity)
          pixels[row * step] = masked(color, opacity);
      }
    }
    x += advance;
  }
}

void card(uint16_t *frame, ScreenRotation rotation, int left, int top, int width, int height) {
  for (int column = left; column < left + width; ++column) {
    const int first = panel_offset(column, panel_width - top - height, rotation);
    const int last = panel_offset(column, panel_width - 1 - top, rotation);
    std::fill_n(frame + std::min(first, last), height, background);
  }
}

}  // namespace

void draw_recovery_card(uint16_t *frame, const uint8_t *atlas, const RecoveryNetwork &network, ScreenRotation rotation) {
  constexpr int left = 48;
  constexpr int width = 504;
  constexpr int height = 184;
  constexpr int top = panel_width - height - 48;
  constexpr int x = left + 24;
  constexpr int value = x + 139;
  card(frame, rotation, left, top, width, height);
  text(frame, atlas, rotation, x, top + 12, "Reconnect Wi-Fi", white);
  text(frame, atlas, rotation, x, top + 52, "Network", muted);
  text(frame, atlas, rotation, value, top + 52, network.ssid.data(), white);
  text(frame, atlas, rotation, x, top + 90, "Password", muted);
  text(frame, atlas, rotation, value, top + 90, network.password.data(), white);
  text(frame, atlas, rotation, x, top + 128, "Open", muted);
  text(frame, atlas, rotation, value, top + 128, recovery_url, white);
}

void draw_source_status(uint16_t *frame, const uint8_t *atlas, PhotoSourceStatus status, ScreenRotation rotation) {
  std::string_view message;
  switch (status) {
    case PhotoSourceStatus::WaitingForTime: message = "Waiting for the date"; break;
    case PhotoSourceStatus::MemoriesEmpty: message = "No On This Day photos"; break;
    case PhotoSourceStatus::MemoriesUnavailable: message = "Couldn't load On This Day"; break;
    case PhotoSourceStatus::IncompleteFilters: message = "Choose filter values"; break;
    case PhotoSourceStatus::UnsupportedFilters: message = "Filters need Immich 3.2"; break;
    case PhotoSourceStatus::FiltersUnavailable: message = "Check photo filters"; break;
    case PhotoSourceStatus::None: return;
  }
  card(frame, rotation, 48, 612, 396, 60);
  text(frame, atlas, rotation, 68, 622, message, white);
}

void draw_loading_status(uint16_t *frame, const uint8_t *atlas, int direction, ScreenRotation rotation) {
  constexpr int margin = 44;
  constexpr int width = 244;
  constexpr int height = 52;
  constexpr int top = slide_height - margin - height;
  const int left = direction < 0 ? margin : slide_width - margin - width;
  card(frame, rotation, left, top, width, height);
  text(frame, atlas, rotation, left + 16, top + 8, "Loading photo", white);
}

}  // namespace espframe::native
