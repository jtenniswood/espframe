#include "webp_image.h"
#ifdef USE_REMOTE_IMAGE_WEBP_SUPPORT

#include "esphome/components/display/display_buffer.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include "remote_image.h"
static const char *const TAG = "remote_image.webp";

namespace esphome {
namespace remote_image {

void WebpDecoder::cleanup_() {
  if (this->rgb_buffer_) {
    free(this->rgb_buffer_);
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

  int src_w = 0, src_h = 0;
  if (!WebPGetInfo(buffer, size, &src_w, &src_h)) {
    ESP_LOGE(TAG, "Not a valid WebP file");
    return DECODE_ERROR_INVALID_TYPE;
  }

  ESP_LOGD(TAG, "WebP header: %dx%d", src_w, src_h);

  if (!this->set_size(src_w, src_h)) {
    ESP_LOGE(TAG, "Could not allocate image buffer");
    return DECODE_ERROR_OUT_OF_MEMORY;
  }

  this->out_w_ = src_w;
  this->out_h_ = src_h;

  size_t rgb_size = static_cast<size_t>(src_w) * src_h * 3;
  this->rgb_buffer_ = static_cast<uint8_t *>(malloc(rgb_size));
  if (!this->rgb_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate RGB decode buffer (%zu bytes)", rgb_size);
    return DECODE_ERROR_OUT_OF_MEMORY;
  }

  int stride = src_w * 3;
  uint8_t *result = WebPDecodeRGBInto(buffer, size, this->rgb_buffer_, rgb_size, stride);
  if (!result) {
    ESP_LOGE(TAG, "WebP decode failed");
    this->cleanup_();
    return DECODE_ERROR_UNSUPPORTED_FORMAT;
  }

  ESP_LOGD(TAG, "WebP decoded successfully, writing to image buffer");

  bool use_rgb565 = (this->image_->image_type() == image::ImageType::IMAGE_TYPE_RGB565);
  bool big_endian = this->image_->is_big_endian();
  bool scaling = (this->x_scale_ != 1.0 || this->y_scale_ != 1.0 ||
                  this->x_offset_ != 0 || this->y_offset_ != 0);
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
      uint8_t *dst = row;
      for (int x = 0; x < this->out_w_; x++) {
        uint8_t r = row[x * 3 + 0];
        uint8_t g = row[x * 3 + 1];
        uint8_t b = row[x * 3 + 2];
        uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        if (big_endian) {
          dst[0] = rgb565 >> 8;
          dst[1] = rgb565 & 0xFF;
        } else {
          dst[0] = rgb565 & 0xFF;
          dst[1] = rgb565 >> 8;
        }
        dst += 2;
      }
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
