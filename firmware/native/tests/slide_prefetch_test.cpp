#include "slide_cache.h"
#include "slide_prefetch.h"
#include "swipe.h"

#include <cassert>

using espframe::native::SlideCache;
using espframe::native::SlideIndex;
using espframe::native::SlidePrefetch;
using espframe::native::SlideRange;
using espframe::native::SlideRetry;
using espframe::native::Swipe;
using espframe::native::slide_width;

void verify_drag_onset_guard() {
  constexpr SlideIndex center = 10;
  constexpr auto anchor = center * slide_width;
  const SlidePrefetch prefetch{center};
  assert(prefetch.can_defer_drag({9, 11}, center, anchor + 20, true, true));
  assert(!prefetch.can_defer_drag({9, 11}, center, anchor + 20, true, false));
  assert(prefetch.can_defer_drag({9, 12}, center, anchor + 20, true, false));
  assert(prefetch.can_defer_drag({9, 11}, center, anchor - 20, true, false));
}

void verify_refill_before_refresh() {
  uint16_t pixels[SlideCache::minimum_capacity]{};
  SlideCache::Buffers buffers{};
  for (size_t i = 0; i < SlideCache::minimum_capacity; ++i)
    buffers[i] = &pixels[i];
  SlideCache cache{buffers};
  for (const auto slide : {7, 9, 10, 11, 13, 14}) {
    const auto lease = cache.reserve(slide, 7, 14);
    assert(lease && cache.complete(*lease, true));
  }
  cache.mark_ready_for_refresh();
  SlidePrefetch prefetch{10};
  const auto forward = prefetch.nearby_window();
  assert(forward.first == 9 && forward.last == 12);
  const auto eligible = [&](SlideIndex slide) { return prefetch.window({1, 100}).contains(slide); };
  assert(prefetch.has_nearby_refill(cache, eligible));
  const auto in_backoff = [&](SlideIndex slide) { return eligible(slide) && slide != 12; };
  assert(!prefetch.has_nearby_refill(cache, in_backoff));

  // Fill the gap without first consuming time on a playable neighbour.
  const auto forward_cache = prefetch.window({1, 100});
  const auto refill = cache.reserve(12, forward_cache.first, forward_cache.last);
  assert(refill && cache.complete(*refill, true));
  assert(!prefetch.has_nearby_refill(cache, eligible));

  // Protect the playable headroom even when replacing the visible slide in
  // a full cache. The spare for the new crop comes from farther away.
  const auto refresh = cache.reserve(10, forward.first, forward.last, true);
  assert(refresh && cache.find(9) && cache.find(10) && cache.find(11) && cache.find(12));
  assert(cache.complete(*refresh, true) && cache.find(10) == refresh->pixels);

  prefetch.moved_to(11);
  prefetch.moved_to(10);
  const auto backward = prefetch.nearby_window();
  assert(backward.first == 8 && backward.last == 11);
  assert(prefetch.has_nearby_refill(cache, eligible));
}

void verify_fast_forward_refill() {
  constexpr SlideRange available{1, 100};
  constexpr SlideRange one_ahead{8, 11};
  constexpr int64_t now = 1'000;
  SlidePrefetch prefetch{10};
  SlidePrefetch startup{0};
  startup.note_drag(10, now);
  assert(!startup.should_stage_forward(1, {0, 0}, now));
  assert(!prefetch.should_stage_forward(12, one_ahead, now));
  prefetch.note_drag(10, now);
  assert(prefetch.should_stage_forward(12, one_ahead, now + 1'000'000));
  assert(!prefetch.should_stage_forward(7, one_ahead, now));
  assert(!prefetch.should_stage_forward(13, one_ahead, now));
  assert(!prefetch.should_stage_forward(13, {8, 12}, now));
  assert(!prefetch.should_stage_forward(12, one_ahead, now + 2'000'000));

  prefetch.moved_to(11);
  assert(prefetch.should_stage_forward(12, one_ahead, now + 1'000'000));
  prefetch.moved_to(10);
  assert(!prefetch.should_stage_forward(12, one_ahead, now + 1'000'000));
  prefetch.note_drag(10, now);
  prefetch.note_drag(-10, now + 1);
  assert(!prefetch.should_stage_forward(12, one_ahead, now + 2));

  prefetch.request_at_edge(11 * slide_width + 1, one_ahead, available);
  assert(prefetch.should_stage_forward(12, one_ahead, now + 2'000'000));
  prefetch.prepared(12);
  assert(!prefetch.should_stage_forward(12, one_ahead, now + 2'000'000));
  prefetch.request_at_edge(8 * slide_width - 1, one_ahead, available);
  assert(!prefetch.should_stage_forward(7, one_ahead, now + 2'000'000));
  assert(!prefetch.should_stage_forward(12, one_ahead, now + 2'000'000));
  assert(!prefetch.can_promote(7) && !prefetch.can_promote(12));

  prefetch.request_at_edge(11 * slide_width + 1, one_ahead, available);
  assert(prefetch.can_promote(12) && !prefetch.can_promote(13));
  prefetch.promoted(12);
  assert(!prefetch.can_promote(12));

  // The old full-preview attempt may finish cancelling after the drag pulls
  // back. Its retry should still be staged, without a second promotion.
  prefetch.request_at_edge(11 * slide_width - 1, one_ahead, available);
  assert(!prefetch.requested() && prefetch.should_stage_forward(12, one_ahead, now + 2'000'000));
  prefetch.request_at_edge(11 * slide_width + 1, one_ahead, available);
  assert(!prefetch.can_promote(12));
  prefetch.prepared(12);
  assert(!prefetch.should_stage_forward(12, one_ahead, now + 2'000'000));
}

int main() {
  verify_drag_onset_guard();
  verify_refill_before_refresh();
  verify_fast_forward_refill();
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
  assert(!prefetch.can_defer_settle({10, 11}, 10) && prefetch.can_defer_settle({10, 12}, 10));
  constexpr auto left_pull = 10 * slide_width - 20;
  constexpr auto right_pull = 10 * slide_width + 20;
  assert(prefetch.can_defer_drag({9, 10}, 10, left_pull, true));
  assert(!prefetch.can_defer_drag({9, 10}, 10, left_pull, false));
  assert(prefetch.can_defer_drag({8, 10}, 10, left_pull, false));
  assert(!prefetch.can_defer_drag({9, 10}, 10, right_pull, true));
  assert(prefetch.can_defer_drag({10, 11}, 10, right_pull, true));
  assert(!prefetch.can_defer_drag({10, 11}, 10, right_pull, false));
  assert(prefetch.can_defer_drag({10, 12}, 10, right_pull, false));
  assert(!SlidePrefetch{0}.can_defer_drag({0, 1}, 0, 20, true));
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
  assert(!deeper.can_defer_settle({8, 11}, 9));
  assert(deeper.can_defer_settle({7, 11}, 9));
  deeper.moved_to(1);
  assert(deeper.window(available).first == 1 && deeper.window(available).last == 9);
  assert(deeper.candidates(deeper.window(available))[9] == 9);

  Swipe landing{slide_width, 101, 10};
  landing.set_ready_range(8, 12);
  landing.press(850, 200, 10 * slide_width, 10'000);
  landing.move(250, 200, 30'000);
  assert(landing.dragging() && landing.settle_destination() == 10);
  assert(prefetch.can_defer_drag({8, 12}, landing.origin_slide(), landing.position(), false));
  landing.release(35'000);
  assert(landing.settling() && landing.settle_destination() == 11);
  assert(!prefetch.can_defer_settle({8, 12}, landing.settle_destination()));
  assert(prefetch.can_defer_settle({8, 13}, landing.settle_destination()));

  // A reversal counts the ready photos behind the backward landing.
  landing.show(10);
  landing.press(250, 200, 10 * slide_width, 40'000);
  landing.move(850, 200, 60'000);
  landing.release(65'000);
  assert(landing.settling() && landing.settle_destination() == 9);
  assert(!prefetch.can_defer_settle({8, 13}, landing.settle_destination()));
  assert(prefetch.can_defer_settle({7, 13}, landing.settle_destination()));

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
  assert(!prefetch.can_defer_settle({10, 12}, 10));

  prefetch.prepared(9);
  assert(prefetch.can_defer_drag({9, 12}, 10, left_pull, true));
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

  // The previous photo remains urgent while the drag snaps back and after
  // it settles. Its arrival allows the next swipe instead of moving this one.
  prefetch.request_at_edge(swipe.position(), {10, 12}, available);
  swipe.release(320'000);
  assert(swipe.settling() && swipe.settle_destination() == 10);
  assert(prefetch.requested() == 9 && !prefetch.can_defer_settle({10, 12}, 10));
  swipe.tick(500'000);
  assert(swipe.idle() && swipe.settled() == 10 && prefetch.requested() == 9);
  prefetch.prepared(9);
  swipe.set_ready_range(7, 12);
  swipe.tick(600'000);
  assert(swipe.idle() && swipe.settled() == 10 && !prefetch.requested());

  prefetch.request_at_edge(12 * slide_width + 20, {7, 12}, available);
  assert(prefetch.requested() == 13);
  prefetch.moved_to(11);
  assert(!prefetch.requested() && prefetch.candidates(prefetch.window(available))[2] == 12);
  prefetch.request_at_edge(slide_width - 20, {1, 100}, available);
  prefetch.request_at_edge(100 * slide_width + 20, {1, 100}, available);
  assert(!prefetch.requested());

  // Reversing a drag cancels the abandoned urgent request and immediately
  // lets the ready side drive decoder deferral.
  SlidePrefetch reverse_drag{10};
  Swipe active_drag{slide_width, 101, 10};
  active_drag.set_ready_range(10, 11);
  active_drag.press(500, 200, 10 * slide_width, 1'000'000);
  active_drag.move(900, 200, 1'120'000);
  reverse_drag.request_at_edge(active_drag.position(), {10, 11}, available);
  assert(reverse_drag.requested() == 9 && reverse_drag.window(available).first == 7);
  assert(!reverse_drag.can_defer_drag({10, 11}, 10, active_drag.position(), true));
  active_drag.move(100, 200, 1'240'000);
  reverse_drag.request_at_edge(active_drag.position(), {10, 11}, available);
  assert(!reverse_drag.requested());
  assert(reverse_drag.can_defer_drag({10, 11}, 10, active_drag.position(), true));
  active_drag.release(1'360'000);
  assert(active_drag.settle_destination() == 11);

  // If touch reports contain a whole drag and release, the returned photo
  // is still outside the ready range when the player starts the refill.
  for (const int distance : {60, 400}) {
    active_drag.show(10);
    active_drag.press(500, 200, 10 * slide_width, 3'000'000);
    active_drag.move(500 + distance, 200, 3'120'000);
    active_drag.release(3'240'000);
    assert(active_drag.settling());
    reverse_drag.request_at_edge(active_drag.position(), {10, 11}, available);
    assert(reverse_drag.requested() == 9);
    active_drag.tick(3'430'000);
    assert(active_drag.idle() && active_drag.settled() == 10 && reverse_drag.requested() == 9);
    reverse_drag.prepared(9);
  }

  // Pulling at the forward edge changes the priority, even after a rewind.
  SlidePrefetch forward_edge{11};
  forward_edge.moved_to(10);
  const SlideRange cold_edge{8, 10};
  forward_edge.request_at_edge(10 * slide_width + 20, cold_edge, available);
  assert(forward_edge.requested() == 11 && forward_edge.can_yield(8, 11));
  assert(forward_edge.requested() == 11 && !forward_edge.can_defer_settle(cold_edge, 10));
  const auto forward_order = forward_edge.candidates(forward_edge.window(available));
  assert(forward_order[1] == 11 && forward_order[2] == 11 && forward_order[3] == 12);
  forward_edge.prepared(11);
  assert(!forward_edge.requested() && forward_edge.can_defer_drag({8, 11}, 10, right_pull, true));
  forward_edge.request_at_edge(10 * slide_width + 20, cold_edge, {8, 10});
  assert(!forward_edge.requested());

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
  assert(reverse_window.can_defer_drag({15, 17}, takeover.origin_slide(), takeover.position(), true));
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
