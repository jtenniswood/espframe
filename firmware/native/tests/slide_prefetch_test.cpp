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
  assert(!SlidePrefetch{0}.can_defer_pixel_work({0, 1}, true, 0));
  assert(!prefetch.can_defer_pixel_work({10, 10}, true, 10));
  assert(!prefetch.can_defer_pixel_work({9, 10}, true, 10) && prefetch.can_defer_pixel_work({10, 11}, true, 10));
  assert(!prefetch.can_defer_pixel_work({10, 11}, false, 10) && prefetch.can_defer_pixel_work({10, 12}, false, 10));
  const auto forward = prefetch.candidates(window);
  // A missing slide 9 must not delay preparing 12 for the next forward swipe.
  assert(forward[2] == 11 && forward[3] == 12 && forward[4] == 9);

  SlidePrefetch deeper{10, SlideCache::capacity};
  assert(deeper.window(available).first == 8 && deeper.window(available).last == 16);
  assert(deeper.candidates(deeper.window(available))[9] == 16);
  deeper.moved_to(9);
  assert(deeper.window(available).first == 3 && deeper.window(available).last == 11);
  const auto backward = deeper.candidates(deeper.window(available));
  assert(backward[2] == 8 && backward[3] == 7 && backward[4] == 10);
  assert(deeper.can_defer_pixel_work({8, 11}, true, 9) && !deeper.can_defer_pixel_work({8, 11}, false, 9));
  assert(deeper.can_defer_pixel_work({7, 11}, false, 9));
  deeper.moved_to(1);
  assert(deeper.window(available).first == 1 && deeper.window(available).last == 9);
  assert(deeper.candidates(deeper.window(available))[9] == 9);

  Swipe landing{slide_width, 101, 10};
  landing.set_ready_range(8, 12);
  landing.press(850, 200, 10 * slide_width, 10'000);
  landing.move(250, 200, 30'000);
  assert(landing.dragging() && landing.settle_destination() == 10);
  assert(prefetch.can_defer_pixel_work({8, 12}, true, landing.settle_destination()));
  landing.release(35'000);
  assert(landing.settling() && landing.settle_destination() == 11);
  assert(!prefetch.can_defer_pixel_work({8, 12}, false, landing.settle_destination()));
  assert(prefetch.can_defer_pixel_work({8, 13}, false, landing.settle_destination()));

  // A reversal counts the ready photos behind the backward landing.
  landing.show(10);
  landing.press(250, 200, 10 * slide_width, 40'000);
  landing.move(850, 200, 60'000);
  landing.release(65'000);
  assert(landing.settling() && landing.settle_destination() == 9);
  assert(!prefetch.can_defer_pixel_work({8, 13}, false, landing.settle_destination()));
  assert(prefetch.can_defer_pixel_work({7, 13}, false, landing.settle_destination()));

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
  assert(!prefetch.can_defer_pixel_work({10, 12}, true, 10));
  assert(!prefetch.can_defer_pixel_work({10, 12}, false, 10));

  prefetch.prepared(9);
  assert(prefetch.can_defer_pixel_work({9, 12}, true, 10));
  prefetch.request_at_edge(8 * slide_width - 20, {8, 12}, available);
  assert(prefetch.requested() == 7 && prefetch.candidates(prefetch.window(available))[1] == 7);
  assert(prefetch.can_yield(13, 7));
  assert(!prefetch.can_yield(10, 7) && !prefetch.can_yield(7, 7));
  assert(prefetch.can_yield(10, 7, true) && !prefetch.can_yield(7, 7, true));
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

  // A reversal shifts the refill window, but the ready photos beyond it
  // still belong to the current feed and can be used by a touch takeover.
  uint16_t extra_pixels[SlideCache::capacity]{};
  for (size_t slot = 0; slot < buffers.size(); ++slot)
    buffers[slot] = &extra_pixels[slot];
  SlideCache wide_cache{buffers};
  for (int slide = 18; slide >= 10; --slide) {
    const auto lease = wide_cache.reserve(slide, 10, 18);
    assert(lease && wide_cache.complete(*lease, true));
  }
  SlidePrefetch reverse_window{12, SlideCache::capacity};
  reverse_window.moved_to(11);
  const auto reverse_keep = reverse_window.window(available);
  assert(reverse_keep.first == 5 && reverse_keep.last == 13);
  const auto ready = wide_cache.ready_range(11, available);
  assert(ready.first == 10 && ready.last == 18);
  const auto bounded = wide_cache.ready_range(12, {12, 16});
  assert(bounded.first == 12 && bounded.last == 16);

  Swipe takeover{slide_width, 101, 11};
  takeover.set_ready_range(ready.first, ready.last);
  const auto presented = 14 * slide_width + 3 * slide_width / 4;
  const auto before_press = takeover.retain_for_motion(reverse_keep, presented);
  assert(before_press.first == 5 && before_press.last == 15);
  takeover.press(1'020, 200, presented, 1'000'000);
  takeover.move(250, 200, 1'150'000);
  assert(takeover.dragging() && takeover.origin_slide() == 15);
  reverse_window.request_at_edge(takeover.position(), ready, available);
  assert(!reverse_window.requested());
  const auto retain = takeover.retain_for_motion(reverse_keep, presented);
  assert(retain.first == 5 && retain.last == 16);
  const auto refill = wide_cache.reserve(6, retain.first, retain.last);
  assert(refill && !wide_cache.find(18) && wide_cache.find(17) && wide_cache.find(16));
  const auto reduced = wide_cache.ready_range(11, available);
  assert(reduced.first == 10 && reduced.last == 17);
  takeover.set_ready_range(reduced.first, reduced.last);
  takeover.release(1'260'000);
  assert(takeover.settle_destination() == 16);
  assert(takeover.retain_for_motion(reverse_keep, presented).last == 16);
  takeover.tick(1'510'000);
  assert(takeover.idle() && takeover.settled() == 16);
}
