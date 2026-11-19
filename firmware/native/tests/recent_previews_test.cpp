#include "recent_previews.h"

#include <array>
#include <cassert>
#include <vector>

using namespace espframe::native;

AssetId id(uint8_t index) {
  AssetId result{};
  result[0] = static_cast<char>(index);
  return result;
}

int main() {
  RecentPreviews previews;
  std::vector<uint8_t> encoded(RecentPreviews::byte_budget / 3 + 1, 5);
  previews.remember(id(1), encoded);
  encoded[0] = 9;
  previews.remember(id(2), encoded);
  assert(previews.find(id(1)).front() == 5);
  previews.remember(id(3), encoded);
  // All three don't fit even though there are unused entry slots. Keep the
  // two most recently accessed, including the photo just replayed backwards.
  assert(previews.find(id(2)).empty());
  assert(previews.find(id(1)).front() == 5);
  assert(previews.find(id(3)).front() == 9);
  previews.discard(id(1));
  previews.discard(id(1));
  assert(previews.find(id(1)).empty());

  RecentPreviews small;
  const std::array<uint8_t, 3> tiny{8, 1, 0};
  for (uint8_t index = 1; index <= RecentPreviews::capacity; ++index)
    small.remember(id(index), tiny);
  assert(!small.find(id(1)).empty());
  small.remember(id(17), tiny);
  assert(small.find(id(2)).empty());
  assert(small.find(id(17)).size() == tiny.size());

  std::vector<uint8_t> oversize(RecentPreviews::byte_budget + 1);
  small.remember(id(18), oversize);
  assert(small.find(id(18)).empty() && !small.find(id(1)).empty());

  RecentPreviews mixed;
  constexpr SlideIndex slide = 1;
  mixed.remember(id(1), encoded);
  mixed.remember_prepared(slide, tiny);
  assert(mixed.find(id(1)).size() == encoded.size());
  assert(mixed.find(slide).size() == tiny.size());
  mixed.discard(id(1));
  assert(mixed.find(id(1)).empty() && mixed.find(slide).front() == 8);
  mixed.remember(id(1), encoded);
  mixed.discard(slide);
  assert(mixed.find(slide).empty() && mixed.find(id(1)).front() == 9);

  RecentPreviews prepared;
  std::vector<uint8_t> pair(RecentPreviews::prepared_byte_budget / 4 + 1, 12);
  for (SlideIndex position = 1; position <= 4; ++position)
    prepared.remember_prepared(position, pair);
  assert(prepared.find(SlideIndex{1}).empty());
  for (SlideIndex position = 2; position <= 4; ++position)
    assert(prepared.find(position).size() == pair.size());

  free_spiram = 2 * 1024 * 1024;
  small.remember(id(19), tiny);
  assert(small.find(id(19)).empty() && small.find(id(17)).empty());
}
