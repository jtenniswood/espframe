#include "swipe_input.h"
#include "trial_swipe_benchmark.h"

#include <cassert>

using namespace espframe::native;

int main() {
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

  TrialSwipeMetrics timing;
  timing.gesture(12'000, {0, 0, 8'000, {CacheRead::Hit}, true, 1'000});
  timing.gesture(50'000, {0, 0, 6'000, {CacheRead::Hit}, false, 0});
  timing.gesture(50'001, {0, 0, 7'000, {CacheRead::Miss}, true, 3'000});
  timing.blit(true);
  timing.blit(false);
  timing.blit(true);
  constexpr auto period = 16'000;
  timing.frame(10'000, {1, MotionPhase::Drag, true}, period);
  timing.frame(26'000, {1, MotionPhase::Drag, true}, period);
  timing.frame(50'000, {1, MotionPhase::Drag, true}, period);
  timing.frame(82'000, {1, MotionPhase::Drag, false}, period);
  timing.frame(128'000, {2, MotionPhase::Drag, false}, period);  // New gesture.
  timing.frame(160'000, {3, MotionPhase::Settle, false}, period);
  const auto stats = timing.results();
  assert(stats.dma_copies == 2 && stats.cpu_copies == 1 && stats.worst_composition_us == 8'000);
  assert(stats.worst_before_submit_us == 7'000 && stats.worst_submit_us == 3'000);
  assert(stats.first_motion.count == 3 && stats.first_motion.worst_us == 50'001 && stats.first_motion.over_limit == 1);
  assert(stats.busy_first_motion.count == 2 && stats.busy_first_motion.over_limit == 1 && stats.cache_hits == 2);
  assert(stats.drag_gaps.count == 3 && stats.drag_gaps.worst_us == 32'000 && stats.drag_gaps.over_limit == 1);
  assert(stats.busy_drag_gaps.count == 2 && stats.busy_drag_gaps.worst_us == 24'000 && stats.busy_drag_gaps.over_limit == 0);
}
