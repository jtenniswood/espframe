#include "frame_blitter.h"

#include "live_trial.h"

#include "esp_log.h"
#if SWIPE_TRIAL_IMMICH
#include "esp_timer.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include <algorithm>
#include <array>

namespace espframe::native {
namespace {

constexpr const char *tag = "frame-blitter";

async_color_convert_request_t rgb565_copy(uint16_t *destination, const uint16_t *source, int stride, int rows,
                                          int dest_x, int source_x, int width) {
  async_color_convert_request_t request{};
  request.src_buffer = source;
  request.dst_buffer = destination;
  request.src_stride = request.dst_stride = stride;
  request.src_x = source_x;
  request.dst_x = dest_x;
  request.copy_width = width;
  request.src_height = request.dst_height = request.copy_height = rows;
  request.src_color_format = request.dst_color_format = ESP_COLOR_FOURCC_RGB16;
  return request;
}

bool finish_split_copy(async_color_convert_handle_t, async_color_convert_event_data_t *, void *arg) {
  BaseType_t task_woken = pdFALSE;
  xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(arg), &task_woken);
  return task_woken == pdTRUE;
}

}  // namespace

void FrameBlitter::init() {
  async_color_convert_config_t config{};
  config.backlog = 2;
  // Match the ESP-IDF DPI framebuffer copy engine's burst size.
  config.dma_burst_size = burst_bytes;
  if (const auto result = esp_async_color_convert_install_dma2d(&config, &dma_); result != ESP_OK) {
    dma_ = nullptr;
    ESP_LOGW(tag, "DMA2D unavailable, using CPU row copies: %s", esp_err_to_name(result));
  }
}

void FrameBlitter::disable_dma_(esp_err_t result) {
  ESP_LOGW(tag, "DMA2D copy rejected, returning to CPU row copies: %s", esp_err_to_name(result));
  esp_async_color_convert_uninstall(dma_);
  dma_ = nullptr;
}

void FrameBlitter::copy_rows_(uint16_t *destination, const uint16_t *source, int width, int rows) {
  copy_block_(destination, source, width, rows, 0, 0, width);
}

void FrameBlitter::copy_block_(uint16_t *destination, const uint16_t *source, int stride, int rows,
                              int dest_x, int source_x, int width) {
  if (dma_ != nullptr) {
    const auto request = rgb565_copy(destination, source, stride, rows, dest_x, source_x, width);
    const auto result = esp_color_convert_blocking(dma_, &request, -1);
    if (result == ESP_OK) {
#if SWIPE_TRIAL_IMMICH
      record_live_trial_blit(true);
#endif
      return;
    }
    if (width == stride)
      disable_dma_(result);
  }
  if (width == stride) {
    copy_slide_rows(destination, source, stride, rows);
  } else {
    for (int row = 0; row < rows; ++row)
      copy_slide_rows(destination + row * stride + dest_x, source + row * stride + source_x, width, 1);
  }
#if SWIPE_TRIAL_IMMICH
  record_live_trial_blit(false);
#endif
}

bool FrameBlitter::copy_split_(uint16_t *output, SlideSpan span) {
  const int first_rows = panel_height - span.offset;
  const std::array requests{
      rgb565_copy(output, span.first + span.offset * panel_width, panel_width, first_rows, 0, 0, panel_width),
      rgb565_copy(output + first_rows * panel_width, span.second, panel_width, span.offset, 0, 0, panel_width),
  };
  StaticSemaphore_t signal;
  const auto finished = xSemaphoreCreateCountingStatic(requests.size(), 0, &signal);
  size_t queued = 0;
  esp_err_t error = ESP_OK;
  for (const auto &request : requests) {
    error = esp_async_color_convert(dma_, &request, finish_split_copy, finished);
    if (error != ESP_OK)
      break;
    ++queued;
  }
  // Do not paint, fall back, or release a cache lease while a slice still
  // reads from a slide or writes to the frame. Either slice may finish first.
  for (size_t index = 0; index < queued; ++index) {
    xSemaphoreTake(finished, portMAX_DELAY);
#if SWIPE_TRIAL_IMMICH
    record_live_trial_blit(true);
#endif
  }
  if (queued == requests.size())
    return true;
  disable_dma_(error);
  for (size_t index = queued; index < requests.size(); ++index) {
    const auto &request = requests[index];
    copy_rows_(static_cast<uint16_t *>(request.dst_buffer), static_cast<const uint16_t *>(request.src_buffer),
               panel_width, request.copy_height);
  }
  return false;
}

void FrameBlitter::slides(uint16_t *output, SlideSpan span, bool across_columns) {
  if (across_columns && span.offset != 0) {
    const int first_width = panel_width - span.offset;
    copy_block_(output, span.first, panel_width, panel_height, 0, span.offset, first_width);
    copy_block_(output, span.second, panel_width, panel_height, first_width, 0, span.offset);
    return;
  }
#if SWIPE_TRIAL_IMMICH
  const auto started = span.offset != 0 ? esp_timer_get_time() : 0;
#endif
  // The DPI framebuffer starts on a cache-line boundary. Only queue adjacent
  // windows together when the row split also keeps their cache syncs separate.
  const size_t split_bytes = (panel_height - span.offset) * panel_width * sizeof(*output);
  [[maybe_unused]] bool batched = false;
  if (span.offset != 0 && dma_ && split_bytes % CONFIG_CACHE_L2_CACHE_LINE_SIZE == 0) {
    batched = copy_split_(output, span);
  } else {
    blit_slides(output, span.first, span.second, panel_width, panel_height, span.offset,
                [this](auto *to, const auto *from, int width, int rows) { copy_rows_(to, from, width, rows); });
  }
#if SWIPE_TRIAL_IMMICH
  if (started)
    record_live_trial_split_blit(static_cast<uint32_t>(esp_timer_get_time() - started), batched);
#endif
}

void FrameBlitter::edge(uint16_t *output, const uint16_t *photo, int pull, bool across_columns) {
  if (across_columns && pull != 0) {
    const int repeated = std::abs(pull);
    const int retained = panel_width - repeated;
    const int destination = std::max(0, -pull);
    const int source = std::max(0, pull);
    copy_block_(output, photo, panel_width, panel_height, destination, source, retained);
    const int strip = pull > 0 ? retained : 0;
    const int edge = pull > 0 ? panel_width - 1 : 0;
    for (int row = 0; row < panel_height; ++row)
      std::fill_n(output + row * panel_width + strip, repeated, photo[row * panel_width + edge]);
    return;
  }
  blit_slide_edge(output, photo, panel_width, panel_height, pull,
                  [this](auto *to, const auto *from, int width, int rows) { copy_rows_(to, from, width, rows); });
}

void FrameBlitter::push(uint16_t *output, const uint16_t *from, const uint16_t *to,
                        int dx, int dy, uint8_t progress, ScreenRotation rotation) {
  const Viewport viewport{rotation};
  const int extent = dx != 0 ? viewport.width : viewport.height;
  const int distance = std::clamp((extent * progress + 127) / 255, 1, extent - 1);
  const int direction = dx != 0 ? dx * viewport.x_stride : dy * viewport.y_stride;
  const bool across_columns = dx != 0 ? viewport.portrait : !viewport.portrait;
  slides(output, direction < 0 ? SlideSpan{from, to, distance} : SlideSpan{to, from, extent - distance},
         across_columns);
}

}  // namespace espframe::native
