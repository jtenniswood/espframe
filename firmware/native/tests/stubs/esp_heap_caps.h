#pragma once

#include <cstdlib>

inline constexpr int MALLOC_CAP_SPIRAM = 0;
inline constexpr int MALLOC_CAP_8BIT = 1;
inline size_t free_spiram = 32 * 1024 * 1024;
inline bool fail_heap_allocations = false;
inline size_t heap_caps_get_free_size(int) { return free_spiram; }
inline void *heap_caps_malloc(size_t count, int) {
  return fail_heap_allocations ? nullptr : std::malloc(count);
}
inline void *heap_caps_aligned_alloc(size_t alignment, size_t count, int) {
  return fail_heap_allocations ? nullptr : std::aligned_alloc(alignment, count);
}
inline void heap_caps_free(void *pointer) { std::free(pointer); }
