#pragma once

#include <cstdlib>

inline constexpr int MALLOC_CAP_SPIRAM = 0;
inline void *heap_caps_malloc(size_t count, int) { return std::malloc(count); }
inline void heap_caps_free(void *pointer) { std::free(pointer); }
