#include <cassert>
#include <iostream>
#include <vector>
#include "components/espframe/photo_buffer_ownership.h"
#include "components/espframe/slideshow_component.h"

using namespace esphome::espframe;
constexpr size_t HALF_BYTES = 640 * 800 * 2;

struct TestImage {
  size_t capacity = HALF_BYTES;
  bool downloading = false;
  bool referenced = false;
  bool cache_valid = true;
  size_t get_buffer_capacity() const { return capacity; }
  bool is_downloading() const { return downloading; }
  void release() {
    assert(!downloading && !referenced && !cache_valid);
    capacity = 0;
  }
};
struct Bindings {
  bool prepare_release(TestImage *image, bool detach) {
    if (image->referenced && !detach) return false;
    image->referenced = false;
    image->cache_valid = false;
    return true;
  }
};
struct Fixture {
  SlideshowRuntimeState state;
  std::array<TestImage, 4> storage;
  Bindings binding;
  Fixture() { state.active_slot_displayed = true; }
  PhotoBufferReclaimResult reclaim(bool pending = false, bool queued = false, bool hidden = true) {
    return reclaim_portrait_buffers(portrait_buffer_owners(state, pending, queued, hidden),
      std::array<TestImage *, 4>{&storage[0], &storage[1], &storage[2], &storage[3]}, binding);
  }
};

static void test_stale_portraits_and_repeated_reclamation() {
  Fixture f;
  f.storage[0].referenced = f.storage[1].referenced = true;
  f.state.previous_display.valid = true;
  f.state.previous_display.image_url = "https://example.test/history";
  f.state.slot1.ready = f.state.slot2.ready = true;
  auto result = f.reclaim();
  assert(result.released_bytes == 4 * HALF_BYTES && result.retained_bytes == 0);
  assert(f.state.previous_display.image_url == "https://example.test/history");
  assert(f.state.slot1.ready && f.state.slot2.ready);
  assert(f.reclaim().released_bytes == 0);  // No double free.
  // Repeated portrait/landscape cycles acquire fresh content and release once.
  for (int cycle = 0; cycle < 100; ++cycle) {
    for (auto &image : f.storage) image = TestImage{};
    assert(f.reclaim().released_bytes == 4 * HALF_BYTES);
  }
}

static void test_display_and_prefetch_ownership() {
  Fixture active;
  active.state.portrait.is_pair = true;
  active.storage[0].referenced = active.storage[1].referenced = true;
  assert(active.reclaim(false, false, false).released_bytes == 2 * HALF_BYTES);
  assert(active.storage[0].capacity == HALF_BYTES && active.storage[1].capacity == HALF_BYTES);

  Fixture preload;
  preload.state.portrait.is_pair = preload.state.portrait.using_preload = true;
  // Real promotion clears these while leaving the preload images on screen.
  preload.state.portrait_preload_slot = -1;
  preload.storage[2].referenced = preload.storage[3].referenced = true;
  assert(preload.reclaim(false, false, false).released_bytes == 2 * HALF_BYTES);
  assert(preload.storage[2].capacity == HALF_BYTES && preload.storage[3].capacity == HALF_BYTES);

  Fixture ready;
  ready.state.portrait_preload_slot = 1;
  ready.state.portrait_preload_left_ready = ready.state.portrait_preload_right_ready = true;
  assert(ready.reclaim().released_bytes == 2 * HALF_BYTES);
  assert(ready.storage[2].capacity == HALF_BYTES && ready.storage[3].capacity == HALF_BYTES);

  Fixture source_still_visible;
  source_still_visible.storage[0].referenced = source_still_visible.storage[1].referenced = true;
  // Even stale/reset model flags cannot free a still-bound visible source.
  assert(source_still_visible.reclaim(false, false, false).released_bytes == 2 * HALF_BYTES);
}

static void test_blocking_owners() {
  for (int condition = 0; condition < 11; ++condition) {
    Fixture f;
    switch (condition) {
      case 0: f.state.active_slot_displayed = false; break;
      case 1: f.state.slot_flags.fetch_in_flight[0] = true; break;
      case 2: f.state.slot_flags.fetch_in_flight[1] = true; break;
      case 3: f.state.slot_flags.fetch_in_flight[2] = true; break;
      case 4: f.state.noncritical_remote_updates_in_flight = 1; break;
      case 5: f.state.portrait.workflow_busy = true; break;
      case 6: case 7: case 8: case 9: f.storage[condition - 6].downloading = true; break;
      case 10: break;
    }
    auto result = f.reclaim(condition == 10);
    assert(result.released_bytes == 0 && result.retained_bytes == 4 * HALF_BYTES);
  }
  Fixture queued;
  assert(queued.reclaim(false, true).released_bytes == 0);
  // Partial/retry state reserves the whole pair, not just its completed half.
  for (int condition = 0; condition < 9; ++condition) {
    Fixture f;
    switch (condition) {
      case 0: f.state.portrait.left_ready = true; break;
      case 1: f.state.portrait.right_ready = true; break;
      case 2: f.state.portrait.left_requested = true; break;
      case 3: f.state.portrait.right_requested = true; break;
      case 4: f.state.portrait.companion_found = true; break;
      case 5: f.state.portrait_preload_slot = 2; break;
      case 6: f.state.portrait_preload_left_ready = true; break;
      case 7: f.state.portrait_preload_right_ready = true; break;
      case 8: f.state.preload_noncritical_in_flight = true; break;
    }
    auto result = f.reclaim();
    assert(result.released_bytes == 2 * HALF_BYTES);
    assert(f.storage[condition < 5 ? 0 : 2].capacity == HALF_BYTES);
  }
}

int main() {
  test_stale_portraits_and_repeated_reclamation();
  test_display_and_prefetch_ownership();
  test_blocking_owners();
  std::cout << "Photo buffer ownership tests passed\n";
}
