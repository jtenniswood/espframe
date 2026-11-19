#include "photo_caption.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using namespace espframe::native;
using namespace std::chrono;

int main(int argc, char **argv) {
  assert(argc == 2);
  std::ifstream glyphs(argv[1], std::ios::binary);
  const std::vector<uint8_t> atlas{std::istreambuf_iterator<char>{glyphs}, {}};
  assert(atlas.size() > 1000 && atlas.size() < 256 * 1024);

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

  constexpr uint16_t background = 0x53a9;
  const std::vector<uint16_t> blank(pixel_count, background);
  auto normal = blank;
  auto rotation = blank;
  paint_photo_caption(normal.data(), atlas, photo, settings, ScreenRotation::Normal, local);
  paint_photo_caption(rotation.data(), atlas, photo, settings, ScreenRotation::UpsideDown, local);
  assert(normal != blank && std::equal(normal.begin(), normal.end(), rotation.rbegin()));
  auto country_only = blank;
  const auto city = photo.city;
  photo.city = {};
  paint_photo_caption(country_only.data(), atlas, photo, settings, ScreenRotation::Normal, local);
  assert(normal != country_only);
  auto unsupported_city = blank;
  std::strcpy(photo.city.data(), "東京");
  paint_photo_caption(unsupported_city.data(), atlas, photo, settings, ScreenRotation::Normal, local);
  assert(unsupported_city == country_only);
  photo.city = city;
  settings.show_date = settings.show_location = false;
  normal = blank;
  paint_photo_caption(normal.data(), atlas, photo, settings, ScreenRotation::Normal, local);
  assert(normal == blank);
}
