#include "runtime.h"
#include "components/espframe/memory_diagnostics.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <iostream>

extern "C" void *__wrap_heap_caps_aligned_alloc(size_t, size_t, uint32_t);
static void *allocation_result = nullptr;
static unsigned allocation_calls = 0;
static uint32_t last_caps = 0;
static bool reenter = false;
extern "C" void *__real_heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps) {
  assert(alignment == 64 && size == 256000);
  last_caps = caps;
  allocation_calls++;
  return allocation_result;
}
void memory_test::log(const char *format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  logs.emplace_back(buffer);
  if (reenter) {
    reenter = false;
    __wrap_heap_caps_aligned_alloc(64, 256000, MALLOC_CAP_8BIT);
  }
}
static bool logged(const std::string &text) {
  for (const auto &entry : memory_test::logs) if (entry.find(text) != std::string::npos) return true;
  return false;
}
static void *allocate() { return __wrap_heap_caps_aligned_alloc(64, 256000, MALLOC_CAP_8BIT); }
int main() {
  using namespace memory_test;
  using namespace esphome::espframe;
  MemorySetupProbe before(true), after(false);
  assert(before.get_setup_priority() > 400 && after.get_setup_priority() < 400);
  allocation_result = &internal_buffer;
  assert(allocate() == allocation_result && allocation_calls == 1 && logs.empty());
  assert(last_caps == MALLOC_CAP_8BIT);
  before.setup();
  logs.clear();
  assert(allocate() == allocation_result && logged("region=internal"));
  assert(last_caps == (MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
  logs.clear();
  allocation_result = &external_buffer;
  reenter = true;
  assert(allocate() == allocation_result && logs.size() == 1 && logged("region=psram"));
  logs.clear();
  allocation_result = nullptr;
  assert(allocate() == nullptr && logged("allocation-failed"));
  logs.clear();
  task = &other_task;
  allocate();
  assert(logs.empty() && last_caps == MALLOC_CAP_8BIT);
  task = &internal_buffer;
  lv_draw_buf_t draw{&internal_buffer, 256000};
  lv_display_t display{&draw};
  test_display = &display;
  internal_free += 100;  // A net release must not underflow into a huge value.
  after.setup();
  assert(logged("internal_used_delta=-100"));
  assert(logged("draw-buffer") && logged("bytes=256000 region=internal"));
  assert(logged("task=loopTask minimum_free_bytes=2048"));
  logs.clear();
  allocate();
  assert(logs.empty());
  assert(last_caps == MALLOC_CAP_8BIT);
  test_display = nullptr;
  after.setup();
  assert(logged("No active LVGL draw buffer"));
  std::cout << "memory diagnostics tests passed\n";
}
