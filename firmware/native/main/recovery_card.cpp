#include "recovery_card.h"
#include "i18n_generated.h"

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

enum class StatusLabel {
#define ESPFRAME_STATUS_LABEL(name) name,
#include "status_labels.def"
#undef ESPFRAME_STATUS_LABEL
  Count
};

struct Line {
  int width{};
  std::span<const uint8_t> runs;
};

bool rtl(StatusText text) {
  return text.language < espframe_i18n_catalogue::LANGUAGE_COUNT &&
         std::string_view{espframe_i18n_catalogue::LANGUAGES[text.language]} == "he";
}

Line line(StatusText text, StatusLabel label) {
  constexpr size_t fingerprint_bytes = 32;
  constexpr size_t label_count = static_cast<size_t>(StatusLabel::Count);
  constexpr size_t table_size = fingerprint_bytes + 4 * (label_count * espframe_i18n_catalogue::LANGUAGE_COUNT + 1);
  if (text.masks.size() < table_size || label == StatusLabel::Count)
    return {};
  const size_t language = text.language < espframe_i18n_catalogue::LANGUAGE_COUNT ? text.language : 0;
  const auto offset = [&](size_t index) -> size_t {
    const auto *bytes = text.masks.data() + fingerprint_bytes + index * 4;
    return uint32_t{bytes[0]} | (uint32_t{bytes[1]} << 8) | (uint32_t{bytes[2]} << 16) | (uint32_t{bytes[3]} << 24);
  };
  const size_t index = language * label_count + static_cast<size_t>(label);
  const size_t begin = offset(index);
  const size_t end = offset(index + 1);
  if (begin < table_size || end <= begin || end - begin <= 2 || end > text.masks.size())
    return {};
  const int width = text.masks[begin] | (text.masks[begin + 1] << 8);
  if (width <= 0 || width > 668)
    return {};
  return {width, text.masks.subspan(begin + 2, end - begin - 2)};
}

uint16_t masked(uint16_t color, unsigned opacity) {
  const auto channel = [opacity](unsigned from, unsigned to) { return (from * (15 - opacity) + to * opacity + 7) / 15; };
  return static_cast<uint16_t>((channel(background >> 11, color >> 11) << 11) |
      (channel((background >> 5) & 63, (color >> 5) & 63) << 5) | channel(background & 31, color & 31));
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

void ascii_text(uint16_t *frame, const uint8_t *atlas, ScreenRotation rotation, int x, int y, std::string_view value) {
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
          pixels[row * viewport.y_stride] = masked(white, opacity);
      }
    }
    x += advance;
  }
}

void localized_text(uint16_t *frame, Line line, ScreenRotation rotation, int x, int y, uint16_t color) {
  const Viewport viewport{rotation};
  if (line.width <= 0 || x < 0 || x + line.width > viewport.width || y < 0 || y + glyph_height > viewport.height)
    return;
  size_t pixel = 0;
  const size_t pixel_count = line.width * glyph_height;
  for (const uint8_t run : line.runs) {
    const unsigned opacity = run >> 4;
    const size_t end = std::min(pixel + (run & 15) + 1, pixel_count);
    if (opacity == 0) {
      pixel = end;
      continue;
    }
    const uint16_t value = masked(color, opacity);
    for (; pixel < end; ++pixel) {
      const int column = pixel % line.width;
      const int row = pixel / line.width;
      frame[viewport.offset(x + column, y + row)] = value;
    }
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
