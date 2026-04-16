#include "webp_image.h"
#ifdef USE_REMOTE_IMAGE_WEBP_SUPPORT

#include "esphome/components/display/display_buffer.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include "remote_image.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

static const char *const TAG = "remote_image.webp";

namespace esphome {
namespace remote_image {

void WebpDecoder::cleanup_() {
  if (this->rgb_buffer_) {
#ifdef ESP_PLATFORM
    heap_caps_free(this->rgb_buffer_);
#else
    free(this->rgb_buffer_);
#endif
    this->rgb_buffer_ = nullptr;
  }
}

int WebpDecoder::prepare(size_t download_size) {
  ImageDecoder::prepare(download_size);
  auto size = this->image_->resize_download_buffer(download_size);
  if (size < download_size) {
    ESP_LOGE(TAG, "Download buffer resize failed!");
    return DECODE_ERROR_OUT_OF_MEMORY;
  }
  return 0;
}

int HOT WebpDecoder::decode(uint8_t *buffer, size_t size) {
  if (this->finished_) {
    return size;
  }

  if (size < this->download_size_) {
    ESP_LOGV(TAG, "Download not complete. Size: %zu/%zu", size, this->download_size_);
    return 0;
  }

  WebPDecoderConfig config;
  if (!WebPInitDecoderConfig(&config)) {
    ESP_LOGE(TAG, "Failed to init WebP decoder config");
    return DECODE_ERROR_UNSUPPORTED_FORMAT;
  }

  if (WebPGetFeatures(buffer, size, &config.input) != VP8_STATUS_OK) {
    ESP_LOGE(TAG, "Not a valid WebP file");
    return DECODE_ERROR_INVALID_TYPE;
  }

  int src_w = config.input.width;
  int src_h = config.input.height;

#ifdef ESP_PLATFORM
  size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  ESP_LOGI(TAG, "WebP %dx%d, PSRAM free: %zu bytes", src_w, src_h, free_psram);
  // Reject images whose raw pixel data alone would exceed 60 % of free PSRAM.
  // Large images increase the chance of heap fragmentation that corrupts the
  // decoder's internal allocation and causes a hard fault in ParseResiduals.
  static const size_t MAX_DECODE_PIXELS = 4000000;  // ~4 MP
  if ((size_t)src_w * (size_t)src_h > MAX_DECODE_PIXELS) {
    ESP_LOGE(TAG, "WebP %dx%d exceeds %zu-pixel safety limit, skipping",
             src_w, src_h, MAX_DECODE_PIXELS);
    return DECODE_ERROR_UNSUPPORTED_FORMAT;
  }
#else
  ESP_LOGD(TAG, "WebP header: %dx%d", src_w, src_h);
#endif

  int target_w = this->image_->get_fixed_width();
  int target_h = this->image_->get_fixed_height();
  int decode_w = src_w;
  int decode_h = src_h;

  if (target_w > 0 && target_h > 0) {
    bool fill = this->image_->is_fill_mode();
    double scale = fill
      ? std::max(static_cast<double>(target_w) / src_w,
                 static_cast<double>(target_h) / src_h)
      : std::min(static_cast<double>(target_w) / src_w,
                 static_cast<double>(target_h) / src_h);
    decode_w = std::max(1, static_cast<int>(src_w * scale));
    decode_h = std::max(1, static_cast<int>(src_h * scale));
    config.options.use_scaling = 1;
    config.options.scaled_width = decode_w;
    config.options.scaled_height = decode_h;
    ESP_LOGD(TAG, "Using libwebp scaling: %dx%d -> %dx%d", src_w, src_h, decode_w, decode_h);
  }
  config.options.no_fancy_upsampling = 1;

  if (!this->set_size(decode_w, decode_h)) {
    ESP_LOGE(TAG, "Could not allocate image buffer");
    return DECODE_ERROR_OUT_OF_MEMORY;
  }

  this->out_w_ = decode_w;
  this->out_h_ = decode_h;

  size_t rgb_size = static_cast<size_t>(decode_w) * decode_h * 3;
#ifdef ESP_PLATFORM
  {
    // Rough estimate: rgb_buffer + libwebp internal decoder state (~2× rgb_size).
    // If PSRAM can't cover it, the allocator falls back to internal heap which
    // is too small and may return a corrupted pointer → hard fault.
    size_t psram_needed = rgb_size * 3;
    size_t psram_avail  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (psram_avail < psram_needed) {
      ESP_LOGE(TAG, "Insufficient PSRAM for WebP decode: need ~%zu, have %zu",
               psram_needed, psram_avail);
      return DECODE_ERROR_OUT_OF_MEMORY;
    }
  }
  this->rgb_buffer_ = static_cast<uint8_t *>(
      heap_caps_malloc(rgb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
  this->rgb_buffer_ = static_cast<uint8_t *>(malloc(rgb_size));
#endif
  if (!this->rgb_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate RGB decode buffer (%zu bytes)", rgb_size);
    return DECODE_ERROR_OUT_OF_MEMORY;
  }

  config.output.colorspace = MODE_RGB;
  config.output.u.RGBA.rgba = this->rgb_buffer_;
  config.output.u.RGBA.stride = decode_w * 3;
  config.output.u.RGBA.size = rgb_size;
  config.output.is_external_memory = 1;

  VP8StatusCode status = WebPDecode(buffer, size, &config);
  if (status != VP8_STATUS_OK) {
    ESP_LOGE(TAG, "WebP decode failed (status %d)", status);
    this->cleanup_();
    return DECODE_ERROR_UNSUPPORTED_FORMAT;
  }

  ESP_LOGD(TAG, "WebP decoded %dx%d, writing to image buffer", decode_w, decode_h);

  bool use_rgb565 = (this->image_->image_type() == image::ImageType::IMAGE_TYPE_RGB565);
  bool big_endian = this->image_->is_big_endian();
  bool scaling = (this->x_scale_ != 1.0 || this->y_scale_ != 1.0 ||
                  this->x_offset_ != 0 || this->y_offset_ != 0);
  int stride = decode_w * 3;
  int prev_dst_y = -1;
  int prev_gap_end = 0;

  for (int y = 0; y < this->out_h_; y++) {
    if ((y & 63) == 0) {
      App.feed_wdt();
    }

    int dst_y = static_cast<int>(y * this->y_scale_) + this->y_offset_;
    if (dst_y == prev_dst_y) {
      continue;
    }
    prev_dst_y = dst_y;

    uint8_t *row = this->rgb_buffer_ + y * stride;

    if (use_rgb565 && scaling) {
      this->draw_rgb888_scaled(y, this->out_w_, row, big_endian);
    } else if (use_rgb565) {
      rgb888_row_to_rgb565(row, row, this->out_w_, big_endian);
      this->draw_rgb565_block(0, y, this->out_w_, 1, row);
    } else {
      for (int x = 0; x < this->out_w_; x++) {
        Color color(row[x * 3 + 0], row[x * 3 + 1], row[x * 3 + 2]);
        this->draw(x, y, 1, 1, color);
      }
    }

    if (this->y_scale_ > 1.0 && dst_y > prev_gap_end) {
      int src_row_y = (dst_y >= 0) ? dst_y : prev_gap_end - 1;
      this->fill_row_gap(prev_gap_end, dst_y, src_row_y);
      prev_gap_end = dst_y + 1;
    } else if (this->y_scale_ > 1.0) {
      prev_gap_end = std::max(dst_y + 1, 0);
    }
  }

  this->cleanup_();
  this->finished_ = true;
  this->decoded_bytes_ = size;
  return size;
}

}  // namespace remote_image
}  // namespace esphome

#endif  // USE_REMOTE_IMAGE_WEBP_SUPPORT
