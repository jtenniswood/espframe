#pragma once

#include <cstddef>
#include <cstdint>

using esp_err_t = int;
inline constexpr esp_err_t ESP_OK = 0;
inline constexpr esp_err_t ESP_FAIL = -1;
inline constexpr uint32_t ESP_COLOR_FOURCC_RGB16 = 0x4c424752;
inline const char *esp_err_to_name(esp_err_t) { return "mock DMA failure"; }

struct async_color_convert_context_t;
using async_color_convert_handle_t = async_color_convert_context_t *;
struct async_color_convert_event_data_t {};
using async_color_convert_isr_cb_t = bool (*)(async_color_convert_handle_t, async_color_convert_event_data_t *, void *);

struct async_color_convert_config_t {
  uint32_t backlog;
  size_t dma_burst_size;
  uint32_t intr_priority;
};

struct async_color_convert_request_t {
  const void *src_buffer;
  uint32_t src_stride;
  uint32_t src_height;
  uint32_t src_x;
  uint32_t src_y;
  void *dst_buffer;
  uint32_t dst_stride;
  uint32_t dst_height;
  uint32_t dst_x;
  uint32_t dst_y;
  uint32_t copy_width;
  uint32_t copy_height;
  uint32_t src_color_format;
  uint32_t dst_color_format;
};

esp_err_t esp_async_color_convert_install_dma2d(const async_color_convert_config_t *, async_color_convert_handle_t *);
esp_err_t esp_async_color_convert_uninstall(async_color_convert_handle_t);
esp_err_t esp_async_color_convert(async_color_convert_handle_t, const async_color_convert_request_t *,
                                  async_color_convert_isr_cb_t, void *);
esp_err_t esp_color_convert_blocking(async_color_convert_handle_t, const async_color_convert_request_t *, int32_t);
