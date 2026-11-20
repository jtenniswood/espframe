#include "photo_caption.h"
#include "clock_overlay.h"
#include "i18n.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using namespace espframe::native;
using namespace std::chrono;

int main(int argc, char **argv) {
  assert(argc == 3);
  std::ifstream glyphs(argv[1], std::ios::binary);
  const std::vector<uint8_t> atlas{std::istreambuf_iterator<char>{glyphs}, {}};
  std::ifstream date_glyphs(argv[2], std::ios::binary);
  const std::vector<uint8_t> dates{std::istreambuf_iterator<char>{date_glyphs}, {}};
  assert(atlas.size() > 1000 && atlas.size() < 512 * 1024);
  assert(dates.size() > 1000 && dates.size() < 64 * 1024);

  PhotoMetadata photo;
  photo.capture_day = sys_days{year(2024)/2/29};
  std::strcpy(photo.city.data(), "São Paulo");
  std::strcpy(photo.country.data(), "Brazil");
  PhotoMetadataSettings settings;
  assert(photo_caption_date(photo, settings, {}) == "29 February, 2024");
  settings.month_before_day = true;
  assert(photo_caption_date(photo, settings, {}) == "February 29, 2024");
  settings.relative = true;
  std::tm local{};
  local.tm_year = 124;
  local.tm_mon = 2;
  local.tm_mday = 1;
  assert(photo_caption_date(photo, settings, local) == "1 day ago");
  assert(photo_caption_date(photo, settings, {}) == "February 29, 2024");
  settings.month_before_day = false;
  assert(photo_caption_date(photo, settings, {}) == "29 February, 2024");
  assert(photo_caption_date(PhotoMetadata{}, settings, {}).empty());

  settings.language = find_espframe_language("de");
  assert(photo_caption_date(photo, settings, local) == "vor 1 Tag");
  assert(photo_caption_date(photo, settings, {}) == "29 Februar, 2024");
  settings.language = find_espframe_language("ru");
  assert(photo_caption_date(photo, settings, {}) == "29 Февраль, 2024");
  settings.language = find_espframe_language("he");
  assert(photo_caption_date(photo, settings, {}) == "2024 ,ראורבפ 29");
  settings.month_before_day = true;
  assert(photo_caption_date(photo, settings, {}) == "2024 ,29 ראורבפ");
  local.tm_mday = 12;
  assert(photo_caption_date(photo, settings, local) == "םימי 12 ינפל");
  settings.language = 0;
  settings.month_before_day = false;
  local.tm_mday = 1;

  constexpr uint16_t background = 0x53a9;
  const std::vector<uint16_t> blank(pixel_count, background);
  auto normal = blank;
  auto rotation = blank;
  paint_photo_caption(normal.data(), atlas, dates, photo, settings, ScreenRotation::Normal, true, local);
  paint_photo_caption(rotation.data(), atlas, dates, photo, settings, ScreenRotation::UpsideDown, true, local);
  assert(normal != blank && std::equal(normal.begin(), normal.end(), rotation.rbegin()));
  assert(std::equal(normal.begin(), normal.begin() + 800 * panel_width, blank.begin()));
  auto country_only = blank;
  const auto city = photo.city;
  photo.city = {};
  paint_photo_caption(country_only.data(), atlas, dates, photo, settings, ScreenRotation::Normal, true, local);
  assert(normal != country_only);
  auto unsupported_city = blank;
  std::strcpy(photo.city.data(), "東京");
  paint_photo_caption(unsupported_city.data(), atlas, dates, photo, settings, ScreenRotation::Normal, true, local);
  assert(unsupported_city == country_only);
  photo.city = city;

  auto capture_date_only = blank;
  settings.show_location = false;
  paint_photo_caption(capture_date_only.data(), atlas, dates, photo, settings, ScreenRotation::Normal, false);
  assert(capture_date_only != blank);
  auto absolute_date = blank;
  settings.relative = false;
  paint_photo_caption(absolute_date.data(), atlas, dates, photo, settings, ScreenRotation::Normal, false, local);
  assert(absolute_date == capture_date_only);

  for (size_t language = 1; language < espframe_i18n_catalogue::LANGUAGE_COUNT; ++language) {
    settings.language = static_cast<uint8_t>(language);
    auto localized = blank;
    auto flipped = blank;
    paint_photo_caption(localized.data(), atlas, dates, photo, settings, ScreenRotation::Normal, true);
    paint_photo_caption(flipped.data(), atlas, dates, photo, settings, ScreenRotation::UpsideDown, true);
    assert(localized != blank && std::equal(localized.begin(), localized.end(), flipped.rbegin()));
  }
  settings.language = 0;

  settings.show_date = false;
  settings.show_location = true;
  std::strcpy(photo.city.data(), "A long city name on the far northern coastline of the country");
  std::strcpy(photo.country.data(), "Another long name for the surrounding territory and islands");
  auto clock_visible = blank;
  auto clock_hidden = blank;
  rotation = blank;
  paint_photo_caption(clock_visible.data(), atlas, dates, photo, settings, ScreenRotation::Normal, true);
  paint_photo_caption(clock_hidden.data(), atlas, dates, photo, settings, ScreenRotation::Normal, false);
  paint_photo_caption(rotation.data(), atlas, dates, photo, settings, ScreenRotation::UpsideDown, true);
  assert(clock_visible != blank && std::equal(clock_visible.begin(), clock_visible.end(), rotation.rbegin()));
  const auto clock_rows = (ClockOverlay::first_row + ClockOverlay::rows) * panel_width;
  assert(std::equal(clock_visible.begin(), clock_visible.begin() + clock_rows, blank.begin()));
  assert(!std::equal(clock_hidden.begin(), clock_hidden.begin() + clock_rows, blank.begin()));

  settings.show_date = settings.show_location = false;
  normal = blank;
  paint_photo_caption(normal.data(), atlas, dates, photo, settings, ScreenRotation::Normal, true, local);
  assert(normal == blank);
}
