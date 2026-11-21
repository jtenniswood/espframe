#include "immich_selection.h"
#include "unavailable_previews.h"

#include <cassert>

using namespace espframe::native;
using namespace std::chrono;

void test_unavailable_previews(const AssetId &first, const AssetId &second) {
  UnavailablePreviews unavailable;
  assert(!unavailable.contains(first, 0));
  for (const int status : {0, 400, 401, 403, 408, 429, 500, 503})
    assert(!unavailable.remember_failure(first, status, 1));
  assert(!unavailable.contains(first, 1));
  assert(unavailable.remember_failure(first, 404, 1));
  assert(unavailable.contains(first, 30'000'000));
  assert(!unavailable.contains(first, 30'000'001));
  assert(!unavailable.contains(second, 1));
  assert(unavailable.remember_failure(first, 410, 2));
  assert(unavailable.remember_failure(second, 413, 3));
  for (char suffix : std::string_view{"0123456789abcdefghijklmnopqrst"}) {
    AssetId id = first;
    id[35] = suffix;
    id[0] = 'f';
    assert(unavailable.remember_failure(id, 200, 4));
  }
  assert(unavailable.contains(first, 4));
  assert(unavailable.contains(second, 4));

  // A new failure evicts the oldest entry, and refreshing an existing
  // failure extends its expiry without evicting another preview.
  assert(unavailable.remember_failure(second, 404, 5));
  assert(unavailable.contains(first, 5));
  AssetId third = second;
  third[0] = 'f';
  assert(unavailable.remember_failure(third, 404, 6));
  assert(!unavailable.contains(first, 6));
  assert(unavailable.contains(second, 30'000'004));
  assert(!unavailable.contains(second, 30'000'005));
}

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
  test_unavailable_previews(primary.id, photos[1].id);
  assert(primary.portrait && primary.metadata.capture_day == sys_days{year(2024)/3/1});
  const auto landscape = parse_photo_search(R"([
    {"id":"c48e2980-18ac-4d7d-876a-a636759a1aec","width":80,"height":120},
    {"id":"0406c468-dabf-4777-bb93-ac6fdac62857","width":80,"height":80}])",
    1, false, false, {}, false, PhotoOrientation::Landscape);
  assert(landscape.valid && landscape.photos.size() == 1 && landscape.photos.front().id == photos[3].id);
  assert(std::string_view(primary.metadata.city.data()) == "São Paulo");
  assert(std::string_view(primary.metadata.country.data()) == "Brazil");
  assert(primary.metadata.location_known && !photos[1].metadata.location_known);
  const auto details = parse_photo_asset(R"({
    "id":"eb492926-9046-408b-be47-a959620753bb","type":"IMAGE",
    "exifInfo":{"city":"Amsterdam","country":"Netherlands"}
  })");
  assert(details && details->id == photos[1].id && details->metadata.location_known);
  assert(std::string_view(details->metadata.city.data()) == "Amsterdam");
  assert(!is_portrait_companion(primary, primary));
  assert(is_portrait_companion(primary, photos[1]));
  assert(is_portrait_companion(primary, photos[2]));
  assert(!is_portrait_companion(primary, photos[3])); // Normalized dimensions take precedence over EXIF.
  assert(photos[4].portrait && !is_portrait_companion(primary, photos[4]));
  assert(is_portrait_companion(primary, photos[4], 1)); // February 29 is one local day before March 1.
  auto two_days_away = photos[4];
  two_days_away.metadata.capture_day = *primary.metadata.capture_day + days{2};
  assert(!is_portrait_companion(primary, two_days_away, 1) && is_portrait_companion(primary, two_days_away, 2));
  const std::array wider_pair{primary, two_days_away};
  const auto only_pairs = [](std::span<const ImmichPhoto> candidates, unsigned days = 0, const AssetId &exclude = {}) {
    return find_pair_or_landscape_seed(candidates, days, exclude, PhotoOrientation::Portrait);
  };
  assert(!only_pairs(wider_pair, 1) && only_pairs(wider_pair, 2) == 1);
  assert(!photos[5].metadata.capture_day && !is_portrait_companion(primary, photos[5]));
  assert(primary.orientation_known && photos[2].orientation_known && photos[3].orientation_known);
  assert(primary.can_stage_unpaired(PhotoOrientation::Portrait, false));
  assert(!primary.can_stage_unpaired(PhotoOrientation::Landscape, false));
  assert(!primary.can_stage_unpaired(PhotoOrientation::Any, true));
  assert(photos[3].can_stage_unpaired(PhotoOrientation::Any, true));
  assert(can_start_portrait_pair(primary));
  assert(!can_start_portrait_pair(photos[3]));
  assert(!can_start_portrait_pair(photos[5]));
  assert(only_pairs(photos) == 2);
  assert(only_pairs(photos, 0, photos[2].id) == 1);
  assert(!only_pairs(wider_pair, 2, primary.id));
  assert(!only_pairs(wider_pair, 2, two_days_away.id));
  assert(!only_pairs(std::span(photos).last(3)));
  auto undated_landscape = photos[3];
  undated_landscape.metadata.capture_day.reset();
  assert(can_play_with_required_pairs(undated_landscape, PhotoOrientation::Any));
  assert(!can_play_with_required_pairs(undated_landscape, PhotoOrientation::Portrait));
  assert(!can_play_with_required_pairs(photos[5], PhotoOrientation::Any));
  const std::array mixed{primary, photos[1], undated_landscape, photos[4]};
  assert(find_pair_or_landscape_seed(mixed, 0, {}, PhotoOrientation::Any) == 2);
  assert(find_pair_or_landscape_seed(mixed, 0, undated_landscape.id, PhotoOrientation::Any) == 1);
  assert(only_pairs(mixed) == 1);
  const auto uncertain = parse_photo_search(R"([
    {"id":"ef492926-9046-408b-be47-a959620753bb","localDateTime":"2024-03-01T19:00:00Z","width":null,"height":null},
    {"id":"ec492926-9046-408b-be47-a959620753bb","localDateTime":"2024-03-01T19:00:00Z","exifInfo":{"exifImageWidth":1920,"exifImageHeight":1080}},
    {"id":"ed492926-9046-408b-be47-a959620753bb","localDateTime":"2024-03-01T19:00:00Z","isEdited":true,"width":1920,"height":1080}
  ])", 3, false, false).photos;
  assert(uncertain.size() == 3);
  for (const auto &photo : uncertain)
    assert(!photo.orientation_known && can_start_portrait_pair(photo) &&
           photo.may_match_orientation(PhotoOrientation::Portrait) && photo.may_match_orientation(PhotoOrientation::Landscape) &&
           !photo.can_stage_unpaired(PhotoOrientation::Any, false));
  assert(!only_pairs(uncertain));
  auto undated_preview = uncertain.front();
  undated_preview.metadata.capture_day.reset();
  assert(can_play_with_required_pairs(undated_preview, PhotoOrientation::Any));
  assert(!can_play_with_required_pairs(undated_preview, PhotoOrientation::Portrait));
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
  const CaptureDateRange pairing_scope{leap_day, parse_calendar_day("2024-03-02")};
  const auto expanded = make_photo_search(12, {}, Api::Structured, march_first, 1, false, pairing_scope, 2);
  assert(expanded.dates == pairing_scope);
  assert(expanded.body.find(R"("takenAt":{"gte":"2024-02-28T00:00:00.000Z","lte":"2024-03-03T23:59:59.999Z"})") !=
         std::string::npos);
  assert(parse_photo_search(response, 16, false, false, expanded.dates).photos.size() == 5);
  assert(make_photo_search(4, {}, Api::Structured, leap_day, 1, true, march, 2).path.empty());

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
  assert(page.valid && page.photos.size() == 1 && page.next_page == 8 && page.has_next_page(7));
  const auto filtered_page = parse_photo_search(
      R"({"assets":{"items":[{"id":"c48e2980-18ac-4d7d-876a-a636759a1aec","width":900,"height":700}],"nextPage":"8"}})",
      16, true, false, {}, false, PhotoOrientation::Portrait);
  assert(filtered_page.valid && filtered_page.photos.empty() && filtered_page.has_next_page(7));
  assert(!filtered_page.has_next_page(8) && !filtered_page.has_next_page(9));
  const auto last_page = parse_photo_search(R"({"assets":{"items":[],"nextPage":null}})", 16, true, false);
  assert(last_page.valid && last_page.photos.empty() && !last_page.has_next_page(8));
  const auto invalid_page = parse_photo_search(R"({"assets":{"items":null,"nextPage":"9"}})", 16, true, false);
  assert(!invalid_page.valid && !invalid_page.has_next_page(8));
  assert(!parse_photo_search(response, 16, true, false).valid);
  const auto favorite_results = parse_photo_search(R"([
    {"id":"c48e2980-18ac-4d7d-876a-a636759a1aec","isFavorite":false},
    {"id":"eb492926-9046-408b-be47-a959620753bb","isFavorite":true},
    {"id":"0406c468-dabf-4777-bb93-ac6fdac62857"}])", 16, false, true);
  assert(favorite_results.valid && favorite_results.photos.size() == 1);
  assert(std::string_view(favorite_results.photos[0].id.data()) == "eb492926-9046-408b-be47-a959620753bb");
  const auto not_favorites = parse_photo_search(R"([
    {"id":"c48e2980-18ac-4d7d-876a-a636759a1aec","isFavorite":false},
    {"id":"eb492926-9046-408b-be47-a959620753bb","isFavorite":true},
    {"id":"0406c468-dabf-4777-bb93-ac6fdac62857"}])", 16, false, false, {}, true);
  assert(not_favorites.valid && not_favorites.photos.size() == 1 && not_favorites.photos[0].id == photos[0].id);

  PhotoFilters filters;
  auto &criteria = filters.criteria;
  criteria.albums_enabled = true;
  assert(photo_filter_status(filters, Api::Structured) == PhotoFilterStatus::Incomplete);
  criteria.album_ids = album.album_id + ",0406c468-dabf-4777-bb93-ac6fdac62857";
  criteria.people_enabled = true;
  criteria.person_ids = "eb492926-9046-408b-be47-a959620753bb";
  criteria.tags_enabled = true;
  criteria.excluded_tag_ids = "4aa2c696-c8d4-46c2-8dc2-3eda9087fb32";
  criteria.favorites_enabled = true;
  criteria.favorite_mode = "Exclude favorites";
  criteria.rating_enabled = true;
  criteria.minimum_rating = 4;
  criteria.location_enabled = true;
  criteria.city = "Ville \"Nouvelle\"";
  filters.album_order = "Album list order";
  assert(photo_filter_status(filters, Api::Legacy) == PhotoFilterStatus::Unsupported);
  assert(photo_filter_status(filters, Api::Unknown) == PhotoFilterStatus::Unsupported);
  assert(make_filtered_photo_search(16, filters, {}, Api::Legacy, {}, 1, false, {}, 0).path.empty());
  uint8_t group_index = 0;
  int album_index = 0;
  const auto first = next_photo_filter_branch(filters, Api::Structured, group_index, album_index);
  const auto second = next_photo_filter_branch(filters, Api::Structured, group_index, album_index);
  assert(first.album_ids == album.album_id && second.album_ids == "0406c468-dabf-4777-bb93-ac6fdac62857");
  const auto filtered = make_filtered_photo_search(16, filters, first, Api::Structured, {}, 1, false, march, 0);
  assert(filtered.path == "/search/random" && !filtered.paged && filtered.dates == march);
  for (const auto value : {
           R"("albumIds":{"any":["c48e2980-18ac-4d7d-876a-a636759a1aec"]})",
           R"("personIds":{"any":["eb492926-9046-408b-be47-a959620753bb"]})",
           R"("tagIds":{"none":["4aa2c696-c8d4-46c2-8dc2-3eda9087fb32"]})",
           R"("isFavorite":{"eq":false})", R"("rating":{"gte":4})", R"("city":{"eq":"Ville \"Nouvelle\""})",
       }) {
    assert(filtered.body.find(value) != std::string::npos);
    const auto companion = make_filtered_photo_search(16, filters, first, Api::Structured, march_first, 1, true, march, 2);
    assert(companion.dates == march && companion.body.find(value) != std::string::npos);
    assert(companion.body.find(R"("withExif":true)") != std::string::npos);
  }
  assert(filtered.body.find("withExif") == std::string::npos);
  criteria.rating_enabled = criteria.tags_enabled = false;
  criteria.inclusion_matching = "Match any enabled group";
  const auto album_branch = next_photo_filter_branch(filters, Api::Legacy, group_index, album_index);
  const auto person_branch = next_photo_filter_branch(filters, Api::Legacy, group_index, album_index);
  assert(album_branch.group == "Album" && album_branch.person_ids.empty());
  assert(person_branch.group == "Person" && person_branch.album_ids.empty());
  const auto flat = make_filtered_photo_search(8, filters, album_branch, Api::Legacy, {}, 7, false, {}, 0);
  assert(flat.path == "/search/metadata" && flat.paged && flat.body.find(R"("page":7)") != std::string::npos);
  assert(flat.body.find(R"("isFavorite":false)") != std::string::npos);
  assert(flat.body.find("tagIds") == std::string::npos && flat.body.find("rating") == std::string::npos);
}
