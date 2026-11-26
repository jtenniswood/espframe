#include "recovery_card.h"
#include "i18n_generated.h"

#include <algorithm>
#include <string_view>

namespace espframe::native {
namespace {

constexpr int glyph_width = 32;
constexpr int glyph_height = 36;
constexpr int glyph_bytes = 1 + glyph_width * glyph_height / 2;
constexpr uint16_t compact_mask = 0x8000;
constexpr uint16_t background = 0x1105;
constexpr uint16_t muted = 0xaeb5;
constexpr uint16_t white = 0xffff;
constexpr uint16_t setup_background = 0x0000;
constexpr uint16_t setup_muted = 0x8c51;

enum class StatusLabel {
#define ESPFRAME_STATUS_LABEL(name) name,
#include "status_labels.def"
#undef ESPFRAME_STATUS_LABEL
  Count
};

struct Line {
  int width{};
  int height{};
  std::span<const uint8_t> runs;
  bool compact{};
};

bool rtl(StatusText text) {
  return text.language < espframe_i18n_catalogue::LANGUAGE_COUNT &&
         std::string_view{espframe_i18n_catalogue::LANGUAGES[text.language]} == "he";
}

constexpr size_t label_count = static_cast<size_t>(StatusLabel::Count);
constexpr size_t icon_index = label_count * espframe_i18n_catalogue::LANGUAGE_COUNT;
constexpr size_t ssid_glyph_start = icon_index + 1;
constexpr std::string_view ssid_characters = recovery_ssid_characters;
constexpr int ssid_glyph_padding = 2;

Line line_at(StatusText text, size_t index) {
  constexpr size_t fingerprint_bytes = 32;
  constexpr size_t line_count = ssid_glyph_start + ssid_characters.size();
  constexpr size_t table_size = fingerprint_bytes + 4 * (line_count + 1);
  if (text.masks.size() < table_size || index >= line_count)
    return {};
  const auto offset = [&](size_t index) -> size_t {
    const auto *bytes = text.masks.data() + fingerprint_bytes + index * 4;
    return uint32_t{bytes[0]} | (uint32_t{bytes[1]} << 8) | (uint32_t{bytes[2]} << 16) | (uint32_t{bytes[3]} << 24);
  };
  const size_t begin = offset(index);
  const size_t end = offset(index + 1);
  if (begin < table_size || end <= begin || end - begin <= 3 || end > text.masks.size())
    return {};
  const auto header = text.masks[begin] | (text.masks[begin + 1] << 8);
  const int width = header & ~compact_mask;
  const int height = text.masks[begin + 2];
  const int max_width = height == glyph_height ? 668 : 700;
  if (width <= 0 || width > max_width || height <= 0 || height > 64)
    return {};
  return {width, height, text.masks.subspan(begin + 3, end - begin - 3), bool(header & compact_mask)};
}

Line line(StatusText text, StatusLabel label) {
  const size_t language = text.language < espframe_i18n_catalogue::LANGUAGE_COUNT ? text.language : 0;
  return line_at(text, language * label_count + static_cast<size_t>(label));
}

uint16_t masked(uint16_t color, unsigned opacity, uint16_t backdrop) {
  const auto channel = [opacity](unsigned from, unsigned to) { return (from * (15 - opacity) + to * opacity + 7) / 15; };
  return static_cast<uint16_t>((channel(backdrop >> 11, color >> 11) << 11) |
      (channel((backdrop >> 5) & 63, (color >> 5) & 63) << 5) | channel(backdrop & 31, color & 31));
}

const uint8_t *ascii_glyph(const uint8_t *atlas, unsigned char character) {
  const int code = character >= ' ' && character <= '~' ? character : '?';
  return atlas + (code - ' ') * glyph_bytes;
}

int ascii_width(const uint8_t *atlas, std::string_view value) {
  int width = 0;
  for (const unsigned char character : value)
    width += *ascii_glyph(atlas, character);
  return width;
}

void ascii_text(uint16_t *frame, const uint8_t *atlas, ScreenRotation rotation, int x, int y, std::string_view value,
                uint16_t color = white, uint16_t backdrop = background) {
  const Viewport viewport{rotation};
  for (const unsigned char character : value) {
    const auto *glyph = ascii_glyph(atlas, character);
    const int advance = *glyph++;
    for (int column = 0; column < std::min(glyph_width, viewport.width - x); ++column) {
      auto *pixels = frame + viewport.offset(x + column, y);
      for (int row = 0; row < glyph_height; ++row) {
        const auto mask = glyph[row * (glyph_width / 2) + column / 2];
        const unsigned opacity = column % 2 ? mask & 15 : mask >> 4;
        if (opacity)
          pixels[row * viewport.y_stride] = masked(color, opacity, backdrop);
      }
    }
    x += advance;
  }
}

void localized_text(uint16_t *frame, Line line, ScreenRotation rotation, int x, int y, uint16_t color,
                    uint16_t backdrop = background) {
  const Viewport viewport{rotation};
  if (line.width <= 0 || x < 0 || x + line.width > viewport.width || y < 0 || y + line.height > viewport.height)
    return;
  size_t pixel = 0;
  const size_t pixel_count = line.width * line.height;
  for (const uint8_t run : line.runs) {
    const unsigned opacity = line.compact ? (run >> 6) * 5 : run >> 4;
    const size_t end = std::min(pixel + (run & (line.compact ? 63 : 15)) + 1, pixel_count);
    if (opacity == 0) {
      pixel = end;
      continue;
    }
    const uint16_t value = masked(color, opacity, backdrop);
    for (; pixel < end; ++pixel) {
      const int column = pixel % line.width;
      const int row = pixel / line.width;
      frame[viewport.offset(x + column, y + row)] = value;
    }
  }
}

void centered_setup_ssid(uint16_t *frame, StatusText text, ScreenRotation rotation,
                         std::string_view ssid, int y) {
  const auto glyph = [&](char character) {
    const auto index = ssid_characters.find(character);
    return index == std::string_view::npos ? Line{} : line_at(text, ssid_glyph_start + index);
  };
  int width = 0;
  for (const char character : ssid)
    width += std::max(0, glyph(character).width - 2 * ssid_glyph_padding);
  int x = (Viewport{rotation}.width - width) / 2;
  for (const char character : ssid) {
    const auto mask = glyph(character);
    localized_text(frame, mask, rotation, x - ssid_glyph_padding, y, setup_muted, setup_background);
    x += std::max(0, mask.width - 2 * ssid_glyph_padding);
  }
}

void card(uint16_t *frame, ScreenRotation rotation, int left, int top, int width, int height) {
  const Viewport viewport{rotation};
  if (viewport.portrait) {
    const int x = viewport.x_stride < 0 ? left + width - 1 : left;
    for (int row = top; row < top + height; ++row)
      std::fill_n(frame + viewport.offset(x, row), width, background);
    return;
  }
  for (int column = left; column < left + width; ++column) {
    const int first = viewport.offset(column, top);
    const int last = viewport.offset(column, top + height - 1);
    std::fill_n(frame + std::min(first, last), height, background);
  }
}

void source_notice(uint16_t *frame, StatusText text, StatusLabel key, ScreenRotation rotation) {
  const auto message = line(text, key);
  if (message.width == 0)
    return;
  constexpr int left = 48;
  const int top = Viewport{rotation}.height - 188;
  constexpr int inset = 20;
  const int width = std::max(396, message.width + 2 * inset);
  card(frame, rotation, left, top, width, 60);
  const int x = rtl(text) ? left + width - inset - message.width : left + inset;
  localized_text(frame, message, rotation, x, top + 10, white);
}

}  // namespace

void draw_wifi_setup(uint16_t *frame, const uint8_t *atlas, StatusText text, const RecoveryNetwork &network,
                     std::string_view connected_address, ScreenRotation rotation) {
  const Viewport viewport{rotation};
  const int center = viewport.height / 2;
  std::fill_n(frame, pixel_count, setup_background);
  const auto centered = [&](Line message, int y, uint16_t color) {
    localized_text(frame, message, rotation, (viewport.width - message.width) / 2, y, color, setup_background);
  };
  const auto value = [&](std::string_view content, int y, uint16_t color) {
    ascii_text(frame, atlas, rotation, (viewport.width - ascii_width(atlas, content)) / 2, y, content, color,
               setup_background);
  };
  if (!connected_address.empty()) {
    centered(line(text, StatusLabel::connected), center - 58, white);
    value(connected_address, center + 18, white);
    return;
  }
  // Match the original LVGL stack: 64px Wi-Fi icon, 46px title, 30px body,
  // with a 20px gap between each of the three widgets.
  const auto icon = line_at(text, icon_index);
  const auto title = line(text, StatusLabel::wifi_setup_title);
  const auto installer = line(text, StatusLabel::wifi_setup_installer);
  const auto hotspot = line(text, StatusLabel::wifi_setup_hotspot);
  const auto browser = line(text, StatusLabel::wifi_setup_browser);
  constexpr int gap = 20;
  const int body_height = 2 * installer.height + hotspot.height + browser.height;
  const int top = center - (icon.height + title.height + body_height + 2 * gap) / 2;
  centered(icon, top, white);
  const int title_y = top + icon.height + gap;
  centered(title, title_y, white);
  const int body_y = title_y + title.height + gap;
  centered(installer, body_y, setup_muted);
  centered(hotspot, body_y + installer.height, setup_muted);
  const int ssid_y = body_y + installer.height + hotspot.height;
  centered_setup_ssid(frame, text, rotation, network.ssid.data(), ssid_y);
  centered(browser, ssid_y + installer.height, setup_muted);
}

void draw_recovery_card(uint16_t *frame, const uint8_t *atlas, StatusText text, const RecoveryNetwork &network,
                        ScreenRotation rotation) {
  constexpr int left = 48;
  constexpr int inset = 24;
  const Viewport viewport{rotation};
  const auto title = line(text, StatusLabel::wifi_setup);
  const Line labels[]{line(text, StatusLabel::wifi_network), line(text, StatusLabel::wifi_password),
                      line(text, StatusLabel::recovery_address)};
  const std::string_view values[]{network.ssid.data(), network.password.data(), recovery_url};
  const int label_width = std::max({labels[0].width, labels[1].width, labels[2].width});
  const int value_width = std::max({ascii_width(atlas, values[0]), ascii_width(atlas, values[1]), ascii_width(atlas, values[2])});
  const int label_column = std::max(139, label_width + 22);
  const bool stacked = viewport.portrait;
  const int height = stacked ? 296 : 184;
  const int top = viewport.height - height - 48;
  const int content_width = stacked ? std::max({title.width, label_width, value_width})
                                    : std::max(title.width, label_column + value_width);
  const int width = std::min(viewport.width - left, std::max(504, content_width + 2 * inset));
  const bool right_to_left = rtl(text);
  const int end = left + width - inset;
  card(frame, rotation, left, top, width, height);
  localized_text(frame, title, rotation, right_to_left ? end - title.width : left + inset, top + 12, white);
  for (size_t i = 0; i < std::size(labels); ++i) {
    const int y = top + 52 + (stacked ? 76 : 38) * static_cast<int>(i);
    localized_text(frame, labels[i], rotation, right_to_left ? end - labels[i].width : left + inset, y, muted);
    const int value_x = stacked || right_to_left ? left + inset : left + inset + label_column;
    ascii_text(frame, atlas, rotation, value_x, stacked ? y + 36 : y, values[i]);
  }
}

void draw_source_status(uint16_t *frame, StatusText text, PhotoSourceStatus status, ScreenRotation rotation) {
  StatusLabel key = StatusLabel::Count;
  switch (status) {
    case PhotoSourceStatus::WaitingForTime: key = StatusLabel::waiting_for_date; break;
    case PhotoSourceStatus::MemoriesEmpty: key = StatusLabel::no_memories; break;
    case PhotoSourceStatus::MemoriesUnavailable: key = StatusLabel::memories_unavailable; break;
    case PhotoSourceStatus::IncompleteFilters: key = StatusLabel::source_needs_setup; break;
    case PhotoSourceStatus::UnsupportedFilters: key = StatusLabel::filter_needs_v32; break;
    case PhotoSourceStatus::FiltersUnavailable: key = StatusLabel::check_photo_filters; break;
    case PhotoSourceStatus::NoPhotos: key = StatusLabel::no_photos; break;
    case PhotoSourceStatus::WaitingForPair: key = StatusLabel::waiting_for_photo_pair; break;
    case PhotoSourceStatus::None: return;
  }
  source_notice(frame, text, key, rotation);
}

void draw_connection_notice(uint16_t *frame, StatusText text, ImmichResponse response, ScreenRotation rotation) {
  source_notice(frame, text, response == ImmichResponse::Rejected ? StatusLabel::invalid_api_key : StatusLabel::connection_failed,
                rotation);
}

}  // namespace espframe::native
