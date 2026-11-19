#include "photo_history.h"

#include <cassert>
#include <cstdio>

using namespace espframe::native;

AssetId photo(SlideIndex slide) {
  AssetId id{};
  std::snprintf(id.data(), id.size(), "photo-%lld", static_cast<long long>(slide));
  return id;
}

int main() {
  PhotoHistory history;
  assert(history.first() == 1 && history.next() == 1);
  assert(history.find(0) == nullptr && history.find(1) == nullptr);

  // Advance well past the old autoplay limit and wrap the ring many times.
  for (SlideIndex slide = 1; slide <= 5'000; ++slide) {
    assert(history.next() == slide);
    history.append({photo(slide), {}});
    assert(history.first() == std::max(SlideIndex{1}, slide - PhotoHistory::capacity + 1));
    assert(history.find(history.first() - 1) == nullptr);
    assert(history.find(history.next()) == nullptr);
    assert(history.find(history.first())->primary == photo(history.first()));
    assert(history.find(slide)->primary == photo(slide));
  }

  assert(history.recently_seen(photo(5'000), 16));
  assert(history.recently_seen(photo(4'985), 16));
  assert(!history.recently_seen(photo(4'984), 16));
  assert(!history.recently_seen(photo(history.first() - 1), 200));
  // A preview removed from Immich can be replaced in place without renumbering.
  *history.find(4'999) = {};
  assert(!history.recently_seen(photo(4'999), 16));
  *history.find(4'999) = {photo(2), photo(3)};
  assert(history.recently_seen(photo(2), 16));
  assert(history.recently_seen(photo(3), 16));
  assert(history.next() == 5'001 && history.find(5'000)->primary == photo(5'000));

  history.start_at(4'000);
  assert(history.first() == 4'000 && history.next() == 4'000);
  assert(history.find(3'999) == nullptr && history.find(4'000) == nullptr);
  assert(!history.recently_seen(photo(5'000), PhotoHistory::capacity));
  history.append({photo(100), {}});
  assert(history.next() == 4'001 && history.find(4'000)->primary == photo(100));
}
