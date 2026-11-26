#include "memory_trace.h"

#if NATIVE_MEMORY_TRACE
#include "esp_heap_caps.h"
#include "esp_log.h"

namespace espframe::native {

void trace_memory(const char *stage) {
  multi_heap_info_t internal{};
  multi_heap_info_t external{};
  heap_caps_get_info(&internal, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  heap_caps_get_info(&external, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  ESP_LOGI("native-memory", "%s internal_used=%zu internal_free=%zu internal_min=%zu internal_largest=%zu "
                            "psram_used=%zu psram_free=%zu psram_min=%zu psram_largest=%zu",
           stage, internal.total_allocated_bytes, internal.total_free_bytes, internal.minimum_free_bytes,
           internal.largest_free_block, external.total_allocated_bytes, external.total_free_bytes,
           external.minimum_free_bytes, external.largest_free_block);
}

}  // namespace espframe::native
#endif
