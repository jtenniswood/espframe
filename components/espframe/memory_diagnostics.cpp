#include "memory_diagnostics.h"

#ifdef ESPFRAME_MEMORY_DIAGNOSTICS
#include <atomic>
#ifndef USE_LVGL
#error "Espframe memory_diagnostics requires LVGL"
#endif
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/log.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome::espframe {
namespace {
constexpr const char *TAG = "memory.lvgl";
std::atomic<TaskHandle_t> trace_task{nullptr};
size_t internal_before = 0;
size_t psram_before = 0;

const char *region(const void *ptr) {
  if (ptr == nullptr) return "allocation-failed";
  if (esp_ptr_external_ram(ptr)) return "psram";
  if (esp_ptr_internal(ptr)) return "internal";
  return "other";
}

void record_buffer(const char *kind, const void *ptr, size_t size, uint32_t caps) {
  ESP_LOGI(TAG, "%s address=%p bytes=%u region=%s requested_caps=0x%08x", kind, ptr,
           (unsigned) size, region(ptr), (unsigned) caps);
}

void record_heap(const char *phase) {
  constexpr uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  constexpr uint32_t external = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  ESP_LOGI(TAG, "%s internal_free=%u internal_low_water=%u internal_largest=%u psram_free=%u psram_largest=%u",
           phase, (unsigned) heap_caps_get_free_size(internal),
           (unsigned) heap_caps_get_minimum_free_size(internal),
           (unsigned) heap_caps_get_largest_free_block(internal),
           (unsigned) heap_caps_get_free_size(external),
           (unsigned) heap_caps_get_largest_free_block(external));
}
}  // namespace

void record_loop_stack(const char *phase) {
  // ESP-IDF returns bytes, unlike upstream FreeRTOS's word count. This measures
  // the calling ESPHome loop task, not all networking/driver task stacks.
  ESP_LOGI("memory.stack", "%s task=%s minimum_free_bytes=%u", phase, pcTaskGetName(nullptr),
           (unsigned) uxTaskGetStackHighWaterMark(nullptr));
}

void MemorySetupProbe::setup() {
  if (before_) {
    record_heap("before-lvgl");
    internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    trace_task.store(xTaskGetCurrentTaskHandle(), std::memory_order_relaxed);
    return;
  }
  trace_task.store(nullptr, std::memory_order_relaxed);
  // Capture deltas before logging. Other tasks can allocate during this window;
  // these are net setup changes, not an attribution of every byte to LVGL.
  const auto internal_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const auto psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  ESP_LOGI(TAG, "setup-net internal_used_delta=%ld psram_used_delta=%ld",
           (long) internal_before - (long) internal_after, (long) psram_before - (long) psram_after);
  record_heap("after-lvgl");
  auto *display = lv_display_get_default();
  auto *draw = display == nullptr ? nullptr : lv_display_get_buf_active(display);
  if (draw == nullptr) {
    ESP_LOGW(TAG, "No active LVGL draw buffer (display setup may have failed)");
  } else {
    // This public LVGL API identifies the actual draw allocation. ESPHome's
    // private rotation buffer appears in the allocation trace, without guessing
    // an address from object layout or modifying the upstream component.
    ESP_LOGI(TAG, "draw-buffer address=%p bytes=%u region=%s", draw->data,
             (unsigned) draw->data_size, region(draw->data));
  }
  record_loop_stack("after-lvgl");
}

void record_setup_allocation(const void *ptr, size_t size, uint32_t caps) {
  auto task = trace_task.load(std::memory_order_relaxed);
  if (task == nullptr || task != xTaskGetCurrentTaskHandle()) return;
  // Suppress reentry if logging itself performs an aligned allocation.
  trace_task.store(nullptr, std::memory_order_relaxed);
  record_buffer("setup-aligned-allocation", ptr, size, caps);
  trace_task.store(task, std::memory_order_relaxed);
}
}  // namespace esphome::espframe

extern "C" void *__real_heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps);
extern "C" void *__wrap_heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps) {
  void *ptr = __real_heap_caps_aligned_alloc(alignment, size, caps);
  esphome::espframe::record_setup_allocation(ptr, size, caps);
  return ptr;
}
#endif
