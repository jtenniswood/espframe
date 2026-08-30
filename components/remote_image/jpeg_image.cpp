#include "jpeg_image.h"
#ifdef USE_REMOTE_IMAGE_JPEG_SUPPORT

#include <cinttypes>
#include <cstring>

#include "esphome/components/display/display_buffer.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include "jpeg_accelerator_helpers.h"
#include "remote_image.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include "driver/jpeg_decode.h"
#include "esp_heap_caps.h"
#endif

// JPEG decoding first collects the full file into the download buffer. P4 can
// then decode eligible previews into a reusable DMA workspace; every other
// target and JPEG mode uses libjpeg-turbo scanlines in bounded loop chunks.
static const char *const TAG = "remote_image.jpeg";

namespace esphome {
namespace remote_image {

// Multiple slideshow slots can finish downloading close together. Decode one
// complete JPEG at a time so the software fallback and the shared P4 hardware
// workspace never overlap their peak memory use.
static JpegDecoder *active_decoder_owner = nullptr;

#if defined(CONFIG_IDF_TARGET_ESP32P4)
struct P4JpegWorkspace {
  jpeg_decoder_handle_t decoder{nullptr};
  uint8_t *input{nullptr};
  size_t input_capacity{0};
  uint8_t *decoded{nullptr};
  size_t decoded_capacity{0};
  bool disabled{false};
  uint32_t attempts{0};
  uint32_t successes{0};
  uint32_t fallbacks{0};
  uint32_t consecutive_failures{0};
  size_t min_internal_largest{SIZE_MAX};
  size_t min_psram_largest{SIZE_MAX};

  void release() {
    if (this->decoder != nullptr) {
      jpeg_del_decoder_engine(this->decoder);
      this->decoder = nullptr;
    }
    free(this->input);
    this->input = nullptr;
    this->input_capacity = 0;
    free(this->decoded);
    this->decoded = nullptr;
    this->decoded_capacity = 0;
  }

  void disable(const char *reason) {
    if (!this->disabled) ESP_LOGW(TAG, "Disabling P4 JPEG acceleration for this boot: %s", reason);
    this->disabled = true;
    this->release();
  }

  bool ensure_ready() {
    if (this->disabled) return false;
    if (this->decoder != nullptr && this->input != nullptr && this->decoded != nullptr) return true;

    jpeg_decode_memory_alloc_cfg_t output_cfg{};
    output_cfg.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
    this->decoded = static_cast<uint8_t *>(
        jpeg_alloc_decoder_mem(P4_JPEG_MAX_DECODED_BYTES, &output_cfg, &this->decoded_capacity));

    jpeg_decode_memory_alloc_cfg_t input_cfg{};
    input_cfg.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER;
    this->input = static_cast<uint8_t *>(
        jpeg_alloc_decoder_mem(P4_JPEG_MAX_INPUT_BYTES, &input_cfg, &this->input_capacity));

    if (this->decoded == nullptr || this->decoded_capacity < P4_JPEG_MAX_DECODED_BYTES ||
        this->input == nullptr || this->input_capacity < P4_JPEG_MAX_INPUT_BYTES) {
      this->disable("fixed DMA workspace allocation failed");
      return false;
    }

    jpeg_decode_engine_cfg_t engine_cfg{};
    engine_cfg.timeout_ms = 1000;
    const esp_err_t err = jpeg_new_decoder_engine(&engine_cfg, &this->decoder);
    if (err != ESP_OK) {
      this->decoder = nullptr;
      this->disable("JPEG decoder engine initialization failed");
      return false;
    }

    ESP_LOGI(TAG, "Initialized fixed P4 JPEG workspace: input=%zu decoded=%zu",
             this->input_capacity, this->decoded_capacity);
    return true;
  }

  void record_memory() {
    const size_t internal_largest =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    this->min_internal_largest = std::min(this->min_internal_largest, internal_largest);
    this->min_psram_largest = std::min(this->min_psram_largest, psram_largest);
    if ((this->successes % 25) == 0) {
      ESP_LOGI(TAG,
               "P4 JPEG stability: attempts=%" PRIu32 " hardware=%" PRIu32 " fallback=%" PRIu32
               " internal_free=%zu internal_largest=%zu internal_largest_min=%zu"
               " psram_free=%zu psram_largest=%zu psram_largest_min=%zu",
               this->attempts, this->successes, this->fallbacks,
               heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT), internal_largest,
               this->min_internal_largest, heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT), psram_largest,
               this->min_psram_largest);
    }
  }
};

static P4JpegWorkspace p4_jpeg_workspace;
#endif

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
  if (active_decoder_owner == this) active_decoder_owner = nullptr;
  this->phase_ = WAITING;
}

int JpegDecoder::prepare(size_t download_size) {
  ImageDecoder::prepare(download_size);
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  this->hardware_attempted_ = false;
#endif
  auto size = this->image_->resize_download_buffer(download_size);
  if (size < download_size) {
    ESP_LOGE(TAG, "Download buffer resize failed!");
    return DECODE_ERROR_OUT_OF_MEMORY;
  }
  return 0;
}

#if defined(CONFIG_IDF_TARGET_ESP32P4)
bool JpegDecoder::try_start_hardware_decode_(uint8_t *buffer, size_t size) {
  this->hardware_attempted_ = true;
  if (p4_jpeg_workspace.disabled || this->image_->image_type() != image::ImageType::IMAGE_TYPE_RGB565 ||
      size > UINT32_MAX) {
    return false;
  }

  p4_jpeg_workspace.attempts++;
  P4JpegInfo info;
  if (!parse_p4_baseline_jpeg(buffer, size, &info)) {
    p4_jpeg_workspace.fallbacks++;
    ESP_LOGD(TAG, "JPEG is not an eligible three-component baseline stream; using software");
    return false;
  }
  if (!p4_jpeg_fits_workspace(info, size)) {
    p4_jpeg_workspace.fallbacks++;
    ESP_LOGD(TAG, "JPEG exceeds fixed P4 workspace: input=%zu decoded=%zu", size, info.decoded_bytes);
    return false;
  }

  const int target_w = this->image_->get_fixed_width();
  const int target_h = this->image_->get_fixed_height();
  if (target_w <= 0 || target_h <= 0) {
    p4_jpeg_workspace.fallbacks++;
    ESP_LOGD(TAG, "JPEG target is auto-sized; using software decoder");
    return false;
  }
  const double scale_x = static_cast<double>(target_w) / info.width;
  const double scale_y = static_cast<double>(target_h) / info.height;
  const double desired_scale = this->image_->is_fill_mode() ? std::max(scale_x, scale_y)
                                                            : std::min(scale_x, scale_y);
  // Libjpeg's native IDCT scaling is already efficient for small images. Keep
  // the large fixed DMA workspace for previews that actually need downscaling.
  if (desired_scale >= 1.0) {
    p4_jpeg_workspace.fallbacks++;
    ESP_LOGD(TAG, "JPEG would be upscaled (%.3f); using software decoder", desired_scale);
    return false;
  }

  if (!p4_jpeg_workspace.ensure_ready()) {
    p4_jpeg_workspace.fallbacks++;
    return false;
  }

  this->hardware_started_us_ = micros();
  memcpy(p4_jpeg_workspace.input, buffer, size);

  jpeg_decode_cfg_t decode_cfg{};
  decode_cfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
  decode_cfg.rgb_order = this->image_->is_big_endian() ? JPEG_DEC_RGB_ELEMENT_ORDER_RGB
                                                        : JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
  decode_cfg.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;
  uint32_t decoded_size = 0;
  const esp_err_t err = jpeg_decoder_process(
      p4_jpeg_workspace.decoder, &decode_cfg, p4_jpeg_workspace.input, static_cast<uint32_t>(size),
      p4_jpeg_workspace.decoded, static_cast<uint32_t>(p4_jpeg_workspace.decoded_capacity), &decoded_size);
  this->hardware_decode_us_ = micros() - this->hardware_started_us_;
  if (err != ESP_OK || decoded_size < info.decoded_bytes) {
    p4_jpeg_workspace.fallbacks++;
    p4_jpeg_workspace.consecutive_failures++;
    ESP_LOGW(TAG, "P4 JPEG decode failed (%s, output=%" PRIu32 "/%zu); using software",
             esp_err_to_name(err), decoded_size, info.decoded_bytes);
    if (err == ESP_ERR_TIMEOUT || p4_jpeg_workspace.consecutive_failures >= 3) {
      p4_jpeg_workspace.disable(err == ESP_ERR_TIMEOUT ? "hardware decode timed out"
                                                       : "three consecutive hardware decode failures");
    }
    return false;
  }

  p4_jpeg_workspace.consecutive_failures = 0;
  if (!this->set_size(static_cast<int>(info.width), static_cast<int>(info.height))) {
    p4_jpeg_workspace.fallbacks++;
    return false;
  }

  this->hardware_src_width_ = info.width;
  this->hardware_src_height_ = info.height;
  this->hardware_stride_ = info.padded_width;
  this->hardware_draw_y_ = -1;
  this->hardware_draw_us_ = 0;
  this->phase_ = HARDWARE_DRAWING;
  return true;
}

void JpegDecoder::continue_hardware_draw_() {
  const uint32_t started_us = micros();
  const bool finished = this->draw_rgb565_scaled_chunk(
      p4_jpeg_workspace.decoded, static_cast<int>(this->hardware_stride_),
      static_cast<int>(this->hardware_src_height_), this->hardware_draw_y_,
      HARDWARE_DRAW_ROWS_PER_CHUNK);
  this->hardware_draw_us_ += micros() - started_us;
  App.feed_wdt();
  if (!finished) return;

  p4_jpeg_workspace.successes++;
  p4_jpeg_workspace.record_memory();
  ESP_LOGD(TAG,
           "P4 JPEG src=%" PRIu32 "x%" PRIu32 " decode=%" PRIu32 "us draw=%" PRIu32
           "us total=%" PRIu32 "us",
           this->hardware_src_width_, this->hardware_src_height_, this->hardware_decode_us_,
           this->hardware_draw_us_, micros() - this->hardware_started_us_);
  if (active_decoder_owner == this) active_decoder_owner = nullptr;
  this->phase_ = FINISHED;
}
#endif

int HOT JpegDecoder::decode(uint8_t *buffer, size_t size) {
  if (this->phase_ == FINISHED) {
    return size;
  }

#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (this->phase_ == HARDWARE_DRAWING) {
    this->continue_hardware_draw_();
    if (this->phase_ == FINISHED) {
      this->decoded_bytes_ = size;
      return size;
    }
    return 0;
  }
#endif

  if (this->phase_ == WAITING) {
    if (size < this->download_size_) {
      ESP_LOGV(TAG, "Download not complete. Size: %zu/%zu", size, this->download_size_);
      return 0;
    }

    if (active_decoder_owner != nullptr && active_decoder_owner != this) return 0;
    active_decoder_owner = this;

#if defined(CONFIG_IDF_TARGET_ESP32P4)
    if (!this->hardware_attempted_ && this->try_start_hardware_decode_(buffer, size)) return 0;
#endif

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
    this->prev_gap_end_ = 0;
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
    this->fill_trailing_row_gap(this->prev_gap_end_);
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
