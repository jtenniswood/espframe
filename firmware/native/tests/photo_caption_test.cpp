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
  assert(atlas.size() > 1000 && atlas.size() < 640 * 1024);
  assert(dates.size() > 1000 && dates.size() < 96 * 1024);

  const auto paint = [&](uint16_t *frame, const PhotoMetadata &metadata, const PhotoMetadataSettings &style,
                         ScreenRotation rotation, bool clock, const std::optional<std::tm> &time = {}) {
    const auto caption = prepare_photo_caption(atlas, dates, metadata, style, clock, time);
    paint_photo_caption(frame, atlas, dates, caption, rotation, PhotoCaptionFade::opaque);
  };

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
  paint(normal.data(), photo, settings, ScreenRotation::Normal, true, local);
  paint(rotation.data(), photo, settings, ScreenRotation::UpsideDown, true, local);
  assert(normal != blank && std::equal(normal.begin(), normal.end(), rotation.rbegin()));
  assert(std::equal(normal.begin(), normal.begin() + 800 * panel_width, blank.begin()));
  auto country_only = blank;
  const auto city = photo.city;
  photo.city = {};
  paint(country_only.data(), photo, settings, ScreenRotation::Normal, true, local);
  assert(normal != country_only);
  auto unsupported_city = blank;
  std::strcpy(photo.city.data(), "東京");
  paint(unsupported_city.data(), photo, settings, ScreenRotation::Normal, true, local);
  assert(unsupported_city == country_only);
  photo.city = city;

  auto capture_date_only = blank;
  settings.show_location = false;
  paint(capture_date_only.data(), photo, settings, ScreenRotation::Normal, false);
  assert(capture_date_only != blank);
  auto absolute_date = blank;
  settings.relative = false;
  paint(absolute_date.data(), photo, settings, ScreenRotation::Normal, false, local);
  assert(absolute_date == capture_date_only);

  auto date_as_location = blank;
  auto location_photo = photo;
  const auto date = photo_caption_date(photo, settings, {});
  std::strcpy(location_photo.city.data(), date.c_str());
  location_photo.country = {};
  auto location_style = settings;
  location_style.show_date = false;
  location_style.show_location = true;
  paint(date_as_location.data(), location_photo, location_style, ScreenRotation::Normal, false);
  assert(date_as_location == capture_date_only);

  for (size_t language = 1; language < espframe_i18n_catalogue::LANGUAGE_COUNT; ++language) {
    settings.language = static_cast<uint8_t>(language);
    auto localized = blank;
    auto flipped = blank;
    paint(localized.data(), photo, settings, ScreenRotation::Normal, true);
    paint(flipped.data(), photo, settings, ScreenRotation::UpsideDown, true);
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
  paint(clock_visible.data(), photo, settings, ScreenRotation::Normal, true);
  paint(clock_hidden.data(), photo, settings, ScreenRotation::Normal, false);
  paint(rotation.data(), photo, settings, ScreenRotation::UpsideDown, true);
  assert(clock_visible != blank && std::equal(clock_visible.begin(), clock_visible.end(), rotation.rbegin()));
  const auto clock_rows = (ClockOverlay::first_row + ClockOverlay::rows) * panel_width;
  assert(std::equal(clock_visible.begin(), clock_visible.begin() + clock_rows, blank.begin()));
  assert(!std::equal(clock_hidden.begin(), clock_hidden.begin() + clock_rows, blank.begin()));

  const auto caption = prepare_photo_caption(atlas, dates, photo, settings, true);
  const int first_caption_row = panel_height - caption.band_rows();
  auto translucent = blank;
  paint_photo_caption(translucent.data(), atlas, dates, caption, ScreenRotation::Normal, 7);
  assert(translucent != blank && translucent != clock_visible);
  assert(std::equal(translucent.begin(), translucent.begin() + first_caption_row * panel_width, blank.begin()));
  std::vector<uint16_t> shadow_tones;
  for (const auto pixel : clock_visible) {
    if (pixel < background && std::find(shadow_tones.begin(), shadow_tones.end(), pixel) == shadow_tones.end())
      shadow_tones.push_back(pixel);
  }
  assert(shadow_tones.size() >= 3);
  auto invisible = blank;
  paint_photo_caption(invisible.data(), atlas, dates, caption, ScreenRotation::Normal, 0);
  assert(invisible == blank);

  settings.show_date = settings.show_location = false;
  normal = blank;
  paint(normal.data(), photo, settings, ScreenRotation::Normal, true, local);
  assert(normal == blank);

  PhotoCaptionFade fade;
  assert(fade.update(0, 1).opacity == 0);
  assert(fade.update(249'999, 1).opacity == 0);
  assert((fade.update(320'000, 1) == PhotoCaptionSample{1, 7}));
  assert((fade.update(390'000, 1) == PhotoCaptionSample{1, PhotoCaptionFade::opaque}));

  assert((fade.update(500'000, std::nullopt) == PhotoCaptionSample{1, PhotoCaptionFade::opaque}));
  assert((fade.update(535'000, std::nullopt) == PhotoCaptionSample{1, 7}));
  assert(fade.update(570'000, std::nullopt).opacity == 0);

  // A new swipe before the caption appears restarts the still period.
  assert(fade.update(600'000, 2).opacity == 0);
  assert(fade.update(849'999, 2).opacity == 0);
  assert(fade.update(850'000, std::nullopt).opacity == 0);
  assert(fade.update(900'000, 2).opacity == 0);
  assert(fade.update(1'149'999, 2).opacity == 0);
  assert((fade.update(1'290'000, 2) == PhotoCaptionSample{2, PhotoCaptionFade::opaque}));
}
