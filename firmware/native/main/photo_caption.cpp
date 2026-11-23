#include "photo_caption.h"

#include "clock_overlay.h"
#include "date_utils.h"

namespace espframe::native {
namespace {

constexpr int glyph_extent = 64;
constexpr int shadow_padding = 8;
constexpr size_t index_entry_size = 6;
// Each glyph stores its advance, two four-byte bounds, then the face and shadow masks.
constexpr size_t glyph_header_size = 9;
constexpr int side_margin = 45;
constexpr int line_height = 38;
constexpr int line_gap = 4;
constexpr int clock_gap = 24;

uint16_t read_codepoint(std::string_view &text) {
  const auto first = static_cast<uint8_t>(text.front());
  if (first < 128) {
    text.remove_prefix(1);
    return first;
  }
  const size_t size = first < 0xe0 ? 2 : 3;
  if (first < 0xc2 || first >= 0xf0 || text.size() < size)
    return 0;
  uint16_t code = first & (size == 2 ? 31 : 15);
  for (size_t index = 1; index < size; ++index) {
    const auto byte = static_cast<uint8_t>(text[index]);
    if ((byte & 0xc0) != 0x80)
      return 0;
    code = static_cast<uint16_t>((code << 6) | (byte & 63));
  }
  if ((size == 3 && code < 0x800) || (code >= 0xd800 && code <= 0xdfff))
    return 0;
  text.remove_prefix(size);
  return code;
}

size_t mask_size(const uint8_t *bounds) {
  return ((bounds[2] + 1) / 2) * bounds[3];
}

const uint8_t *find_glyph(std::span<const uint8_t> atlas, uint16_t code) {
  if (atlas.size() < 2)
    return nullptr;
  const size_t count = atlas[0] | (atlas[1] << 8);
  const size_t bitmaps = 2 + count * index_entry_size;
  if (bitmaps > atlas.size())
    return nullptr;
  size_t first = 0;
  size_t end = count;
  while (first < end) {
    const size_t middle = first + (end - first) / 2;
    const auto *record = atlas.data() + 2 + middle * index_entry_size;
    const auto key = static_cast<uint16_t>(record[0] | (record[1] << 8));
    if (key == code) {
      const uint32_t offset = record[2] | (record[3] << 8) | (record[4] << 16) | (uint32_t{record[5]} << 24);
      const size_t available = atlas.size() - bitmaps;
      if (offset > available || available - offset < glyph_header_size)
        return nullptr;
      const auto *glyph = atlas.data() + bitmaps + offset;
      for (const auto *bounds : {glyph + 1, glyph + 5}) {
        if (bounds[0] + bounds[2] > glyph_extent || bounds[1] + bounds[3] > glyph_extent)
          return nullptr;
      }
      const auto bytes = mask_size(glyph + 1) + mask_size(glyph + 5);
      return bytes <= available - offset - glyph_header_size ? glyph : nullptr;
    }
    if (key < code)
      first = middle + 1;
    else
      end = middle;
  }
  return nullptr;
}

std::string_view supported(std::span<const uint8_t> atlas, std::string_view value) {
  auto remaining = value;
  while (!remaining.empty()) {
    if (!find_glyph(atlas, read_codepoint(remaining)))
      return {};
  }
  return value;
}

uint16_t paint(uint16_t old, unsigned opacity, unsigned light) {
  const auto channel = [opacity, light](unsigned value, unsigned max) {
    return (value * (15 - opacity) + light * max * opacity + 7) / 15;
  };
  return static_cast<uint16_t>((channel(old >> 11, 31) << 11) |
      (channel((old >> 5) & 63, 63) << 5) | channel(old & 31, 31));
}

PhotoCaptionLine layout_line(std::span<const uint8_t> atlas, std::string_view value, int line_width) {
  PhotoCaptionLine line;
  const auto *ellipsis = find_glyph(atlas, 0x2026);
  const int tail = ellipsis ? ellipsis[0] : 0;
  auto remaining = value;
  while (!remaining.empty()) {
    const auto *glyph = find_glyph(atlas, read_codepoint(remaining));
    const auto length = value.size() - remaining.size();
    if (!glyph || length > line.text.size() || line.width + glyph[0] + (remaining.empty() ? 0 : tail) > line_width)
      break;
    line.width += glyph[0];
    line.length = static_cast<uint8_t>(length);
  }
  std::copy_n(value.begin(), line.length, line.text.begin());
  line.ellipsis = ellipsis && line.length < value.size();
  if (line.ellipsis)
    line.width += tail;
  return line;
}

void draw_line(uint16_t *frame, std::span<const uint8_t> atlas, const PhotoCaptionLine &line, int top,
               ScreenRotation rotation, uint8_t opacity, bool shadow) {
  if (!line.width)
    return;
  const Viewport viewport{rotation};
  const int padding = shadow ? shadow_padding : 0;
  int x = viewport.width - side_margin - line.width - padding;
  const auto draw = [&](const uint8_t *glyph) {
    const auto *bounds = glyph + (shadow ? 5 : 1);
    const auto *mask = glyph + glyph_header_size + (shadow ? mask_size(glyph + 1) : 0);
    const int stride = (bounds[2] + 1) / 2;
    const int y = top - padding + bounds[1];
    for (int column = 0; column < bounds[2]; ++column) {
      auto *pixels = frame + viewport.offset(x + bounds[0] + column, y);
      for (int row = 0; row < bounds[3]; ++row) {
        const auto packed = mask[row * stride + column / 2];
        const unsigned coverage = column % 2 ? packed & 15 : packed >> 4;
        const unsigned alpha = (coverage * opacity + 7) / PhotoCaptionFade::opaque;
        if (alpha)
          pixels[row * viewport.y_stride] = paint(pixels[row * viewport.y_stride], alpha, !shadow);
      }
    }
    x += glyph[0];
  };
  std::string_view displayed{line.text.data(), line.length};
  while (!displayed.empty()) {
    draw(find_glyph(atlas, read_codepoint(displayed)));
  }
  if (line.ellipsis)
    draw(find_glyph(atlas, 0x2026));
}

std::string hebrew_date_visual_order(std::string_view text) {
  // The Hebrew date templates contain Hebrew words, decimal numbers and
  // separators. Lay out the words right to left and keep each number intact.
  std::string result;
  result.reserve(text.size());
  while (!text.empty()) {
    size_t start = text.size() - 1;
    const auto digit = [](char value) { return value >= '0' && value <= '9'; };
    if (digit(text[start])) {
      while (start > 0 && digit(text[start - 1]))
        --start;
    } else {
      while (start > 0 && (static_cast<uint8_t>(text[start]) & 0xc0) == 0x80)
        --start;
    }
    result += text.substr(start);
    text.remove_suffix(text.size() - start);
  }
  return result;
}

}  // namespace

std::string photo_caption_date(const PhotoMetadata &metadata, const PhotoMetadataSettings &settings,
                               const std::optional<std::tm> &local) {
  if (!settings.show_date || !metadata.capture_day)
    return {};
  const std::chrono::year_month_day day{*metadata.capture_day};
  const auto year = static_cast<int>(day.year());
  const auto month = static_cast<unsigned>(day.month());
  const auto number = static_cast<unsigned>(day.day());
  const auto language = settings.language < espframe_i18n_catalogue::LANGUAGE_COUNT ? settings.language : 0;
  std::string date;
  if (settings.relative && local)
    date = format_photo_age(year, month, number, local->tm_year + 1900, local->tm_mon + 1, local->tm_mday, language);
  else
    date = settings.month_before_day ? format_photo_date_month_day_year(year, month, number, language)
                                     : format_photo_date_full(year, month, number, language);
  if (std::string_view{espframe_i18n_catalogue::LANGUAGES[language]} == "he")
    return hebrew_date_visual_order(date);
  return date;
}

int PhotoCaption::band_rows() const {
  const auto width = std::max(location.width, date.width);
  return width ? side_margin + width + shadow_padding : 0;
}

bool PhotoCaption::fits_beside_clock(ScreenRotation rotation) const {
  return band_rows() <= Viewport{rotation}.width - (ClockOverlay::first_row + ClockOverlay::rows + clock_gap);
}

PhotoCaption prepare_photo_caption(std::span<const uint8_t> locations, std::span<const uint8_t> dates,
                                   const PhotoMetadata &metadata, const PhotoMetadataSettings &settings,
                                   bool show_clock, const std::optional<std::tm> &local, ScreenRotation rotation) {
  const int left = (show_clock ? ClockOverlay::first_row + ClockOverlay::rows + clock_gap : side_margin) + shadow_padding;
  const int line_width = Viewport{rotation}.width - side_margin - left;
  PhotoCaption caption;
  caption.date = layout_line(dates, photo_caption_date(metadata, settings, local), line_width);
  if (settings.show_location) {
    const auto city = supported(locations, metadata.city.data());
    const auto country = supported(locations, metadata.country.data());
    if (!city.empty() && !country.empty() && city != country) {
      std::string place{city};
      place += ", ";
      place += country;
      caption.location = layout_line(locations, place, line_width);
    } else {
      caption.location = layout_line(locations, city.empty() ? country : city, line_width);
    }
  }
  return caption;
}

void paint_photo_caption(uint16_t *frame, std::span<const uint8_t> locations, std::span<const uint8_t> dates,
                         const PhotoCaption &caption, ScreenRotation rotation, uint8_t opacity) {
  if (!opacity)
    return;
  const int date_top = Viewport{rotation}.height - 50 - line_height;
  const int location_top = caption.date.width ? date_top - line_height - line_gap : date_top;
  for (const bool shadow : {true, false}) {
    draw_line(frame, dates, caption.date, date_top, rotation, opacity, shadow);
    draw_line(frame, locations, caption.location, location_top, rotation, opacity, shadow);
  }
}

}  // namespace espframe::native
