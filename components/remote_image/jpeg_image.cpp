#include "jpeg_image.h"
#ifdef USE_REMOTE_IMAGE_JPEG_SUPPORT

#include "esphome/components/display/display_buffer.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include "remote_image.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include "driver/jpeg_decode.h"
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#endif

// JPEG decoding is handled as a two-stage process: first collect the full file
// into the download buffer because libjpeg-turbo expects seekable memory, then
// decompress scanlines in small chunks so ESPHome's main loop can keep running.
static const char *const TAG = "remote_image.jpeg";

namespace esphome {
namespace remote_image {

// Several slideshow buffers may finish downloading close together. libjpeg and
// the ESP32-P4 hardware path both have bursty transient memory needs, so decode
// one complete JPEG at a time while leaving later downloads intact and ready.
static JpegDecoder *active_decoder_owner = nullptr;

static void jpeg_error_exit(j_common_ptr cinfo) {
  auto *err = reinterpret_cast<JpegErrorMgr *>(cinfo->err);
  (*(cinfo->err->format_message))(cinfo, err->message);
  longjmp(err->setjmp_buffer, 1);
}

void JpegDecoder::cleanup_() {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  this->cleanup_hardware_();
#endif
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
static uint8_t jpeg_sof_marker(const uint8_t *buffer, size_t size) {
  size_t offset = 0;
  while (offset + 1 < size) {
    while (offset < size && buffer[offset] != 0xFF) offset++;
    while (offset < size && buffer[offset] == 0xFF) offset++;
    if (offset >= size) break;
    uint8_t marker = buffer[offset++];
    if (marker == 0x00 || marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7)) continue;
    if (marker == 0xD9 || marker == 0xDA) break;
    if ((marker >= 0xC0 && marker <= 0xCF) && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
      return marker;
    }
    if (offset + 1 >= size) break;
    uint16_t segment_size = static_cast<uint16_t>(buffer[offset] << 8) | buffer[offset + 1];
    if (segment_size < 2 || segment_size > size - offset) break;
    offset += segment_size;
  }
  return 0;
}

static uint8_t *allocate_hardware_buffer(size_t requested, size_t *capacity) {
  size_t alignment = 0;
  if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &alignment) != ESP_OK || alignment == 0) return nullptr;
  if (requested > SIZE_MAX - (alignment - 1)) return nullptr;
  *capacity = (requested + alignment - 1) & ~(alignment - 1);
  // Both peripherals invalidate their output range before DMA, so clearing
  // multi-megabyte PSRAM buffers here only adds latency without adding safety.
  return static_cast<uint8_t *>(heap_caps_aligned_alloc(alignment, *capacity, MALLOC_CAP_SPIRAM));
}

void JpegDecoder::cleanup_hardware_() {
  if (this->hardware_ppa_ != nullptr) {
    ppa_unregister_client(static_cast<ppa_client_handle_t>(this->hardware_ppa_));
    this->hardware_ppa_ = nullptr;
  }
  if (this->hardware_stage_ != nullptr) {
    free(this->hardware_stage_);
    this->hardware_stage_ = nullptr;
  }
  if (this->hardware_decoded_ != nullptr) {
    free(this->hardware_decoded_);
    this->hardware_decoded_ = nullptr;
  }
  if (active_decoder_owner == this) active_decoder_owner = nullptr;
}

bool JpegDecoder::try_start_hardware_decode_(uint8_t *buffer, size_t size) {
  // The ESP32-P4 JPEG peripheral emits RGB565 directly and the PPA peripheral
  // performs the expensive first scaling pass. Keep a small CPU rescale at the
  // end so fit/fill dimensions and alignment remain pixel-for-pixel compatible
  // with the software decoder despite PPA's 1/16 scaling resolution.
  this->hardware_attempted_ = true;
  if (this->image_->image_type() != image::ImageType::IMAGE_TYPE_RGB565 || size > UINT32_MAX) {
    return false;
  }
  // The silicon only supports baseline DCT. Screen the stream before calling
  // the driver so progressive/extended previews fall back without error logs.
  uint8_t sof_marker = jpeg_sof_marker(buffer, size);
  if (sof_marker != 0xC0) {
    ESP_LOGD(TAG, "JPEG SOF marker 0x%02x is not hardware-decodable; using software", sof_marker);
    return false;
  }

  jpeg_decode_picture_info_t info{};
  esp_err_t err = jpeg_decoder_get_info(buffer, static_cast<uint32_t>(size), &info);
  if (err != ESP_OK || info.width == 0 || info.height == 0 ||
      info.sample_method == JPEG_DOWN_SAMPLING_GRAY) {
    ESP_LOGD(TAG, "Hardware JPEG header rejected (%s); using software decoder", esp_err_to_name(err));
    return false;
  }

  uint32_t mcu_w = 8;
  uint32_t mcu_h = 8;
  if (info.sample_method == JPEG_DOWN_SAMPLING_YUV422) {
    mcu_w = 16;
  } else if (info.sample_method == JPEG_DOWN_SAMPLING_YUV420) {
    mcu_w = 16;
    mcu_h = 16;
  }
  uint32_t padded_w = (info.width + mcu_w - 1) / mcu_w * mcu_w;
  uint32_t padded_h = (info.height + mcu_h - 1) / mcu_h * mcu_h;

  int target_w = this->image_->get_fixed_width();
  int target_h = this->image_->get_fixed_height();
  double desired_scale = 1.0;
  if (target_w > 0 && target_h > 0) {
    double scale_x = static_cast<double>(target_w) / info.width;
    double scale_y = static_cast<double>(target_h) / info.height;
    desired_scale = this->image_->is_fill_mode() ? std::max(scale_x, scale_y)
                                                  : std::min(scale_x, scale_y);
  }
  // PPA cannot represent an upscale more accurately than the existing path,
  // and small inputs do not justify a full intermediate buffer.
  if (desired_scale >= 1.0) return false;

  // PPA accepts scaling factors from 1/16 and has 1/16 fractional precision.
  // Scale to the next larger representable size, then let the existing exact
  // nearest-neighbour path discard the small excess. Never upscale here: the
  // CPU path already handles it and this keeps the staging buffer bounded.
  uint32_t scale_steps = static_cast<uint32_t>(std::ceil(desired_scale * 16.0));
  scale_steps = std::max<uint32_t>(1, std::min<uint32_t>(16, scale_steps));
  uint32_t stage_w = info.width * scale_steps / 16;
  uint32_t stage_h = info.height * scale_steps / 16;
  if (stage_w == 0 || stage_h == 0) return false;

  uint64_t decoded_bytes_64 = static_cast<uint64_t>(padded_w) * padded_h * 2;
  uint64_t stage_bytes_64 = static_cast<uint64_t>(stage_w) * stage_h * 2;
  if (decoded_bytes_64 > SIZE_MAX || decoded_bytes_64 > UINT32_MAX ||
      stage_bytes_64 > SIZE_MAX || stage_bytes_64 > UINT32_MAX) {
    return false;
  }

  jpeg_decoder_handle_t decoder = nullptr;
  this->hardware_started_us_ = micros();
  size_t decoded_capacity = 0;
  this->hardware_decoded_ = allocate_hardware_buffer(static_cast<size_t>(decoded_bytes_64), &decoded_capacity);
  this->hardware_stage_ = allocate_hardware_buffer(static_cast<size_t>(stage_bytes_64),
                                                    &this->hardware_stage_capacity_);
  if (this->hardware_decoded_ == nullptr || this->hardware_stage_ == nullptr) {
    ESP_LOGD(TAG, "Hardware JPEG buffers unavailable; using software decoder");
    this->cleanup_hardware_();
    return false;
  }
  uint32_t allocated_us = micros();
  this->hardware_alloc_us_ = allocated_us - this->hardware_started_us_;

  jpeg_decode_engine_cfg_t engine_cfg{};
  engine_cfg.timeout_ms = 1000;
  err = jpeg_new_decoder_engine(&engine_cfg, &decoder);
  if (err != ESP_OK) {
    ESP_LOGD(TAG, "Hardware JPEG engine unavailable (%s); using software decoder", esp_err_to_name(err));
    this->cleanup_hardware_();
    return false;
  }

  jpeg_decode_cfg_t decode_cfg{};
  decode_cfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
  decode_cfg.rgb_order = this->image_->is_big_endian() ? JPEG_DEC_RGB_ELEMENT_ORDER_RGB
                                                        : JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
  decode_cfg.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;
  uint32_t decoded_size = 0;
  err = jpeg_decoder_process(decoder, &decode_cfg, buffer, static_cast<uint32_t>(size),
                             this->hardware_decoded_, static_cast<uint32_t>(decoded_capacity), &decoded_size);
  jpeg_del_decoder_engine(decoder);
  decoder = nullptr;
  if (err != ESP_OK) {
    ESP_LOGD(TAG, "Hardware JPEG decode rejected image (%s); using software decoder", esp_err_to_name(err));
    this->cleanup_hardware_();
    return false;
  }
  this->hardware_decode_us_ = micros() - allocated_us;

  ppa_client_config_t client_cfg{};
  client_cfg.oper_type = PPA_OPERATION_SRM;
  client_cfg.max_pending_trans_num = 1;
  client_cfg.data_burst_length = PPA_DATA_BURST_LENGTH_128;
  ppa_client_handle_t ppa = nullptr;
  err = ppa_register_client(&client_cfg, &ppa);
  if (err != ESP_OK) {
    ESP_LOGD(TAG, "PPA scaler unavailable (%s); using software decoder", esp_err_to_name(err));
    this->cleanup_hardware_();
    return false;
  }
  this->hardware_ppa_ = ppa;
  this->hardware_src_w_ = info.width;
  this->hardware_src_h_ = info.height;
  this->hardware_padded_w_ = padded_w;
  this->hardware_padded_h_ = padded_h;
  this->hardware_stage_w_ = stage_w;
  this->hardware_stage_h_ = stage_h;
  this->hardware_scale_steps_ = scale_steps;
  this->hardware_scale_y_ = 0;
  this->hardware_draw_y_ = 0;
  this->hardware_scale_us_ = 0;
  this->hardware_draw_us_ = 0;
  this->hardware_worst_chunk_us_ = std::max(this->hardware_alloc_us_, this->hardware_decode_us_);
  this->phase_ = HARDWARE_SCALING;
  return true;
}

bool JpegDecoder::continue_hardware_decode_() {
  uint32_t chunk_started_us = micros();
  if (this->phase_ == HARDWARE_SCALING) {
    // A multiple-of-16 stripe preserves the PPA scaler's fractional phase at
    // every boundary, producing the same output as one monolithic operation.
    // Four-ish stripes for a typical Immich preview release the 6-10 MB
    // full-resolution buffer quickly while keeping each PPA slice below the
    // hardware JPEG decode slice on the measured ESP32-P4 display.
    constexpr uint32_t STRIPE_HEIGHT = 512;
    uint32_t stripe_h = std::min(STRIPE_HEIGHT, this->hardware_src_h_ - this->hardware_scale_y_);
    uint32_t out_y = this->hardware_scale_y_ * this->hardware_scale_steps_ / 16;
    float ppa_scale = static_cast<float>(this->hardware_scale_steps_) / 16.0f;

    ppa_srm_oper_config_t scale_cfg{};
    scale_cfg.in.buffer = this->hardware_decoded_;
    scale_cfg.in.pic_w = this->hardware_padded_w_;
    scale_cfg.in.pic_h = this->hardware_padded_h_;
    scale_cfg.in.block_w = this->hardware_src_w_;
    scale_cfg.in.block_h = stripe_h;
    scale_cfg.in.block_offset_y = this->hardware_scale_y_;
    scale_cfg.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    scale_cfg.out.buffer = this->hardware_stage_;
    scale_cfg.out.buffer_size = static_cast<uint32_t>(this->hardware_stage_capacity_);
    scale_cfg.out.pic_w = this->hardware_stage_w_;
    scale_cfg.out.pic_h = this->hardware_stage_h_;
    scale_cfg.out.block_offset_y = out_y;
    scale_cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    scale_cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    scale_cfg.scale_x = ppa_scale;
    scale_cfg.scale_y = ppa_scale;
    scale_cfg.mode = PPA_TRANS_MODE_BLOCKING;
    esp_err_t err = ppa_do_scale_rotate_mirror(
        static_cast<ppa_client_handle_t>(this->hardware_ppa_), &scale_cfg);
    uint32_t chunk_us = micros() - chunk_started_us;
    this->hardware_scale_us_ += chunk_us;
    this->hardware_worst_chunk_us_ = std::max(this->hardware_worst_chunk_us_, chunk_us);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "PPA scale failed (%s); restarting with software decoder", esp_err_to_name(err));
      this->cleanup_hardware_();
      this->phase_ = WAITING;
      return false;
    }

    this->hardware_scale_y_ += stripe_h;
    if (this->hardware_scale_y_ < this->hardware_src_h_) return true;

    // PPA has finished consuming the large decode buffer.
    ppa_unregister_client(static_cast<ppa_client_handle_t>(this->hardware_ppa_));
    this->hardware_ppa_ = nullptr;
    free(this->hardware_decoded_);
    this->hardware_decoded_ = nullptr;
    if (!this->set_size(static_cast<int>(this->hardware_stage_w_),
                        static_cast<int>(this->hardware_stage_h_))) {
      this->cleanup_hardware_();
      this->phase_ = WAITING;
      return false;
    }
    this->phase_ = HARDWARE_DRAWING;
    return true;
  }

  if (this->phase_ == HARDWARE_DRAWING) {
    constexpr uint32_t DRAW_ROWS = 32;
    uint32_t rows = std::min(DRAW_ROWS, this->hardware_stage_h_ - this->hardware_draw_y_);
    const uint8_t *source = this->hardware_stage_ +
        static_cast<size_t>(this->hardware_draw_y_) * this->hardware_stage_w_ * 2;
    this->draw_rgb565_block(0, static_cast<int>(this->hardware_draw_y_),
                            static_cast<int>(this->hardware_stage_w_), static_cast<int>(rows), source);
    this->hardware_draw_y_ += rows;
    uint32_t chunk_us = micros() - chunk_started_us;
    this->hardware_draw_us_ += chunk_us;
    this->hardware_worst_chunk_us_ = std::max(this->hardware_worst_chunk_us_, chunk_us);
    if (this->hardware_draw_y_ < this->hardware_stage_h_) return true;

    uint32_t total_us = micros() - this->hardware_started_us_;
    ESP_LOGD(TAG, "Hardware JPEG src=%" PRIu32 "x%" PRIu32
                  " stage=%" PRIu32 "x%" PRIu32 " alloc=%" PRIu32
                  "us decode=%" PRIu32 "us scale=%" PRIu32 "us draw=%" PRIu32
                  "us total=%" PRIu32 "us worst=%" PRIu32 "us",
             this->hardware_src_w_, this->hardware_src_h_, this->hardware_stage_w_,
             this->hardware_stage_h_, this->hardware_alloc_us_, this->hardware_decode_us_,
             this->hardware_scale_us_, this->hardware_draw_us_, total_us,
             this->hardware_worst_chunk_us_);
    this->cleanup_hardware_();
    this->phase_ = FINISHED;
    return true;
  }
  return false;
}
#endif

int HOT JpegDecoder::decode(uint8_t *buffer, size_t size) {
  if (this->phase_ == FINISHED) {
    return size;
  }

#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (this->phase_ == HARDWARE_SCALING || this->phase_ == HARDWARE_DRAWING) {
    if (this->continue_hardware_decode_()) {
      if (this->phase_ == FINISHED) {
        this->decoded_bytes_ = size;
        return size;
      }
      return 0;
    }
    // A mid-pipeline hardware error keeps the compressed download buffer intact,
    // so continue below and decode the same image in software.
  }
#endif

  if (this->phase_ == WAITING) {
    if (size < this->download_size_) {
      ESP_LOGV(TAG, "Download not complete. Size: %zu/%zu", size, this->download_size_);
      return 0;
    }

    if (active_decoder_owner != nullptr && active_decoder_owner != this) {
      return 0;
    }
    active_decoder_owner = this;

#if defined(CONFIG_IDF_TARGET_ESP32P4)
    if (!this->hardware_attempted_ && this->try_start_hardware_decode_(buffer, size)) {
      return 0;
    }
#endif
    // A failed hardware setup may release its transient resources and owner
    // marker; the software fallback still needs the same serialization.
    active_decoder_owner = this;

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

    this->use_rgb565_ = (this->image_->image_type() == image::ImageType::IMAGE_TYPE_RGB565);
    this->big_endian_ = this->image_->is_big_endian();
    this->decoder_outputs_rgb565_ = this->use_rgb565_ && !this->big_endian_;
    this->cinfo_->out_color_space = this->decoder_outputs_rgb565_ ? JCS_RGB565 : JCS_RGB;
    // Match ESPFrame's existing RGB888-to-RGB565 truncation exactly. The
    // libjpeg default enables RGB565 dithering, which would alter the pixels.
    if (this->decoder_outputs_rgb565_) this->cinfo_->dither_mode = JDITHER_NONE;
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

    // libjpeg's RGB565 extension writes two bytes per pixel but deliberately
    // reports three output components for API compatibility. Honour that
    // advertised capacity; allocating only the packed width can corrupt the
    // heap in scanline paths that use the reported row width internally.
    size_t row_stride = static_cast<size_t>(this->out_w_) * this->cinfo_->output_components;
    this->row_buffer_ = static_cast<uint8_t *>(malloc(row_stride));
    if (this->row_buffer_ == nullptr) {
      this->cleanup_();
      return DECODE_ERROR_OUT_OF_MEMORY;
    }

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

      if (this->decoder_outputs_rgb565_) {
        this->draw_rgb565_block(0, this->current_scanline_, this->out_w_, 1, this->row_buffer_);
      } else if (this->use_rgb565_ && this->scaling_) {
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
