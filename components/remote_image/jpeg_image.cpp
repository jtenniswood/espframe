#include "jpeg_image.h"
#ifdef USE_REMOTE_IMAGE_JPEG_SUPPORT

#include "esphome/components/display/display_buffer.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include "remote_image.h"

// JPEG decoding is handled as a two-stage process: first collect the full file
// into the download buffer because libjpeg-turbo expects seekable memory, then
// decompress scanlines in small chunks so ESPHome's main loop can keep running.
static const char *const TAG = "remote_image.jpeg";

namespace esphome {
namespace remote_image {

static void jpeg_error_exit(j_common_ptr cinfo) {
  auto *err = reinterpret_cast<JpegErrorMgr *>(cinfo->err);
  (*(cinfo->err->format_message))(cinfo, err->message);
  longjmp(err->setjmp_buffer, 1);
}

void JpegDecoder::cleanup_() {
  if (this->cinfo_) {
    jpeg_destroy_decompress(this->cinfo_);
    delete this->cinfo_;
    this->cinfo_ = nullptr;
  }
  if (this->jerr_) {
    delete this->jerr_;
    this->jerr_ = nullptr;
  }
  if (this->row_buffer_) {
    free(this->row_buffer_);
    this->row_buffer_ = nullptr;
  }
  this->phase_ = WAITING;
}

int JpegDecoder::prepare(size_t download_size) {
  ImageDecoder::prepare(download_size);
  auto size = this->image_->resize_download_buffer(download_size);
  if (size < download_size) {
    ESP_LOGE(TAG, "Download buffer resize failed!");
    return DECODE_ERROR_OUT_OF_MEMORY;
  }
  return 0;
}

int HOT JpegDecoder::decode(uint8_t *buffer, size_t size) {
  if (this->phase_ == FINISHED) {
    return size;
  }

  if (this->phase_ == WAITING) {
    if (size < this->download_size_) {
      ESP_LOGV(TAG, "Download not complete. Size: %zu/%zu", size, this->download_size_);
      return 0;
    }

    this->cinfo_ = new jpeg_decompress_struct();
    this->jerr_ = new JpegErrorMgr();

    this->cinfo_->err = jpeg_std_error(&this->jerr_->pub);
    this->jerr_->pub.error_exit = jpeg_error_exit;

    if (setjmp(this->jerr_->setjmp_buffer)) {
      ESP_LOGE(TAG, "JPEG decode error during setup: %s", this->jerr_->message);
      this->cleanup_();
      return DECODE_ERROR_UNSUPPORTED_FORMAT;
    }

    jpeg_create_decompress(this->cinfo_);
    jpeg_mem_src(this->cinfo_, buffer, size);

    if (jpeg_read_header(this->cinfo_, TRUE) != JPEG_HEADER_OK) {
      ESP_LOGE(TAG, "Could not read JPEG header");
      this->cleanup_();
      return DECODE_ERROR_INVALID_TYPE;
    }

    int src_w = this->cinfo_->image_width;
    int src_h = this->cinfo_->image_height;
    ESP_LOGD(TAG, "JPEG header: %dx%d, components=%d, progressive=%s",
             src_w, src_h, this->cinfo_->num_components,
             this->cinfo_->progressive_mode ? "yes" : "no");

    this->cinfo_->out_color_space = JCS_RGB;
    this->cinfo_->dct_method = JDCT_IFAST;

    // Use libjpeg's native IDCT downscaling before ESPFrame scaling. This avoids
    // decoding more pixels than the screen can use, which is important on ESP32
    // memory and watchdog budgets.
    int target_w = this->image_->get_fixed_width();
    int target_h = this->image_->get_fixed_height();
    if (target_w > 0 && target_h > 0) {
      bool fill = this->image_->is_fill_mode();
      constexpr unsigned int denoms[] = {8, 4, 2, 1};
      for (unsigned int denom : denoms) {
        this->cinfo_->scale_num = 1;
        this->cinfo_->scale_denom = denom;
        jpeg_calc_output_dimensions(this->cinfo_);
        int idct_w = static_cast<int>(this->cinfo_->output_width);
        int idct_h = static_cast<int>(this->cinfo_->output_height);
        double fit = fill
          ? std::max(static_cast<double>(target_w) / idct_w,
                     static_cast<double>(target_h) / idct_h)
          : std::min(static_cast<double>(target_w) / idct_w,
                     static_cast<double>(target_h) / idct_h);
        int need_w = static_cast<int>(idct_w * fit);
        int need_h = static_cast<int>(idct_h * fit);
        if (idct_w >= need_w && idct_h >= need_h) break;
      }
    } else {
      jpeg_calc_output_dimensions(this->cinfo_);
    }

    this->out_w_ = this->cinfo_->output_width;
    int out_h = this->cinfo_->output_height;
    if (this->out_w_ != src_w || out_h != src_h) {
      ESP_LOGD(TAG, "Using IDCT downscale: %dx%d -> %dx%d", src_w, src_h, this->out_w_, out_h);
    }

    if (!this->set_size(this->out_w_, out_h)) {
      this->cleanup_();
      return DECODE_ERROR_OUT_OF_MEMORY;
    }

    jpeg_start_decompress(this->cinfo_);

    size_t row_stride = static_cast<size_t>(this->out_w_) * 3;
    this->row_buffer_ = static_cast<uint8_t *>(malloc(row_stride));
    if (this->row_buffer_ == nullptr) {
      this->cleanup_();
      return DECODE_ERROR_OUT_OF_MEMORY;
    }

    this->use_rgb565_ = (this->image_->image_type() == image::ImageType::IMAGE_TYPE_RGB565);
    this->big_endian_ = this->image_->is_big_endian();
    this->scaling_ = (this->x_scale_ != 1.0 || this->y_scale_ != 1.0 ||
                      this->x_offset_ != 0 || this->y_offset_ != 0);
    this->current_scanline_ = 0;
    this->prev_dst_y_ = -1;
    this->phase_ = DECOMPRESSING;
  }

  // DECOMPRESSING phase: process only a small batch of scanlines each call.
  // Returning 0 tells OnlineImage to call us again without discarding bytes.
  if (setjmp(this->jerr_->setjmp_buffer)) {
    ESP_LOGE(TAG, "JPEG decode error: %s", this->jerr_->message);
    this->cleanup_();
    return DECODE_ERROR_UNSUPPORTED_FORMAT;
  }

  int lines_this_chunk = 0;
  while (this->cinfo_->output_scanline < this->cinfo_->output_height &&
         lines_this_chunk < SCANLINES_PER_CHUNK) {
    uint8_t *row_ptr = this->row_buffer_;
    jpeg_read_scanlines(this->cinfo_, &row_ptr, 1);

    if ((this->current_scanline_ & 63) == 0) {
      App.feed_wdt();
    }

    int dst_y = static_cast<int>(this->current_scanline_ * this->y_scale_) + this->y_offset_;
    if (dst_y != this->prev_dst_y_) {
      this->prev_dst_y_ = dst_y;
      lines_this_chunk++;

      if (this->use_rgb565_ && this->scaling_) {
        this->draw_rgb888_scaled(this->current_scanline_, this->out_w_, this->row_buffer_, this->big_endian_);
      } else if (this->use_rgb565_) {
        rgb888_row_to_rgb565(this->row_buffer_, this->row_buffer_, this->out_w_, this->big_endian_);
        this->draw_rgb565_block(0, this->current_scanline_, this->out_w_, 1, this->row_buffer_);
      } else {
        for (int x = 0; x < this->out_w_; x++) {
          Color color(this->row_buffer_[x * 3 + 0], this->row_buffer_[x * 3 + 1], this->row_buffer_[x * 3 + 2]);
          this->draw(x, this->current_scanline_, 1, 1, color);
        }
      }

      if (this->y_scale_ > 1.0 && dst_y > prev_gap_end_) {
        int src_row_y = (dst_y >= 0) ? dst_y : prev_gap_end_ - 1;
        this->fill_row_gap(prev_gap_end_, dst_y, src_row_y);
        prev_gap_end_ = dst_y + 1;
      } else if (this->y_scale_ > 1.0) {
        prev_gap_end_ = std::max(dst_y + 1, 0);
      }
    }
    this->current_scanline_++;
  }

  if (this->cinfo_->output_scanline >= this->cinfo_->output_height) {
    jpeg_finish_decompress(this->cinfo_);
    this->cleanup_();
    this->phase_ = FINISHED;
    this->decoded_bytes_ = size;
    return size;
  }

  return 0;
}

}  // namespace remote_image
}  // namespace esphome

#endif  // USE_REMOTE_IMAGE_JPEG_SUPPORT
