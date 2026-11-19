#include "slide_cache.h"
#include "slide_prefetch.h"
#include "swipe.h"

#include <cassert>

using espframe::native::SlideCache;
using espframe::native::SlidePrefetch;
using espframe::native::SlideRange;
using espframe::native::SlideRetry;
using espframe::native::Swipe;
using espframe::native::slide_width;

int main() {
  const auto next_candidate_retry = SlideRetry::after(10, 1'000, 1, true);
  assert(!next_candidate_retry.due(10, 100'999, 1) && next_candidate_retry.due(10, 101'000, 1));
  const auto transport_retry = SlideRetry::after(10, 1'000, 1, false);
  assert(!transport_retry.due(10, 101'000, 1) && transport_retry.due(10, 2'001'000, 1));
  assert(transport_retry.due(10, 1'001, 2));

  const SlideRetry offline_retry{10, 2'000'000, 0};
  assert(!offline_retry.due(10, 1, 0));
  assert(offline_retry.due(10, 1, 1));
  const SlideRetry online_retry{10, 2'000'000, 1};
  assert(!online_retry.due(10, 1, 1) && !online_retry.due(10, 1, 0));
  assert(online_retry.due(10, 1, 3));
  assert(online_retry.due(10, 2'000'000, 1) && online_retry.due(11, 1, 1));

  constexpr SlideRange available{1, 100};
  SlidePrefetch prefetch{10};
  auto window = prefetch.window(available);
  assert(window.first == 8 && window.last == 13);
  assert(!SlidePrefetch{0}.can_defer_pixel_work({0, 1}));
  assert(!prefetch.can_defer_pixel_work({10, 10}));
  assert(!prefetch.can_defer_pixel_work({9, 10}) && prefetch.can_defer_pixel_work({10, 11}));

  SlidePrefetch deeper{10, SlideCache::capacity};
  assert(deeper.window(available).first == 8 && deeper.window(available).last == 16);
  assert(deeper.candidates(deeper.window(available))[9] == 16);
  deeper.moved_to(9);
  assert(deeper.window(available).first == 3 && deeper.window(available).last == 11);
  deeper.moved_to(1);
  assert(deeper.window(available).first == 1 && deeper.window(available).last == 9);
  assert(deeper.candidates(deeper.window(available))[9] == 9);

  uint16_t pixels[SlideCache::minimum_capacity]{};
  SlideCache::Buffers buffers{};
  for (size_t i = 0; i < SlideCache::minimum_capacity; ++i)
    buffers[i] = &pixels[i];
  SlideCache cache{buffers};
  for (int slide = 8; slide <= 12; ++slide) {
    const auto lease = cache.reserve(slide, window.first, window.last);
    assert(lease && cache.complete(*lease, true));
  }
  const auto speculative = cache.reserve(13, window.first, window.last);
  assert(speculative);

  // Take over a backward move near the outer ready slide and drag beyond it.
  Swipe swipe{slide_width, 101, 10};
  swipe.set_available_range(available.first, available.last);
  swipe.set_ready_range(8, 12);
  swipe.press(150, 200, 9 * slide_width, 5'000);
  swipe.move(151, 200, 10'000);
  assert(!swipe.dragging());
  prefetch.request_at_edge(swipe.position(), {8, 12}, available);
  assert(!prefetch.requested());

  // Start a drag at the edge of a shorter ready range. The real photo
  // requested by the edge takes precedence over speculative work.
  swipe.cancel(15'000);
  swipe.tick(300'000);
  swipe.set_ready_range(10, 12);
  swipe.press(300, 200, 10 * slide_width, 305'000);
  swipe.move(900, 200, 310'000);
  prefetch.request_at_edge(swipe.position(), {10, 12}, available);
  assert(prefetch.requested() == 9 && prefetch.candidates(prefetch.window(available))[1] == 9);
  assert(!prefetch.can_defer_pixel_work({10, 12}));

  prefetch.prepared(9);
  assert(prefetch.can_defer_pixel_work({9, 12}));
  prefetch.request_at_edge(8 * slide_width - 20, {8, 12}, available);
  assert(prefetch.requested() == 7 && prefetch.candidates(prefetch.window(available))[1] == 7);
  assert(prefetch.can_yield(13, 7));
  assert(!prefetch.can_yield(10, 7) && !prefetch.can_yield(7, 7));
  window = prefetch.window(available);
  assert(window.first == 7 && window.last == 12);
  assert(!cache.reserve(7, window.first, window.last));
  assert(cache.complete(*speculative, false));
  const auto requested = cache.reserve(*prefetch.requested(), window.first, window.last);
  assert(requested && cache.complete(*requested, true));
  assert(cache.find(8) && cache.find(10) && cache.find(12));
  prefetch.prepared(7);
  assert(!prefetch.requested() && prefetch.window(available).first == 7);

  // The previous photo can finish prefetching after an intentional release.
  const auto held_edge = swipe.position();
  swipe.release(320'000);
  swipe.tick(500'000);
  assert(swipe.loading_direction(500'000) == -1 && swipe.position() == held_edge);
  swipe.set_ready_range(7, 12);
  swipe.tick(600'000);
  assert(swipe.settling() && swipe.position() == held_edge);
  swipe.tick(900'000);
  assert(swipe.idle() && swipe.settled() == 9);
  prefetch.moved_to(swipe.settled());

  prefetch.request_at_edge(12 * slide_width + 20, {7, 12}, available);
  assert(prefetch.requested() == 13);
  prefetch.moved_to(11);
  assert(!prefetch.requested() && prefetch.candidates(prefetch.window(available))[2] == 12);
  prefetch.request_at_edge(slide_width - 20, {1, 100}, available);
  prefetch.request_at_edge(100 * slide_width + 20, {1, 100}, available);
  assert(!prefetch.requested());
}
