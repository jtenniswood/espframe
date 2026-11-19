#include "immich_selection.h"

#include <cassert>

using namespace espframe::native;
using namespace std::chrono;

int main() {
  constexpr std::string_view response = R"([
    {"id":"c48e2980-18ac-4d7d-876a-a636759a1aec","localDateTime":"2024-03-01T00:12:00.000Z","width":700,"height":900,
     "exifInfo":{"city":" São Paulo ","country":"Brazil","dateTimeOriginal":"2024-02-29T23:12:00Z"}},
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

  const auto photos = parse_photo_search(response, 16, false, false).photos;
  assert(photos.size() == 6);
  const auto &primary = photos[0];
  assert(primary.portrait && primary.metadata.capture_day == sys_days{year(2024)/3/1});
  assert(std::string_view(primary.metadata.city.data()) == "São Paulo");
  assert(std::string_view(primary.metadata.country.data()) == "Brazil");
  assert(!is_portrait_companion(primary, primary));
  assert(is_portrait_companion(primary, photos[1]));
  assert(is_portrait_companion(primary, photos[2]));
  assert(!is_portrait_companion(primary, photos[3])); // Normalized dimensions take precedence over EXIF.
  assert(photos[4].portrait && !is_portrait_companion(primary, photos[4]));
  assert(!photos[5].metadata.capture_day && !is_portrait_companion(primary, photos[5]));
  assert(parse_photo_search(response, 2, false, false).photos.size() == 2);
  const auto visible = parse_photo_search(R"([
    {"id":"c48e2980-18ac-4d7d-876a-a636759a1aec","visibility":"archive"},
    {"id":"eb492926-9046-408b-be47-a959620753bb","visibility":"hidden"},
    {"id":"0406c468-dabf-4777-bb93-ac6fdac62857","visibility":"timeline"}
  ])", 3, false, false);
  assert(visible.photos.size() == 1);
  assert(std::string_view(visible.photos.front().id.data()) == "0406c468-dabf-4777-bb93-ac6fdac62857");
  assert(!parse_photo_search("[] invalid", 16, false, false).valid);
  assert(!parse_photo_search("{\"assets\":[]}", 16, true, false).valid);

  const auto march_first = parse_calendar_day("2024-03-01");
  const auto leap_day = parse_calendar_day("2024-02-29");
  assert(march_first && leap_day);
  assert(!parse_calendar_day("2023-02-29"));
  assert(!parse_calendar_day("2024-2-09"));
  assert(!parse_calendar_day("2024-02-29T00:00:00"));
  const CaptureDateRange march{march_first, march_first};
  const auto march_photos = parse_photo_search(response, 16, false, false, march).photos;
  assert(march_photos.size() == 4 && march_photos.front().id == primary.id);
  assert(parse_photo_search(response, 16, false, false, {{}, leap_day}).photos.size() == 1);
  CaptureDateSettings relative;
  relative.enabled = relative.relative = true;
  assert(!relative.resolve({}));
  assert(relative.resolve(leap_day)->from == parse_calendar_day("2023-02-28"));
  relative.years = false;
  const auto rolling = relative.resolve(parse_calendar_day("2024-03-31"));
  assert(rolling && rolling->from == leap_day && rolling->through == parse_calendar_day("2024-03-31"));

  using Api = ImmichSearchApi;
  const auto all = make_photo_search(4, {}, Api::Unknown);
  assert(all.path == "/search/random" && !all.paged && all.body == R"({"type":"IMAGE","visibility":"timeline","size":4})");
  assert(make_photo_search(12, {}, Api::Unknown, primary.metadata.capture_day).body ==
         R"({"type":"IMAGE","visibility":"timeline","size":12,"withExif":true,"takenAfter":"2024-02-29T00:00:00.000Z","takenBefore":"2024-03-02T23:59:59.999Z"})");
  assert(make_photo_search(4, {}, Api::Legacy, {}, 1, false, {march_first, {}}).body ==
         R"({"type":"IMAGE","visibility":"timeline","size":4,"takenAfter":"2024-02-29T00:00:00.000Z"})");
  assert(make_photo_search(4, {}, Api::Structured, {}, 1, false, {{}, leap_day}).body ==
         R"({"size":4,"filter":{"type":{"eq":"IMAGE"},"visibility":{"eq":"timeline"},"takenAt":{"lte":"2024-03-01T23:59:59.999Z"}}})");
  assert(make_photo_search(4, {}, Api::Structured, leap_day, 1, true, march).path.empty());

  assert(parse_search_api(R"({"major":2,"minor":5,"patch":6})") == Api::Legacy);
  assert(parse_search_api(R"({"major":3,"minor":1,"patch":9})") == Api::Legacy);
  assert(parse_search_api(R"({"major":3,"minor":2,"patch":0})") == Api::Structured);
  assert(parse_search_api(R"({"major":4,"minor":0,"patch":0})") == Api::Structured);
  assert(parse_search_api(R"({"major":"3","minor":2})") == Api::Unknown);
  assert(parse_search_api(R"({"major":3})") == Api::Unknown);

  const PhotoSource album{PhotoSourceMode::Album, "c48e2980-18ac-4d7d-876a-a636759a1aec"};
  const PhotoSource favorites{PhotoSourceMode::Favorites, {}};
  assert(make_photo_search(16, album, Api::Unknown).path.empty());
  assert(make_photo_search(16, favorites, Api::Unknown).path.empty());
  assert(make_photo_search(16, {PhotoSourceMode::Album, {}}, Api::Legacy).path.empty());
  const auto legacy = make_photo_search(16, album, Api::Legacy, {}, 7);
  assert(legacy.path == "/search/metadata" && legacy.paged);
  assert(legacy.body == R"({"type":"IMAGE","visibility":"timeline","size":16,"page":7,"albumIds":["c48e2980-18ac-4d7d-876a-a636759a1aec"]})");
  const auto modern = make_photo_search(16, album, Api::Structured);
  assert(modern.path == "/search/random" && !modern.paged);
  assert(modern.body == R"({"size":16,"filter":{"type":{"eq":"IMAGE"},"visibility":{"eq":"timeline"},"albumIds":{"any":["c48e2980-18ac-4d7d-876a-a636759a1aec"]}}})");
  assert(make_photo_search(4, album, Api::Legacy).body ==
         R"({"type":"IMAGE","visibility":"timeline","size":4,"page":1,"albumIds":["c48e2980-18ac-4d7d-876a-a636759a1aec"]})");
  assert(make_photo_search(12, album, Api::Legacy, primary.metadata.capture_day).body ==
         R"({"type":"IMAGE","visibility":"timeline","size":12,"page":1,"withExif":true,"albumIds":["c48e2980-18ac-4d7d-876a-a636759a1aec"],"takenAfter":"2024-02-29T00:00:00.000Z","takenBefore":"2024-03-02T23:59:59.999Z"})");
  assert(make_photo_search(12, album, Api::Structured, primary.metadata.capture_day).body ==
         R"({"size":12,"withExif":true,"filter":{"type":{"eq":"IMAGE"},"visibility":{"eq":"timeline"},"albumIds":{"any":["c48e2980-18ac-4d7d-876a-a636759a1aec"]},"takenAt":{"gte":"2024-02-29T00:00:00.000Z","lte":"2024-03-02T23:59:59.999Z"}}})");
  assert(make_photo_search(4, favorites, Api::Legacy).body == R"({"type":"IMAGE","visibility":"timeline","size":4,"isFavorite":true})");
  assert(make_photo_search(12, favorites, Api::Structured, primary.metadata.capture_day).body ==
         R"({"size":12,"withExif":true,"filter":{"type":{"eq":"IMAGE"},"visibility":{"eq":"timeline"},"isFavorite":{"eq":true},"takenAt":{"gte":"2024-02-29T00:00:00.000Z","lte":"2024-03-02T23:59:59.999Z"}}})");

  assert(make_photo_search(4, {}, Api::Legacy, {}, 1, true).body ==
         R"({"type":"IMAGE","visibility":"timeline","size":4,"withExif":true})");
  assert(make_photo_search(4, favorites, Api::Structured, {}, 1, true).body ==
         R"({"size":4,"withExif":true,"filter":{"type":{"eq":"IMAGE"},"visibility":{"eq":"timeline"},"isFavorite":{"eq":true}}})");

  const auto page = parse_photo_search(R"({"assets":{"items":[{"id":"c48e2980-18ac-4d7d-876a-a636759a1aec"}],"nextPage":"8"}})",
                                       16, true, false);
  assert(page.valid && page.photos.size() == 1 && page.next_page == 8);
  const auto last_page = parse_photo_search(R"({"assets":{"items":[],"nextPage":null}})", 16, true, false);
  assert(last_page.valid && last_page.photos.empty() && last_page.next_page == 0);
  assert(!parse_photo_search(response, 16, true, false).valid);
  const auto favorite_results = parse_photo_search(R"([
    {"id":"c48e2980-18ac-4d7d-876a-a636759a1aec","isFavorite":false},
    {"id":"eb492926-9046-408b-be47-a959620753bb","isFavorite":true},
    {"id":"0406c468-dabf-4777-bb93-ac6fdac62857"}])", 16, false, true);
  assert(favorite_results.valid && favorite_results.photos.size() == 1);
  assert(std::string_view(favorite_results.photos[0].id.data()) == "eb492926-9046-408b-be47-a959620753bb");
}
