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
  std::cout << "Immich production parser tests passed\n";
}
