#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include "components/espframe/immich_helpers.h"

static std::string fixture(const char *name) {
  std::ifstream input(std::string("tests/fixtures/immich/") + name + ".json");
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int main() {
  const std::string base = "https://immich.example.test";
  const std::string candidates = R"JSON([
    {"id":"primary","width":100,"height":200,"localDateTime":"2026-04-21T12:00:00"},
    {"id":"landscape","width":200,"height":100,"localDateTime":"2026-04-21T12:00:00"},
    {"id":"unknown-date","width":100,"height":200},
    {"id":"before","width":100,"height":200,"localDateTime":"2026-04-21T11:59:55"},
    {"id":"after","width":100,"height":200,"localDateTime":"2026-04-21T12:00:05"}
  ])JSON";
  auto companion = [&](const std::string &body, const std::string &date, uint32_t *next = nullptr) {
    return find_immich_portrait_companion_url(body, base, "primary", date, next);
  };
  const std::string suffix = "/thumbnail?size=preview";
  // A full first page can miss a companion that exists on a later cursor page.
  std::string first_page = "{\"assets\":{\"nextCursor\":\"opaque-page-2\",\"items\":[";
  for (int i = 0; i < 20; ++i) {
    if (i) first_page += ",";
    first_page += "{\"id\":\"landscape-" + std::to_string(i) + "\",\"width\":200,\"height\":100}";
  }
  first_page += "]}}";
  std::string cursor;
  uint32_t page = 99;
  assert(find_immich_portrait_companion_url(first_page, base, "primary", "", &page, &cursor).empty());
  assert(page == 0 && cursor == "opaque-page-2");
  const std::string second_page = "{\"assets\":{\"nextCursor\":null,\"items\":" + candidates + "}}";
  assert(!find_immich_portrait_companion_url(second_page, base, "primary", "", &page, &cursor).empty());
  assert(cursor.empty());
  for (const auto *invalid_cursor : {"42", "null", "{}"}) {
    cursor = "stale";
    find_immich_portrait_companion_url(std::string("{\"assets\":{\"items\":[],\"nextCursor\":") + invalid_cursor + "}}",
                                     base, "primary", "", nullptr, &cursor);
    assert(cursor.empty());
  }

  // Skip self/landscape, prefer a valid timestamp, and retain the first equal-distance match.
  assert(companion(candidates, "2026-04-21T12:00:00") == base + "/api/assets/before" + suffix);
  assert(companion(candidates, "invalid") == base + "/api/assets/unknown-date" + suffix);
  for (const auto *key : {"items", "assets"}) {
    uint32_t next = 0;
    assert(companion(std::string("{\"assets\":{\"nextPage\":\"3\",\"") + key + "\":" + candidates + "}}",
                     "2026-04-21T12:00:00", &next) == base + "/api/assets/before" + suffix);
    assert(next == 3);
  }
  std::string next_cursor;
  assert(find_immich_portrait_companion_url(
           "{\"assets\":{\"nextCursor\":\"cursor-2\",\"items\":" + candidates + "}}",
           base, "primary", "2026-04-21T12:00:00", nullptr, &next_cursor) ==
         base + "/api/assets/before" + suffix);
  assert(next_cursor == "cursor-2");
  assert(companion(R"JSON([{"id":"rotated","exifInfo":{"exifImageWidth":200,"exifImageHeight":100,"orientation":"6","dateTimeOriginal":"2026-04-21T12:00:00"}}])JSON",
                   "2026-04-21T12:00:00") == base + "/api/assets/rotated" + suffix);
  assert(companion("[]", "2026-04-21T12:00:00").empty());
  assert(companion("{", "2026-04-21T12:00:00").empty());
  const std::string portrait_id = "11111111-1111-4111-8111-111111111111";
  ImmichAssetMeta meta;
  assert(parse_immich_asset(fixture("asset-portrait"), base, &meta) ==
         base + "/api/assets/" + portrait_id + "/thumbnail?size=preview");
  assert(meta.asset_id == portrait_id && meta.is_portrait && meta.orientation_known);
  assert(meta.date == "21 April, 2026" && meta.location == "London, UK" && meta.person == "Alex");
  assert(!parse_immich_asset(fixture("random-assets"), base, &meta, "Portrait Only").empty());
  assert(meta.asset_id == portrait_id);
  for (const auto *shape : {"metadata-items", "metadata-assets"}) {
    auto body = fixture(shape);
    size_t count = 999;
    assert(parse_immich_metadata_item_count(body, &count) && count == 2);
    assert(!parse_immich_metadata_asset(body, base, &meta, "Portrait Only").empty());
    assert(meta.asset_id == portrait_id);
    assert(!parse_immich_metadata_asset(body, base, &meta, "Landscape Only").empty());
    assert(!meta.is_portrait && meta.date == "1 December, 2025");
  }
  size_t count = 999;
  assert(parse_immich_metadata_item_count(fixture("metadata-empty"), &count) && count == 0);
  assert(parse_immich_metadata_asset(fixture("metadata-empty"), base, &meta).empty());
  for (const auto *invalid : {"{", "null", "{}", "{\"assets\":{\"items\":42}}"}) {
    count = 999;
    assert(!parse_immich_metadata_item_count(invalid, &count) && count == 999);
    assert(parse_immich_metadata_asset(invalid, base, &meta).empty());
    assert(parse_immich_asset(invalid, base, &meta).empty());
  }
  assert(parse_immich_statistics_total("{\"images\":12}") == 12);
  assert(parse_immich_statistics_total("{\"total\":-1}") == 0);
  uint32_t album_count = 99;
  assert(parse_immich_album_asset_count("{\"assetCount\":0}", &album_count) && album_count == 0);
  assert(!parse_immich_album_asset_count("{\"assetCount\":\"0\"}", &album_count));
  assert(parse_immich_metadata_next_cursor(
           "{\"assets\":{\"nextCursor\":\"cursor-3\"}}") == "cursor-3");
  assert(parse_immich_metadata_next_cursor(
           "{\"assets\":{\"nextCursor\":null}}").empty());
  const std::string scoped_asset =
      "{\"assets\":{\"items\":[{\"id\":\"asset-1\",\"people\":[{\"id\":\"33333333-3333-4333-8333-333333333333\"},{\"id\":\"55555555-5555-4555-8555-555555555555\"}],\"tags\":[{\"id\":\"44444444-4444-4444-8444-444444444444\"}]}]}}";
  assert(immich_asset_filter_scope(
           scoped_asset, "asset-1",
           "33333333-3333-4333-8333-333333333333,55555555-5555-4555-8555-555555555555",
           "people") ==
         "33333333-3333-4333-8333-333333333333,55555555-5555-4555-8555-555555555555");
  assert(immich_asset_filter_scope(
           scoped_asset, "asset-1",
           "44444444-4444-4444-8444-444444444444,55555555-5555-4555-8555-555555555555",
           "tags") == "44444444-4444-4444-8444-444444444444");
  std::cout << "Immich production parser tests passed\n";
}
