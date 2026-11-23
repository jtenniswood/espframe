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
  const RecentPreviews::PreparedKey slide{id(1), id(2)};
  mixed.remember(id(1), encoded);
  mixed.remember_prepared(slide, tiny);
  const RecentPreviews::PreparedKey cropped{id(1), id(2), DisplayMode::Fill};
  const RecentPreviews::PreparedKey portrait{id(1), id(2), DisplayMode::Fit, PhotoOrientation::Portrait};
  const RecentPreviews::PreparedKey reversed{id(1), id(2), DisplayMode::Fit, PhotoOrientation::Any,
                                             ScreenRotation::UpsideDown};
  assert(mixed.find(reversed).empty());
  assert(mixed.find(cropped).empty() && mixed.find(portrait).empty());
  mixed.remember_prepared(cropped, std::array<uint8_t, 1>{42});
  assert(mixed.find(slide).front() == 8 && mixed.find(cropped).front() == 42);
  assert(mixed.find(id(1)).size() == encoded.size());
  assert(mixed.find(slide).size() == tiny.size());
  assert(mixed.find(RecentPreviews::PreparedKey{id(1), {}}).empty());
  assert(mixed.find(RecentPreviews::PreparedKey{id(2), id(1)}).empty());
  mixed.discard(id(1));
  assert(mixed.find(id(1)).empty() && mixed.find(slide).front() == 8);
  mixed.remember(id(1), encoded);
  mixed.discard(slide);
  assert(mixed.find(slide).empty() && mixed.find(id(1)).front() == 9);

  RecentPreviews prepared;
  std::vector<uint8_t> pair(RecentPreviews::prepared_byte_budget / 4 + 1, 12);
  for (uint8_t index = 1; index <= 4; ++index)
    prepared.remember_prepared({id(index), id(21)}, pair);
  assert(prepared.find(RecentPreviews::PreparedKey{id(1), id(21)}).empty());
  for (uint8_t index = 2; index <= 4; ++index)
    assert(prepared.find(RecentPreviews::PreparedKey{id(index), id(21)}).size() == pair.size());

  RecentPreviews interrupted;
  std::vector<uint8_t> download(RecentPreviews::byte_budget / 2, 8);
  interrupted.remember(id(1), download);
  interrupted.remember_interrupted(slide, download);
  download[0] = 7;  // The request buffer can be reused for the next transfer.
  assert(interrupted.find(slide).front() == 8);
  interrupted.remember_interrupted(id(2), download);
  assert(interrupted.find(id(1)).size() == download.size() && interrupted.find(id(2)).empty());
  interrupted.remember(id(3), download);
  assert(interrupted.find(slide).empty() && !interrupted.find(id(1)).empty());

  // Once a saved transfer decodes successfully it follows the ordinary LRU.
  interrupted.discard(id(3));
  interrupted.remember_interrupted(id(4), download);
  interrupted.remember(id(4), interrupted.find(id(4)));
  interrupted.remember(id(5), download);
  assert(interrupted.find(id(1)).empty() && !interrupted.find(id(4)).empty());

  small.remember_interrupted(id(20), tiny);
  assert(small.find(id(20)).empty() && !small.find(id(17)).empty());

  free_spiram = 2 * 1024 * 1024;
  mixed.remember_interrupted(slide, tiny);
  assert(mixed.find(slide).empty() && !mixed.find(id(1)).empty());
  small.remember(id(19), tiny);
  assert(small.find(id(19)).empty() && small.find(id(17)).empty());
}
