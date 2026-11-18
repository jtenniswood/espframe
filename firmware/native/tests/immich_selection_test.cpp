#include "immich_selection.h"

#include <cassert>

using namespace espframe::native;
using namespace std::chrono;

int main() {
  constexpr std::string_view response = R"([
    {"id":"c48e2980-18ac-4d7d-876a-a636759a1aec","localDateTime":"2024-03-01T00:12:00.000Z","width":700,"height":900},
    {"id":"c48e2980-18ac-4d7d-876a-a636759a1aec","width":900,"height":700},
    {"id":"eb492926-9046-408b-be47-a959620753bb","localDateTime":"2024-03-01T19:00:00.000Z","width":60,"height":90},
    {"id":"4aa2c696-c8d4-46c2-8dc2-3eda9087fb32","localDateTime":"2024-03-01T10:00:00.000Z",
     "exifInfo":{"exifImageWidth":2400,"exifImageHeight":1800,"orientation":"6"}},
    {"id":"0406c468-dabf-4777-bb93-ac6fdac62857","localDateTime":"2024-03-01T10:00:00.000Z","width":4000,"height":3000,
     "exifInfo":{"exifImageWidth":3000,"exifImageHeight":4000,"orientation":"1"}},
    {"id":"9e6e26d9-e821-4b7d-ae03-aa10ffc2201e","localDateTime":"2024-02-29T23:00:00.000Z","width":80,"height":90},
    {"id":"9e6e26d9-e821-4b7d-ae03-aa10ffc22010","localDateTime":"2023-02-29T23:00:00.000Z","width":80,"height":90},
    {"id":"not-a-uuid","localDateTime":"2024-03-01T19:00:00.000Z","width":80,"height":90}
  ] )";

  const auto photos = parse_photo_search(response, 16);
  assert(photos.size() == 6);
  const auto &primary = photos[0];
  assert(primary.portrait && primary.capture_day == sys_days{year(2024)/3/1});
  assert(!is_portrait_companion(primary, primary));
  assert(is_portrait_companion(primary, photos[1]));
  assert(is_portrait_companion(primary, photos[2]));
  assert(!is_portrait_companion(primary, photos[3])); // Normalized dimensions take precedence over EXIF.
  assert(photos[4].portrait && !is_portrait_companion(primary, photos[4]));
  assert(!photos[5].capture_day && !is_portrait_companion(primary, photos[5]));
  assert(parse_photo_search(response, 2).size() == 2);
  assert(parse_photo_search("[] invalid", 16).empty());
  assert(parse_photo_search("{\"assets\":[]}", 16).empty());

  assert(random_photo_request(4) == R"({"type":"IMAGE","size":4})");
  assert(random_photo_request(12, primary.capture_day) ==
         R"({"type":"IMAGE","size":12,"withExif":true,"takenAfter":"2024-02-29T00:00:00.000Z","takenBefore":"2024-03-02T23:59:59.999Z"})");
}
