#include "slide_cache.h"

#include <cassert>

using espframe::native::SlideCache;

int main() {
  uint16_t pixels[SlideCache::minimum_capacity]{};
  SlideCache::Buffers buffers{};
  for (size_t i = 0; i < SlideCache::minimum_capacity; ++i)
    buffers[i] = &pixels[i];
  SlideCache cache{buffers};
  assert(cache.size() == 6);

  auto first = cache.reserve(2, 0, 5);
  assert(first && cache.find(2) == nullptr);
  assert(!cache.reserve(2, 0, 5));
  *first->pixels = 42;
  assert(cache.complete(*first, true));
  assert(cache.find(2) == first->pixels && *cache.find(2) == 42);
  assert(!cache.complete(*first, false));

  for (int slide : {0, 1, 3, 4, 5}) {
    auto lease = cache.reserve(slide, 0, 5);
    assert(lease && cache.complete(*lease, true));
  }
  assert(!cache.reserve(6, 0, 5));

  auto shifted = cache.reserve(6, 1, 6);
  assert(shifted && cache.find(0) == nullptr && cache.find(1) != nullptr);
  assert(cache.find(6) == nullptr);

  // Moving the retention window cannot evict a buffer the worker is still
  // writing. Failed preparation returns that buffer to the empty pool.
  auto work = cache.reserve(7, 1, 6);
  assert(!work);
  assert(cache.complete(*shifted, false));
  auto retry = cache.reserve(6, 1, 6);
  assert(retry && retry->pixels == shifted->pixels);
  assert(!cache.complete(*shifted, true));
  assert(cache.find(6) == nullptr);
  assert(cache.complete(*retry, true));
  assert(cache.find(6) == retry->pixels);

  auto pending = cache.reserve(7, 2, 7);
  assert(pending);
  assert(!cache.reserve(8, 2, 6));
  assert(cache.complete(*pending, true));

  // Reversing direction shifts the extra prefetch slot behind the current
  // photo. Keep both adjacent slides readable during that refill.
  auto reversed = cache.reserve(1, 1, 6);
  assert(reversed && cache.find(7) == nullptr);
  assert(cache.find(3) && cache.find(4) && cache.find(5));
  assert(cache.complete(*reversed, true));

  // Reframing gets a different pixel buffer at the same index. Both
  // neighbors and the old crop remain available while the worker owns it.
  const auto *old_crop = cache.find(4);
  auto redraw = cache.reserve(4, 3, 5, true);
  assert(redraw && redraw->replacement && redraw->pixels != old_crop);
  assert(cache.find(4) == old_crop && cache.find(3) && cache.find(5));
  assert(!cache.reserve(4, 3, 5, true));
  assert(cache.complete(*redraw, false) && cache.find(4) == old_crop);
  auto new_crop = cache.reserve(4, 3, 5, true);
  assert(new_crop && !cache.complete(*redraw, true));
  *new_crop->pixels = 97;
  assert(cache.complete(*new_crop, true));
  assert(cache.find(4) == new_crop->pixels && *cache.find(4) == 97);
  assert(cache.find(3) && cache.find(5));

  // After retiring a feed, the current photo remains readable while its new
  // neighbor can reuse any released slot at the same logical index.
  const auto *anchor = cache.find(4);
  cache.discard_other_ready_slides(4);
  assert(cache.find(4) == anchor && !cache.find(3) && !cache.find(5));
  const auto replacement = cache.reserve(5, 4, 9);
  assert(replacement && cache.complete(*replacement, true));
  assert(cache.find(4) == anchor && cache.find(5) == replacement->pixels);

  uint16_t roomy_pixels[SlideCache::capacity]{};
  for (size_t slot = 0; slot < buffers.size(); ++slot)
    buffers[slot] = &roomy_pixels[slot];
  SlideCache roomy{buffers};
  assert(roomy.size() == 9);
  for (int slide = 0; slide < 9; ++slide) {
    const auto lease = roomy.reserve(slide, 0, 8);
    assert(lease && roomy.complete(*lease, true));
  }
  assert(!roomy.reserve(9, 0, 8));
  assert(roomy.reserve(9, 1, 9) && !roomy.find(0) && roomy.find(8));
}
