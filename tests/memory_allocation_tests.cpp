#include "runtime.h"
#include "components/espframe/memory_diagnostics.h"
#include <cassert>
#include <iostream>
extern "C" void *__wrap_heap_caps_aligned_alloc(size_t, size_t, uint32_t);
static uint32_t observed_caps;
static void *result = &memory_test::external_buffer;
extern "C" void *__real_heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps) {
  assert(alignment == 64 && size == 256000);
  observed_caps = caps;
  return result;
}
void memory_test::log(const char *, ...) {}
int main() {
  using namespace memory_test;
  using namespace esphome::espframe;
  auto allocate = [](uint32_t caps) { return __wrap_heap_caps_aligned_alloc(64, 256000, caps); };
  MemorySetupProbe before(true), after(false);
  allocate(MALLOC_CAP_8BIT);
  assert(observed_caps == MALLOC_CAP_8BIT);
  before.setup();
  assert(allocate(MALLOC_CAP_8BIT) == result);
  assert(observed_caps == (MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
  for (auto caps : {MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, uint32_t(0x08)}) {
    allocate(caps);
    assert(observed_caps == caps);
  }
  task = &other_task;
  allocate(MALLOC_CAP_8BIT);
  assert(observed_caps == MALLOC_CAP_8BIT);
  task = &internal_buffer;
  result = nullptr;
  assert(allocate(MALLOC_CAP_8BIT) == nullptr);
  assert(observed_caps == (MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
  after.setup();
  allocate(MALLOC_CAP_8BIT);
  assert(observed_caps == MALLOC_CAP_8BIT);
  std::cout << "memory allocation policy tests passed\n";
}
