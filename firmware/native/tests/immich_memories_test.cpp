#include "immich_memories.h"

#include <cassert>

using namespace espframe::native;
using namespace std::chrono;

int main() {
  const auto leap = sys_days{year(2024) / 3 / 1};
  assert(memory_request_path(leap, 0) == "/memories?type=on_this_day&for=2024-03-01");
  assert(memory_request_path(leap, 1).ends_with("2024-02-29"));
  assert(memory_request_path(leap, 2).ends_with("2024-03-02"));
  const auto new_year = sys_days{year(2026) / 1 / 1};
  assert(memory_request_path(new_year, 13).ends_with("2025-12-25"));
  assert(memory_request_path(new_year, 14).ends_with("2026-01-08"));

  constexpr std::string_view fixture = R"json([
    {"type":"birthday","assets":[{"id":"c48e2980-18ac-4d7d-876a-a636759a1aed","type":"IMAGE"}]},
    {"assets":[
        {"type":"VIDEO","id":"eb492926-9046-408b-be47-a959620753bc"},
        {"type":"IMAGE","id":"c48e2980-18ac-4d7d-876a-a636759a1aec",
         "localDateTime":"2004-02-29T21:00:00.000Z","width":600,"height":800,
         "exifInfo":{"city":"Zürich","country":"Switzerland","description":"escaped \" } ]"}}
      ],"type":"on_this_day"},
    {"type":"on_this_day","assets":[
        {"id":"c48e2980-18ac-4d7d-876a-a636759a1aec","type":"IMAGE"},
        {"id":"eb492926-9046-408b-be47-a959620753bb","type":"IMAGE"},
        {"id":"bad-id","type":"IMAGE"}
      ]}
  ])json";
  const auto photos = parse_memories(fixture, 16);
  assert(photos && photos->size() == 2);
  const auto &portrait = photos->front();
  assert(portrait.portrait && portrait.metadata.capture_day == sys_days{year(2004) / 2 / 29});
  assert(std::string_view(portrait.metadata.city.data()) == "Zürich");
  assert(parse_memories(fixture, 1)->size() == 1);
  constexpr auto mixed = R"([{"type":"on_this_day","assets":[
    {"id":"c48e2980-18ac-4d7d-876a-a636759a1aec","type":"IMAGE","width":800,"height":600},
    {"id":"eb492926-9046-408b-be47-a959620753bb","type":"IMAGE","width":600,"height":800},
    {"id":"4aa2c696-c8d4-46c2-8dc2-3eda9087fb32","type":"IMAGE","width":800,"height":600,"isEdited":true}
  ]}])";
  const auto only_portrait = parse_memories(mixed, 1, PhotoOrientation::Portrait);
  assert(only_portrait && only_portrait->size() == 1 && only_portrait->front().portrait);
  const auto only_landscape = parse_memories(mixed, 3, PhotoOrientation::Landscape);
  assert(only_landscape && only_landscape->size() == 2 && !only_landscape->front().portrait);
  assert(!only_landscape->back().orientation_known);
  assert(parse_memories("[]", 16)->empty());
  assert(!parse_memories("[] null", 16));
  assert(!parse_memories(R"([{"type":"on_this_day","assets":null}])", 16));
  assert(!parse_memories(fixture.substr(0, fixture.size() - 2), 16));

  // A response can be bigger than a normal search batch. A single large or
  // deeply nested asset must not cause an unbounded parse allocation.
  const std::string large_asset = R"({"type":"IMAGE","note":")" + std::string(100 * 1024, 'a') + R"("})";
  const std::string response = R"([{"type":"on_this_day","assets":[)" + large_asset +
      R"(,{"id":"eb492926-9046-408b-be47-a959620753bb","type":"IMAGE"}]}])";
  assert(parse_memories(response, 16)->size() == 1);
  assert(!parse_memories(std::string(40, '[') + std::string(40, ']'), 16));
}
