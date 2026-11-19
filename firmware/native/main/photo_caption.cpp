#include "photo_caption.h"

#include "date_utils.h"

namespace espframe::native {
namespace {

constexpr int glyph_height = 32;
constexpr int glyph_width = 32;
constexpr size_t index_entry_size = 6;
constexpr size_t bitmap_header_size = 5;
constexpr int left = 44;
constexpr int bottom = 701;
constexpr int line_gap = 4;
constexpr int line_width = 620;

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
      if (glyph[1] + glyph[3] > glyph_width || glyph[2] + glyph[4] > glyph_height)
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

void draw_line(uint16_t *frame, std::span<const uint8_t> atlas, std::string_view value, int top,
               ScreenRotation rotation) {
  const int step = -axis_direction(rotation);
  const auto *ellipsis = find_glyph(atlas, 0x2026);
  const int tail = ellipsis ? ellipsis[0] : 0;
  for (const int shadow : {2, 0}) {
    int x = left + shadow;
    auto remaining = value;
    while (!remaining.empty()) {
      const auto *glyph = find_glyph(atlas, read_codepoint(remaining));
      if (!glyph)
        break;
      if (x + glyph[0] + (remaining.empty() ? 0 : tail) > left + shadow + line_width) {
        glyph = ellipsis;
        remaining = {};
        if (!glyph)
          break;
      }
      const auto *mask = glyph + bitmap_header_size;
      const int stride = (glyph[3] + 1) / 2;
      for (int column = 0; column < glyph[3]; ++column) {
        auto *pixels = frame + panel_offset(x + glyph[1] + column, panel_width - 1 - top - glyph[2] - shadow, rotation);
        for (int row = 0; row < glyph[4]; ++row) {
          const auto packed = mask[row * stride + column / 2];
          const unsigned alpha = column % 2 ? packed & 15 : packed >> 4;
          if (alpha)
            pixels[row * step] = paint(pixels[row * step], alpha, shadow == 0);
        }
      }
      x += glyph[0];
    }
  }
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
  if (settings.relative && local)
    return format_photo_age(year, month, number, local->tm_year + 1900, local->tm_mon + 1, local->tm_mday);
  return settings.month_before_day ? format_photo_date_month_day_year(year, month, number)
                                    : format_photo_date_full(year, month, number);
}

void paint_photo_caption(uint16_t *frame, std::span<const uint8_t> atlas, const PhotoMetadata &metadata,
                         const PhotoMetadataSettings &settings, ScreenRotation rotation,
                         const std::optional<std::tm> &local) {
  const auto date = photo_caption_date(metadata, settings, local);
  int location_top = bottom - glyph_height;
  if (!date.empty()) {
    draw_line(frame, atlas, date, location_top, rotation);
    location_top -= glyph_height + line_gap;
  }
  if (settings.show_location) {
    const auto city = supported(atlas, metadata.city.data());
    const auto country = supported(atlas, metadata.country.data());
    if (!city.empty() && !country.empty() && city != country) {
      std::string place{city};
      place += ", ";
      place += country;
      draw_line(frame, atlas, place, location_top, rotation);
    } else {
      draw_line(frame, atlas, city.empty() ? country : city, location_top, rotation);
    }
  }
}

}  // namespace espframe::native
