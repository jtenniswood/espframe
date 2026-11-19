#include "frame_blitter.h"

#include "live_trial.h"

#include "esp_log.h"

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
  if (dma_ != nullptr) {
    async_color_convert_request_t request{};
    request.src_buffer = source;
    request.dst_buffer = destination;
    request.src_stride = request.dst_stride = request.copy_width = width;
    request.src_height = request.dst_height = request.copy_height = rows;
    request.src_color_format = request.dst_color_format = ESP_COLOR_FOURCC_RGB16;
    const auto result = esp_color_convert_blocking(dma_, &request, -1);
    if (result == ESP_OK) {
#if SWIPE_TRIAL_IMMICH
      record_live_trial_blit(true);
#endif
      return;
    }
    ESP_LOGW(tag, "DMA2D copy rejected, returning to CPU row copies: %s", esp_err_to_name(result));
    esp_async_color_convert_uninstall(dma_);
    dma_ = nullptr;
  }
  copy_slide_rows(destination, source, width, rows);
#if SWIPE_TRIAL_IMMICH
  record_live_trial_blit(false);
#endif
}

void FrameBlitter::slides(uint16_t *output, SlideSpan span) {
  blit_slides(output, span.first, span.second, panel_width, panel_height, span.offset,
              [this](auto *to, const auto *from, int width, int rows) { copy_rows_(to, from, width, rows); });
}

void FrameBlitter::edge(uint16_t *output, const uint16_t *photo, int pull) {
  blit_slide_edge(output, photo, panel_width, panel_height, pull,
                  [this](auto *to, const auto *from, int width, int rows) { copy_rows_(to, from, width, rows); });
}

}  // namespace espframe::native
