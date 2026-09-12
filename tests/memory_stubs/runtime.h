#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
using TaskHandle_t = void *;
namespace memory_test {
inline int internal_buffer, external_buffer, other_task;
inline void *task = &internal_buffer;
inline size_t internal_free = 300000, external_free = 1000000;
inline std::vector<std::string> logs;
void log(const char *format, ...);
}
constexpr uint32_t MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_SPIRAM = 2, MALLOC_CAP_8BIT = 4;
inline size_t heap_caps_get_free_size(uint32_t caps) {
  return caps & MALLOC_CAP_INTERNAL ? memory_test::internal_free : memory_test::external_free;
}
inline size_t heap_caps_get_minimum_free_size(uint32_t) { return 100; }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 200; }
inline bool esp_ptr_external_ram(const void *ptr) { return ptr == &memory_test::external_buffer; }
inline bool esp_ptr_internal(const void *ptr) { return ptr == &memory_test::internal_buffer; }
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return memory_test::task; }
inline const char *pcTaskGetName(TaskHandle_t) { return "loopTask"; }
inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t) { return 2048; }
struct lv_draw_buf_t { void *data; uint32_t data_size; };
struct lv_display_t { lv_draw_buf_t *draw; };
inline lv_display_t *test_display = nullptr;
inline lv_display_t *lv_display_get_default() { return test_display; }
inline lv_draw_buf_t *lv_display_get_buf_active(lv_display_t *display) { return display->draw; }
