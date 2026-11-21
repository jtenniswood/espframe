#include "photo_caption.h"

#include "clock_overlay.h"
#include "date_utils.h"

namespace espframe::native {
namespace {

constexpr int glyph_extent = 64;
constexpr size_t index_entry_size = 6;
constexpr size_t bitmap_header_size = 5;
constexpr int side_margin = 45;
constexpr int right = slide_width - side_margin;
constexpr int bottom = slide_height - 50;
constexpr int line_height = 34;
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
      if (offset > available || available - offset < bitmap_header_size)
        return nullptr;
      const auto *glyph = atlas.data() + bitmaps + offset;
      if (glyph[1] + glyph[3] > glyph_extent || glyph[2] + glyph[4] > glyph_extent)
        return nullptr;
      const size_t bitmap_size = ((glyph[3] + 1) / 2) * glyph[4];
      return bitmap_size <= available - offset - bitmap_header_size ? glyph : nullptr;
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
               ScreenRotation rotation, uint8_t opacity) {
  if (!line.width)
    return;
  const int step = -axis_direction(rotation);
  for (const int shadow : {2, 0}) {
    int x = right + shadow - line.width;
    const auto draw = [&](const uint8_t *glyph) {
      const auto *mask = glyph + bitmap_header_size;
      const int stride = (glyph[3] + 1) / 2;
      for (int column = 0; column < glyph[3]; ++column) {
        auto *pixels = frame + panel_offset(x + glyph[1] + column, panel_width - 1 - top - glyph[2] - shadow, rotation);
        for (int row = 0; row < glyph[4]; ++row) {
          const auto packed = mask[row * stride + column / 2];
          const unsigned coverage = column % 2 ? packed & 15 : packed >> 4;
          const unsigned ink = shadow == 0 ? coverage : (coverage + 2) / 5;
          const unsigned alpha = (ink * opacity + 7) / PhotoCaptionFade::opaque;
          if (alpha)
            pixels[row * step] = paint(pixels[row * step], alpha, shadow == 0);
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
  return width ? side_margin + width : 0;
}

PhotoCaption prepare_photo_caption(std::span<const uint8_t> locations, std::span<const uint8_t> dates,
                                   const PhotoMetadata &metadata, const PhotoMetadataSettings &settings,
                                   bool show_clock, const std::optional<std::tm> &local) {
  const int left = show_clock ? ClockOverlay::first_row + ClockOverlay::rows + clock_gap : side_margin;
  const int line_width = right - left;
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
  int location_top = bottom - line_height;
  if (caption.date.width) {
    draw_line(frame, dates, caption.date, location_top, rotation, opacity);
    location_top -= line_height + line_gap;
  }
  draw_line(frame, locations, caption.location, location_top, rotation, opacity);
}

}  // namespace espframe::native
