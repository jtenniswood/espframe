#include "slide_cache.h"

#include <cassert>

using espframe::native::SlideCache;

int main() {
  uint16_t pixels[SlideCache::capacity]{};
  SlideCache::Buffers buffers{};
  for (size_t i = 0; i < buffers.size(); ++i)
    buffers[i] = &pixels[i];
  SlideCache cache{buffers};

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
}
