#include "swipe_input.h"
#include "trial_swipe_benchmark.h"
#include "trial_swipe_landing.h"

#include <cassert>

using namespace espframe::native;

namespace {

void test_wifi_outage() {
  using Script = TrialSwipeBenchmark;
  using Window = Script::WifiWindow;
  Script script{Script::Scenario::WifiOutage};
  Swipe swipe{slide_width, 64, 1};
  SwipeInput touch;
  const auto run = [&](int64_t start, int64_t end, Window wifi, SlideIndex last_ready) {
    swipe.set_ready_range(1, last_ready);
    for (auto now = start; now < end; now += 1'000) {
      if (const auto report = script.poll(now, swipe.settled(), {1, last_ready}, swipe.idle(), wifi))
        touch.update(swipe, *report, swipe.position());
      swipe.tick(now);
    }
  };

  run(0, 1'000'000, Window::Waiting, 5);
  assert(script.results().started == 0);
  run(1'000'000, 13'000'000, Window::Offline, 5);
  assert(script.results().offline_settled == Script::wifi_offline_moves);
  assert(script.results().settled == Script::wifi_offline_moves && swipe.settled() == 1);
  assert(script.results().offline_ready_last == 5 && script.results().reconnected_settled == 0);
  run(13'000'000, 15'000'000, Window::Waiting, 5);
  assert(script.results().started == Script::wifi_offline_moves);

  run(15'000'000, 22'000'000, Window::Reconnected, 5);
  assert(!script.finished() && swipe.settled() == 5);
  assert(script.results().reconnected_settled == 4);
  run(22'000'000, 30'000'000, Window::Reconnected, 13);
  assert(script.finished() && script.results().reconnected_settled == Script::wifi_reconnected_moves);
  assert(swipe.settled() == 13 && script.results().wrong_slide == 0);

  Script missed{Script::Scenario::WifiOutage};
  assert(missed.poll(100'000, 1, {1, 5}, true, Window::Offline));
  assert(missed.poll(300'000, 1, {1, 5}, false, Window::Offline));
  assert(!missed.poll(500'000, 2, {1, 5}, true, Window::Reconnected));
  assert(!missed.poll(1'000'000, 2, {1, 5}, true, Window::Reconnected));
  assert(missed.results().settled == 1 && missed.results().offline_settled == 0);
}

void test_physical_drag_outcomes() {
  Swipe swipe{slide_width, 5, 2};
  SwipeInput input;
  TrialSwipeLanding landing;
  using End = SwipeInput::DragEnd;
  uint32_t sequence{};
  const auto send = [&](int64_t time, int x, uint8_t contacts = 1) {
    End end;
    input.update(swipe, {x, 300, contacts, true, ++sequence, time}, swipe.position(), &end);
    return end;
  };
  const auto release = [&](int64_t time, int dx) {
    assert(send(time, 650) == End::None);
    assert(send(time + 180'000, 650 + dx) == End::None);
    assert(send(time + 200'000, 0, 0) == End::Released);
    landing.released(swipe.origin_slide());
  };

  release(1'000'000, -120);
  swipe.tick(1'500'000);
  assert(landing.presented(swipe.settled()) == TrialDragOutcome::Forward);
  assert(!landing.presented(swipe.settled()));

  release(2'000'000, 120);
  swipe.tick(2'500'000);
  assert(landing.presented(swipe.settled()) == TrialDragOutcome::Backward);

  release(3'000'000, -90);
  swipe.tick(3'500'000);
  assert(landing.presented(swipe.settled()) == TrialDragOutcome::Returned);

  assert(send(4'000'000, 650) == End::None);
  assert(send(4'100'000, 530) == End::None);
  ++sequence;  // A missing report on release cancels the drag.
  assert(send(4'110'000, 0, 0) == End::Cancelled);
  swipe.tick(4'400'000);
  assert(!landing.presented(swipe.settled()));

  release(5'000'000, -120);
  const auto previous_contact = input.contact_started_us();
  assert(send(5'215'000, 640) == End::None);
  assert(input.contact_started_us() != previous_contact && landing.interrupt());
  assert(!landing.interrupt());
  assert(send(5'225'000, 0, 0) == End::None);
  swipe.tick(5'500'000);
  assert(!landing.presented(swipe.settled()));
}

}  // namespace

int main() {
  test_physical_drag_outcomes();
  test_wifi_outage();
  constexpr int64_t start = 500'000;
  TrialSwipeBenchmark script;
  assert(!script.poll(start, 0, {0, 1}, true));
  assert(!script.poll(start, 1, {1, 1}, true));
  assert(!script.poll(start, 1, {1, 2}, false));
  const auto press = script.poll(start, 1, {1, 2}, true);
  assert(press && press->contacts == 1 && press->valid && press->sequence == 1);
  assert(!script.poll(start + TrialSwipeBenchmark::report_period_us - 1, 1, {1, 2}, false));
  const auto move = script.poll(start + 20'000, 1, {1, 2}, false);
  assert(move && move->sequence == 2 && move->x < press->x && move->sampled_us == start + 20'000);
  assert(!script.poll(start + 20'000, 1, {1, 2}, false));  // No catch-up burst.
  const auto release = script.poll(start + TrialSwipeBenchmark::drag_us, 1, {1, 2}, false);
  assert(release && release->contacts == 0);
  assert(!script.poll(start + 300'000, 1, {1, 2}, true));
  assert(script.results().settled == 1 && script.results().wrong_slide == 1);

  TrialSwipeBenchmark full_run;
  Swipe swipe{slide_width, 64, 1};
  SwipeInput input;
  constexpr SlideRange ready{1, 63};
  swipe.set_ready_range(ready.first, ready.last);
  unsigned first_movements = 0;
  for (auto now = start; now < start + 60'000'000; now += 1'000) {
    if (const auto report = full_run.poll(now, swipe.settled(), ready, swipe.idle())) {
      input.update(swipe, *report, swipe.position());
      if (input.take_first_motion() != 0)
        ++first_movements;
    }
    swipe.tick(now);
  }
  const auto full = full_run.results();
  assert(full.started == 48 && full.settled == 48 && full.wrong_slide == 0);
  assert(swipe.settled() == 17 && first_movements == full.started);

  TrialSwipeBenchmark history{TrialSwipeBenchmark::Scenario::History};
  Swipe rewind{slide_width, 64, 1};
  SwipeInput rewind_touch;
  rewind.set_ready_range(ready.first, ready.last);
  SlideIndex furthest = 1;
  for (auto now = start; now < start + 20'000'000; now += 1'000) {
    if (const auto report = history.poll(now, rewind.settled(), ready, rewind.idle()))
      rewind_touch.update(rewind, *report, rewind.position());
    rewind.tick(now);
    furthest = std::max(furthest, rewind.settled());
  }
  assert(furthest == 10 && rewind.settled() == 1);
  assert(history.results().settled == history.target() && history.results().settled_slide == 1 &&
         history.results().wrong_slide == 0);

  const auto run_cold = [&](int64_t ready_after, SlideIndex expected) {
    TrialSwipeBenchmark rapid{TrialSwipeBenchmark::Scenario::Rapid};
    Swipe player{slide_width, 64, 1};
    SwipeInput touch;
    for (auto now = start; now < start + 1'400'000 && rapid.results().settled == 0; now += 1'000) {
      const SlideRange available{1, now - start >= ready_after ? 2 : 1};
      player.set_ready_range(available.first, available.last);
      if (const auto report = rapid.poll(now, player.settled(), available, player.idle()))
        touch.update(player, *report, player.position());
      player.tick(now);
    }
    assert(player.settled() == expected);
    assert(rapid.results().started == 1 && rapid.results().settled == 1);
    assert(rapid.results().cold_starts == 1 && rapid.results().wrong_slide == 0);
    if (rapid.results().refill.count == 0)
      assert(!rapid.poll(start + ready_after, player.settled(), {1, 2}, false));
    assert(rapid.results().refill.count == 1 && rapid.results().refill.worst_us == ready_after);
    return rapid.results();
  };
  // A refill after release leaves the original slide in place.
  const auto late = run_cold(280'000, 1);
  assert(late.cold_releases_unready == 1 && late.cold_returns == 1 && late.cold_release_advances == 0);
  const auto expired = run_cold(1'000'000, 1);
  assert(expired.cold_releases_unready == 1 && expired.cold_returns == 1 && expired.cold_release_advances == 0);
  // A refill early in the drag leaves enough travel to advance.
  const auto early = run_cold(48'000, 2);
  assert(early.cold_releases_unready == 0 && early.cold_returns == 0);

  TrialSwipeBenchmark repeated{TrialSwipeBenchmark::Scenario::Rapid};
  assert(repeated.poll(start, 1, {1, 1}, true));
  const auto released_at = start + TrialSwipeBenchmark::drag_us;
  assert(repeated.poll(released_at, 1, {1, 1}, false)->contacts == 0);
  const auto settled_at = released_at + 130'000;
  assert(!repeated.poll(settled_at, 1, {1, 1}, true));
  assert(repeated.poll(settled_at + TrialSwipeBenchmark::rapid_rest_us, 1, {1, 1}, true));
  repeated.poll(start + 600'000, 1, {1, 2}, false);
  assert(repeated.results().cold_starts == 2 && repeated.results().refill.count == 1);
  assert(repeated.results().refill.worst_us == 600'000 && repeated.results().refill.over_limit == 1);
  TrialSwipeBenchmark last_cold{TrialSwipeBenchmark::Scenario::Rapid};
  auto now = start;
  for (unsigned i = 0; i < last_cold.target(); ++i) {
    assert(last_cold.poll(now, 1, {1, 1}, true));
    now += TrialSwipeBenchmark::drag_us;
    assert(last_cold.poll(now, 1, {1, 1}, false)->contacts == 0);
    assert(!last_cold.poll(++now, 1, {1, 1}, true));
    now += TrialSwipeBenchmark::rapid_rest_us;
  }
  assert(last_cold.finished() && last_cold.results().cold_returns == 48);
  assert(!last_cold.poll(now, 1, {1, 2}, true));
  assert(last_cold.results().refill.count == 0);  // A later download is outside the run.

  DragFrameDemand demand;
  demand.observe(1'000, true, 120, 120);  // A hold or rounded edge position needs no new frame.
  assert(demand.take() == 0);
  demand.observe(2'000, true, 121, 120);
  demand.observe(3'000, true, 123, 120);  // Keep the earliest unsatisfied update.
  assert(demand.take() == 2'000 && demand.take() == 0);
  demand.observe(4'000, true, 124, 123);
  demand.observe(5'000, true, 123, 123);  // Reversal back to the queued position clears demand.
  assert(demand.take() == 0);
  demand.observe(6'000, true, 124, 123);
  demand.observe(7'000, false, 125, 123);  // A release ends the drag measurement.
  assert(demand.take() == 0);
  demand.observe(8'000, true, 124, 123);
  assert(demand.take() == 8'000);

  TrialSwipeMetrics timing;
  timing.gesture(12'000, {0, 0, 8'000, {CacheRead::Hit}, true, 1'000});
  timing.gesture(50'000, {0, 0, 6'000, {CacheRead::Hit}, false, 0});
  timing.gesture(50'001, {0, 0, 7'000, {CacheRead::Miss}, true, 3'000});
  timing.gesture(15'000, {0, 0, 6'000, {CacheRead::Edge}, false, 0});
  timing.blit(true);
  timing.blit(false);
  timing.blit(true);
  timing.split_blit(13'000, false);
  timing.split_blit(14'000, true);
  constexpr auto period = 16'000;
  timing.frame(10'000, {1, MotionPhase::Drag, true}, period, 25, 2'000);
  timing.frame(26'000, {1, MotionPhase::Drag, true}, period, 250, 0);
  timing.frame(50'000, {1, MotionPhase::Drag, true}, period, 251, 25'000);
  timing.frame(82'000, {1, MotionPhase::Drag, false, true}, period, 990, 60'000);
  timing.frame(106'000, {1, MotionPhase::Drag, false, true}, period, 30, 82'000);
  timing.frame(128'000, {2, MotionPhase::Drag, false}, period, 30, 104'000);  // New gesture.
  timing.frame(160'000, {3, MotionPhase::Settle, false}, period, 2'000, 132'000);
  timing.frame(176'000, {3, MotionPhase::Settle, false, false, true}, period, 25, 0);
  timing.frame(208'000, {3, MotionPhase::Settle, true, true}, period, 25, 0);
  timing.frame(232'000, {3, MotionPhase::Settle, true, true}, period, 25, 0);
  // A later release may use the same phase but belongs to a new gesture.
  timing.frame(272'000, {5, MotionPhase::Settle, false, true}, period, 25, 0);
  timing.frame(305'000, {6, MotionPhase::Crossfade, false}, period, 25, 0);
  const auto stats = timing.results();
  assert(stats.dma_copies == 2 && stats.cpu_copies == 1 && stats.worst_composition_us == 8'000);
  assert(stats.split_blits == 2 && stats.batched_split_blits == 1 && stats.worst_split_blit_us == 14'000 &&
         stats.total_split_blit_us == 27'000);
  assert(stats.worst_before_submit_us == 7'000 && stats.worst_submit_us == 3'000);
  assert(stats.first_motion.count == 4 && stats.first_motion.worst_us == 50'001 && stats.first_motion.over_limit == 1);
  assert(stats.cache_miss_motion.count == 1 && stats.cache_miss_motion.worst_us == 50'001);
  assert(stats.busy_first_motion.count == 2 && stats.busy_first_motion.over_limit == 1 && stats.cache_hits == 2);
  assert(stats.quiet_cache_hit_motion.count == 1 && stats.quiet_cache_hit_motion.worst_us == 50'000 &&
         stats.quiet_cache_hit_motion.over_limit == 0);
  assert(stats.busy_cache_hit_motion.count == 1 && stats.busy_cache_hit_motion.worst_us == 12'000 &&
         stats.busy_cache_hit_motion.over_limit == 0);
  assert(stats.drag_gaps.count == 4 && stats.drag_gaps.worst_us == 32'000 && stats.drag_gaps.over_limit == 1);
  assert(stats.quiet_drag_gaps.count == 1 && stats.quiet_drag_gaps.worst_us == 24'000 && stats.quiet_drag_gaps.over_limit == 0);
  assert(stats.busy_drag_gaps.count == 2 && stats.busy_drag_gaps.worst_us == 24'000 && stats.busy_drag_gaps.over_limit == 0);
  assert(stats.in_range_drag_gaps.count == 2 && stats.in_range_drag_gaps.worst_us == 24'000 &&
         stats.in_range_drag_gaps.over_limit == 0);
  assert(stats.resisted_drag_gaps.count == 1 && stats.resisted_drag_gaps.worst_us == 24'000 &&
         stats.resisted_drag_gaps.over_limit == 0);
  assert(stats.settle_gaps.count == 3 && stats.settle_gaps.worst_us == 32'000 && stats.settle_gaps.over_limit == 1);
  assert(stats.in_range_settle_gaps.count == 1 && stats.in_range_settle_gaps.worst_us == 16'000 &&
         stats.in_range_settle_gaps.over_limit == 0);
  assert(stats.resisted_settle_gaps.count == 1 && stats.resisted_settle_gaps.worst_us == 24'000 &&
         stats.resisted_settle_gaps.over_limit == 0);
  assert(stats.landing_gaps.count == 1 && stats.landing_gaps.worst_us == 16'000 && stats.landing_gaps.over_limit == 0);
  assert(stats.approach_gaps.count == 2 && stats.approach_gaps.worst_us == 32'000 && stats.approach_gaps.over_limit == 1);
  assert(stats.drag_handoff_delay.count == 6 && stats.drag_handoff_delay.worst_us == 990 &&
         stats.drag_handoff_delay.over_limit == 2);
  assert(stats.in_range_drag_updates.count == 3 && stats.in_range_drag_updates.worst_us == 25'000 &&
         stats.in_range_drag_updates.over_limit == 1);
  assert(stats.resisted_drag_updates.count == 2 && stats.resisted_drag_updates.worst_us == 24'000 &&
         stats.resisted_drag_updates.over_limit == 0);
}
