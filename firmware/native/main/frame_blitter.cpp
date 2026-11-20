#include "frame_blitter.h"

#include "live_trial.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <algorithm>

namespace espframe::native {
namespace {

constexpr const char *tag = "frame-blitter";

}  // namespace

void FrameBlitter::init() {
  async_color_convert_config_t config{};
  config.backlog = 1;
  // Match the ESP-IDF DPI framebuffer copy engine's burst size.
  config.dma_burst_size = burst_bytes;
  if (const auto result = esp_async_color_convert_install_dma2d(&config, &dma_); result != ESP_OK) {
    dma_ = nullptr;
    ESP_LOGW(tag, "DMA2D unavailable, using CPU row copies: %s", esp_err_to_name(result));
  }
}

void FrameBlitter::copy_rows_(uint16_t *destination, const uint16_t *source, int width, int rows) {
  copy_block_(destination, source, width, rows, 0, 0, width);
}

void FrameBlitter::copy_block_(uint16_t *destination, const uint16_t *source, int stride, int rows,
                              int dest_x, int source_x, int width) {
  if (dma_ != nullptr) {
    async_color_convert_request_t request{};
    request.src_buffer = source;
    request.dst_buffer = destination;
    request.src_stride = request.dst_stride = stride;
    request.src_x = source_x;
    request.dst_x = dest_x;
    request.copy_width = width;
    request.src_height = request.dst_height = request.copy_height = rows;
    request.src_color_format = request.dst_color_format = ESP_COLOR_FOURCC_RGB16;
    const auto result = esp_color_convert_blocking(dma_, &request, -1);
    if (result == ESP_OK) {
#if SWIPE_TRIAL_IMMICH
      record_live_trial_blit(true);
#endif
      return;
    }
    if (width == stride) {
      ESP_LOGW(tag, "DMA2D copy rejected, returning to CPU row copies: %s", esp_err_to_name(result));
      esp_async_color_convert_uninstall(dma_);
      dma_ = nullptr;
    }
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

void FrameBlitter::slides(uint16_t *output, SlideSpan span) {
#if SWIPE_TRIAL_IMMICH
  const auto started = span.offset != 0 ? esp_timer_get_time() : 0;
#endif
  blit_slides(output, span.first, span.second, panel_width, panel_height, span.offset,
              [this](auto *to, const auto *from, int width, int rows) { copy_rows_(to, from, width, rows); });
#if SWIPE_TRIAL_IMMICH
  if (started)
    record_live_trial_split_blit(static_cast<uint32_t>(esp_timer_get_time() - started), false);
#endif
}

void FrameBlitter::edge(uint16_t *output, const uint16_t *photo, int pull) {
  blit_slide_edge(output, photo, panel_width, panel_height, pull,
                  [this](auto *to, const auto *from, int width, int rows) { copy_rows_(to, from, width, rows); });
}

void FrameBlitter::push(uint16_t *output, const uint16_t *from, const uint16_t *to,
                        int dx, int dy, uint8_t progress, ScreenRotation rotation) {
  const int extent = dx != 0 ? panel_height : panel_width;
  const int distance = std::clamp((extent * progress + 127) / 255, 1, extent - 1);
  const int orientation = axis_direction(rotation);
  if (dx != 0) {
    slides(output, dx * orientation < 0 ? SlideSpan{from, to, distance}
                                       : SlideSpan{to, from, extent - distance});
  } else if (dy * orientation > 0) {
    copy_block_(output, from, panel_width, panel_height, 0, distance, extent - distance);
    copy_block_(output, to, panel_width, panel_height, extent - distance, 0, distance);
  } else {
    copy_block_(output, to, panel_width, panel_height, 0, extent - distance, distance);
    copy_block_(output, from, panel_width, panel_height, distance, 0, extent - distance);
  }
}

}  // namespace espframe::native
