#ifdef USE_ESP32_VARIANT_ESP32P4
#include <algorithm>
#include <cstring>
#include <utility>
#include "mipi_dsi.h"
#include "esphome/core/helpers.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"

namespace esphome::mipi_dsi {

// Maximum bytes to log for init commands (truncated if larger)
static constexpr size_t MIPI_DSI_MAX_CMD_LOG_BYTES = 64;

static bool notify_refresh_ready(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx) {
  SemaphoreHandle_t sem = static_cast<SemaphoreHandle_t>(user_ctx);
  BaseType_t need_yield = pdFALSE;
  xSemaphoreGiveFromISR(sem, &need_yield);
  return (need_yield == pdTRUE);
}

struct MipiDsiIsrContext {
  SemaphoreHandle_t io_lock;
  SemaphoreHandle_t frame_done;
};

static bool notify_trans_done(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx) {
  return notify_refresh_ready(panel, edata, static_cast<MipiDsiIsrContext *>(user_ctx)->io_lock);
}

// Fires each time the DMA restarts a frame, after it has re-read the current
// frame buffer index, so the buffer it was scanning before is free.
static bool notify_frame_done(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx) {
  return notify_refresh_ready(panel, edata, static_cast<MipiDsiIsrContext *>(user_ctx)->frame_done);
}

void MipiDsi::smark_failed(const LogString *message, esp_err_t err) {
  ESP_LOGE(TAG, "%s: %s", LOG_STR_ARG(message), esp_err_to_name(err));
  this->mark_failed(message);
}

void MipiDsi::setup() {
  this->frame_transitions_ = this->frame_transitions_ && this->double_buffer_;
  this->num_fbs_ = this->frame_transitions_ ? 3 : this->double_buffer_ ? 2 : 1;
  ESP_LOGCONFIG(TAG, "Running Setup");

  if (!this->enable_pins_.empty()) {
    for (auto *pin : this->enable_pins_) {
      pin->setup();
      pin->digital_write(true);
    }
    delay(10);
  }

  esp_lcd_dsi_bus_config_t bus_config = {
      .bus_id = 0,  // index from 0, specify the DSI host to use
      .num_data_lanes =
          this->lanes_,  // Number of data lanes to use, can't set a value that exceeds the chip's capability
      .phy_clk_src = {},  // Let ESP-IDF choose the silicon-appropriate PHY source
      .lane_bit_rate_mbps = this->lane_bit_rate_,   // Bit rate of the data lanes, in Mbps
  };
  auto err = esp_lcd_new_dsi_bus(&bus_config, &this->bus_handle_);
  if (err != ESP_OK) {
    this->smark_failed(LOG_STR("lcd_new_dsi_bus failed"), err);
    return;
  }
  esp_lcd_dbi_io_config_t dbi_config = {
      .virtual_channel = 0,
      .lcd_cmd_bits = 8,    // according to the LCD spec
      .lcd_param_bits = 8,  // according to the LCD spec
  };
  err = esp_lcd_new_panel_io_dbi(this->bus_handle_, &dbi_config, &this->io_handle_);
  if (err != ESP_OK) {
    this->smark_failed(LOG_STR("new_panel_io_dbi failed"), err);
    return;
  }
  // clang-format off
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
  auto color_format = LCD_COLOR_FMT_RGB565;
  if (this->color_depth_ == display::COLOR_BITNESS_888) {
    color_format = LCD_COLOR_FMT_RGB888;
  }
  esp_lcd_dpi_panel_config_t dpi_config = {.virtual_channel = 0,
                                           .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
                                           .dpi_clock_freq_mhz = this->pclk_frequency_,
                                           .in_color_format = color_format,
#else
  auto pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
  if (this->color_depth_ == display::COLOR_BITNESS_888) {
    pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB888;
  }
  esp_lcd_dpi_panel_config_t dpi_config = {.virtual_channel = 0,
                                           .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
                                           .dpi_clock_freq_mhz = this->pclk_frequency_,
                                           .pixel_format = pixel_format,
#endif
                                           .num_fbs = this->num_fbs_,  // number of frame buffers to allocate
                                           .video_timing =
                                               {
                                                   .h_size = this->width_,
                                                   .v_size = this->height_,
                                                   .hsync_pulse_width = this->hsync_pulse_width_,
                                                   .hsync_back_porch = this->hsync_back_porch_,
                                                   .hsync_front_porch = this->hsync_front_porch_,
                                                   .vsync_pulse_width = this->vsync_pulse_width_,
                                                   .vsync_back_porch = this->vsync_back_porch_,
                                                   .vsync_front_porch = this->vsync_front_porch_,
                                               },
                                           .flags = {
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
                                               .use_dma2d = true,
#endif
                                           }};
  // clang-format on
  err = esp_lcd_new_panel_dpi(this->bus_handle_, &dpi_config, &this->handle_);
  if (err != ESP_OK) {
    this->smark_failed(LOG_STR("esp_lcd_new_panel_dpi failed"), err);
    return;
  }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
  err = esp_lcd_dpi_panel_enable_dma2d(this->handle_);
  if (err != ESP_OK) {
    this->smark_failed(LOG_STR("esp_lcd_dpi_panel_enable_dma2d failed"), err);
    return;
  }
#endif
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(true);
    delay(5);
    this->reset_pin_->digital_write(false);
    delay(5);
    this->reset_pin_->digital_write(true);
  } else {
    esp_lcd_panel_io_tx_param(this->io_handle_, SW_RESET_CMD, nullptr, 0);
  }
  // need to know when the display is ready for SLPOUT command - will be 120ms after reset
  auto when = millis() + 120;
  err = esp_lcd_panel_init(this->handle_);
  if (err != ESP_OK) {
    this->smark_failed(LOG_STR("esp_lcd_init failed"), err);
    return;
  }
  size_t index = 0;
  auto &vec = this->init_sequence_;
  while (index != vec.size()) {
    if (vec.size() - index < 2) {
      this->mark_failed(LOG_STR("Malformed init sequence"));
      return;
    }
    uint8_t cmd = vec[index++];
    uint8_t x = vec[index++];
    if (x == DELAY_FLAG) {
      ESP_LOGD(TAG, "Delay %dms", cmd);
      delay(cmd);
    } else {
      uint8_t num_args = x & 0x7F;
      if (vec.size() - index < num_args) {
        this->mark_failed(LOG_STR("Malformed init sequence"));
        return;
      }
      if (cmd == SLEEP_OUT) {
        // are we ready, boots?
        int duration = when - millis();
        if (duration > 0) {
          delay(duration);
        }
      }
      const auto *ptr = vec.data() + index;
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERY_VERBOSE
      char hex_buf[format_hex_pretty_size(MIPI_DSI_MAX_CMD_LOG_BYTES)];
#endif
      ESP_LOGVV(TAG, "Command %02X, length %d, byte(s) %s", cmd, num_args,
                format_hex_pretty_to(hex_buf, ptr, num_args, '.'));
      err = esp_lcd_panel_io_tx_param(this->io_handle_, cmd, ptr, num_args);
      if (err != ESP_OK) {
        this->smark_failed(LOG_STR("lcd_panel_io_tx_param failed"), err);
        return;
      }
      index += num_args;
      if (cmd == SLEEP_OUT)
        delay(10);
    }
  }
  this->io_lock_ = xSemaphoreCreateBinary();
  esp_lcd_dpi_panel_event_callbacks_t cbs = {
      .on_color_trans_done = notify_trans_done,
  };
  if (this->double_buffer_) {
    err = this->num_fbs_ == 3
              ? esp_lcd_dpi_panel_get_frame_buffer(this->handle_, 3, reinterpret_cast<void **>(&this->fbs_[0]),
                                                   reinterpret_cast<void **>(&this->fbs_[1]),
                                                   reinterpret_cast<void **>(&this->fbs_[2]))
              : esp_lcd_dpi_panel_get_frame_buffer(this->handle_, 2, reinterpret_cast<void **>(&this->fbs_[0]),
                                                   reinterpret_cast<void **>(&this->fbs_[1]));
    if (err != ESP_OK) {
      this->smark_failed(LOG_STR("esp_lcd_dpi_panel_get_frame_buffer failed"), err);
      return;
    }
    this->frame_done_ = xSemaphoreCreateBinary();
    ppa_client_config_t ppa_config{};
    ppa_config.oper_type = PPA_OPERATION_SRM;
    ppa_config.max_pending_trans_num = 1;
    if (ppa_register_client(&ppa_config, &this->ppa_client_) != ESP_OK) {
      ESP_LOGW(TAG, "PPA unavailable, copying frame buffers with the CPU");
      this->ppa_client_ = nullptr;
    }
    cbs.on_frame_buf_complete = notify_frame_done;
  }
  if (this->frame_transitions_) {
    const size_t fb_size = this->width_ * this->height_ * (3 - this->color_depth_);
    this->snapshot_ = static_cast<uint8_t *>(heap_caps_aligned_alloc(128, fb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    this->target_ = static_cast<uint8_t *>(heap_caps_aligned_alloc(128, fb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ppa_client_config_t blend_config{};
    blend_config.oper_type = PPA_OPERATION_BLEND;
    blend_config.max_pending_trans_num = 1;
    if (this->snapshot_ == nullptr || this->target_ == nullptr || this->ppa_client_ == nullptr ||
        ppa_register_client(&blend_config, &this->blend_client_) != ESP_OK) {
      ESP_LOGW(TAG, "Frame transitions unavailable");
      this->frame_transitions_ = false;
    }
  }
  static MipiDsiIsrContext isr_context;
  isr_context = {this->io_lock_, this->frame_done_};

  err = (esp_lcd_dpi_panel_register_event_callbacks(this->handle_, &cbs, &isr_context));
  if (err != ESP_OK) {
    this->smark_failed(LOG_STR("Failed to register callbacks"), err);
    return;
  }

  ESP_LOGCONFIG(TAG, "MIPI DSI setup complete");
}

void MipiDsi::update() {
  if (this->auto_clear_enabled_) {
    this->clear();
  }
  if (this->show_test_card_) {
    this->test_card();
  } else if (this->page_ != nullptr) {
    this->page_->get_writer()(*this);
  } else if (this->writer_.has_value()) {
    (*this->writer_)(*this);
  } else {
    this->stop_poller();
  }
  if (this->buffer_ == nullptr || this->x_low_ > this->x_high_ || this->y_low_ > this->y_high_)
    return;
  ESP_LOGV(TAG, "x_low %d, y_low %d, x_high %d, y_high %d", this->x_low_, this->y_low_, this->x_high_, this->y_high_);
  int w = this->x_high_ - this->x_low_ + 1;
  int h = this->y_high_ - this->y_low_ + 1;
  this->write_to_display_(this->x_low_, this->y_low_, w, h, this->buffer_, this->x_low_, this->y_low_,
                          this->width_ - w - this->x_low_);
  // invalidate watermarks
  this->x_low_ = this->width_;
  this->y_low_ = this->height_;
  this->x_high_ = 0;
  this->y_high_ = 0;
}

void MipiDsi::draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr, display::ColorOrder order,
                             display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset, int x_pad) {
  if (w <= 0 || h <= 0)
    return;
  // if color mapping is required, pass the buck.
  // note that endianness is not considered here - it is assumed to match!
  if (bitness != this->color_depth_) {
    display::Display::draw_pixels_at(x_start, y_start, w, h, ptr, order, bitness, big_endian, x_offset, y_offset,
                                     x_pad);
    return;
  }
  this->write_to_display_(x_start, y_start, w, h, ptr, x_offset, y_offset, x_pad);
}

void MipiDsi::write_to_display_(int x_start, int y_start, int w, int h, const uint8_t *ptr, int x_offset, int y_offset,
                                int x_pad) {
  esp_err_t err = ESP_OK;
  auto bytes_per_pixel = 3 - this->color_depth_;
  auto stride = (x_offset + w + x_pad) * bytes_per_pixel;
  ptr += y_offset * stride + x_offset * bytes_per_pixel;  // skip to the first pixel
  if (this->double_buffer_) {
    this->copy_to_back_buffer_(x_start, y_start, w, h, ptr, stride);
    return;
  }
  // x_ and y_offset are offsets into the source buffer, unrelated to our own offsets into the display.
  if (x_offset == 0 && x_pad == 0) {
    err = esp_lcd_panel_draw_bitmap(this->handle_, x_start, y_start, x_start + w, y_start + h, ptr);
    xSemaphoreTake(this->io_lock_, portMAX_DELAY);

  } else {
    // draw line by line
    for (int y = 0; y != h; y++) {
      err = esp_lcd_panel_draw_bitmap(this->handle_, x_start, y + y_start, x_start + w, y + y_start + 1, ptr);
      if (err != ESP_OK)
        break;
      ptr += stride;  // next line
      xSemaphoreTake(this->io_lock_, portMAX_DELAY);
    }
  }
  if (err != ESP_OK)
    ESP_LOGE(TAG, "lcd_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(err));
}

void MipiDsi::copy_to_back_buffer_(int x_start, int y_start, int w, int h, const uint8_t *ptr, size_t stride) {
  const size_t bytes_per_pixel = 3 - this->color_depth_;
  const int x_end = std::min<int>(x_start + w, this->width_);
  const int y_end = std::min<int>(y_start + h, this->height_);
  if (x_start < 0 || y_start < 0 || x_start >= x_end || y_start >= y_end)
    return;
  this->copy_block_(ptr, stride / bytes_per_pixel, h, 0, 0, this->draw_buffer_(), x_start, y_start, x_end - x_start,
                    y_end - y_start);
  const Area area{x_start, y_start, x_end, y_end};
  this->drawn_.push_back(area);
  if (!this->dirty_) {
    this->dirty_area_ = area;
    this->dirty_ = true;
  } else {
    this->dirty_area_.x1 = std::min(this->dirty_area_.x1, x_start);
    this->dirty_area_.y1 = std::min(this->dirty_area_.y1, y_start);
    this->dirty_area_.x2 = std::max(this->dirty_area_.x2, x_end);
    this->dirty_area_.y2 = std::max(this->dirty_area_.y2, y_end);
  }
}

void MipiDsi::copy_block_(const uint8_t *src, int src_pic_w, int src_pic_h, int src_x, int src_y, uint8_t *dst,
                          int dst_x, int dst_y, int w, int h) {
  const size_t bytes_per_pixel = 3 - this->color_depth_;
  if (this->blend_client_ != nullptr) {
    // The blend engine copies roughly three times faster than SRM: blend the
    // source under a fully transparent A8 foreground. Its pixel data is never
    // used, so any readable buffer of at least w*h bytes will do.
    const auto color_mode =
        this->color_depth_ == display::COLOR_BITNESS_888 ? PPA_BLEND_COLOR_MODE_RGB888 : PPA_BLEND_COLOR_MODE_RGB565;
    ppa_blend_oper_config_t config{};
    config.in_bg.buffer = src;
    config.in_bg.pic_w = src_pic_w;
    config.in_bg.pic_h = src_pic_h;
    config.in_bg.block_w = w;
    config.in_bg.block_h = h;
    config.in_bg.block_offset_x = src_x;
    config.in_bg.block_offset_y = src_y;
    config.in_bg.blend_cm = color_mode;
    config.in_fg.buffer = dst == this->snapshot_ ? this->target_ : this->snapshot_;
    config.in_fg.pic_w = w;
    config.in_fg.pic_h = h;
    config.in_fg.block_w = w;
    config.in_fg.block_h = h;
    config.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_A8;
    config.out.buffer = dst;
    config.out.buffer_size = this->width_ * this->height_ * bytes_per_pixel;
    config.out.pic_w = this->width_;
    config.out.pic_h = this->height_;
    config.out.block_offset_x = dst_x;
    config.out.block_offset_y = dst_y;
    config.out.blend_cm = color_mode;
    config.bg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;
    config.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
    config.fg_alpha_fix_val = 0;
    config.mode = PPA_TRANS_MODE_BLOCKING;
    if (ppa_do_blend(this->blend_client_, &config) == ESP_OK)
      return;
    ESP_LOGW(TAG, "PPA blend copy failed, falling back to SRM");
  }
  if (this->ppa_client_ != nullptr) {
    const auto color_mode =
        this->color_depth_ == display::COLOR_BITNESS_888 ? PPA_SRM_COLOR_MODE_RGB888 : PPA_SRM_COLOR_MODE_RGB565;
    ppa_srm_oper_config_t config{};
    config.in.buffer = src;
    config.in.pic_w = src_pic_w;
    config.in.pic_h = src_pic_h;
    config.in.block_w = w;
    config.in.block_h = h;
    config.in.block_offset_x = src_x;
    config.in.block_offset_y = src_y;
    config.in.srm_cm = color_mode;
    config.out.buffer = dst;
    config.out.buffer_size = this->width_ * this->height_ * bytes_per_pixel;
    config.out.pic_w = this->width_;
    config.out.pic_h = this->height_;
    config.out.block_offset_x = dst_x;
    config.out.block_offset_y = dst_y;
    config.out.srm_cm = color_mode;
    config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    config.scale_x = 1.0f;
    config.scale_y = 1.0f;
    config.mode = PPA_TRANS_MODE_BLOCKING;
    if (ppa_do_scale_rotate_mirror(this->ppa_client_, &config) == ESP_OK)
      return;
    ESP_LOGW(TAG, "PPA copy failed, using the CPU from now on");
    ppa_unregister_client(this->ppa_client_);
    this->ppa_client_ = nullptr;
  }
  const size_t src_line = src_pic_w * bytes_per_pixel;
  const size_t dst_line = this->width_ * bytes_per_pixel;
  const size_t copy_bytes = w * bytes_per_pixel;
  src += src_y * src_line + src_x * bytes_per_pixel;
  uint8_t *out = dst + dst_y * dst_line + dst_x * bytes_per_pixel;
  for (int y = 0; y < h; y++) {
    memcpy(out, src, copy_bytes);
    src += src_line;
    out += dst_line;
  }
  esp_cache_msync(dst + dst_y * dst_line, h * dst_line, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

// LVGL renders a whole refresh inside its own loop(), so by the time this runs
// the back buffer holds a complete frame.
void MipiDsi::loop() {
  if (!this->double_buffer_)
    return;
  if (this->animating_) {
    if (this->dirty_) {
      // LVGL drew into target_; a new transition restarts from what is on
      // screen now, anything else just updates the target.
      if (this->pending_.mode != FrameTransitionMode::NONE)
        this->begin_animation_();
      this->dirty_ = false;
      this->drawn_.clear();
    }
    this->animate_step_();
    return;
  }
  if (this->pending_.mode != FrameTransitionMode::NONE && millis() - this->pending_ms_ > 1000)
    this->pending_ = {};
  if (!this->dirty_)
    return;
  if (this->pending_.mode != FrameTransitionMode::NONE) {
    // The back buffer must hold the complete new frame before it is blended.
    this->sync_stale_area_();
    this->dirty_ = false;
    this->drawn_.clear();
    this->begin_animation_();
    this->animate_step_();
    return;
  }
  this->present_();
}

void MipiDsi::start_transition(const FrameTransition &transition) {
  if (!this->frame_transitions_ || transition.mode == FrameTransitionMode::NONE || transition.duration_ms == 0)
    return;
  this->pending_ = transition;
  this->pending_ms_ = millis();
}

// Brings the back buffer's stale area up to date from the front buffer, except
// where this frame already drew. Full-screen frames (transitions) skip the copy
// entirely. Drawn areas do not overlap, so splitting the stale area into bands
// at their edges leaves each band either fully drawn or untouched per area.
void MipiDsi::sync_stale_area_() {
  if (!this->stale_)
    return;
  const Area stale = this->stale_area_;
  this->stale_ = false;
  std::vector<int> edges{stale.y1, stale.y2};
  for (const auto &a : this->drawn_) {
    if (a.y1 > stale.y1 && a.y1 < stale.y2)
      edges.push_back(a.y1);
    if (a.y2 > stale.y1 && a.y2 < stale.y2)
      edges.push_back(a.y2);
  }
  std::sort(edges.begin(), edges.end());
  edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
  std::vector<std::pair<int, int>> covered;
  for (size_t e = 0; e + 1 < edges.size(); e++) {
    const int y1 = edges[e], y2 = edges[e + 1];
    covered.clear();
    for (const auto &a : this->drawn_) {
      if (a.y1 <= y1 && a.y2 >= y2 && a.x2 > stale.x1 && a.x1 < stale.x2)
        covered.emplace_back(std::max(a.x1, stale.x1), std::min(a.x2, stale.x2));
    }
    std::sort(covered.begin(), covered.end());
    int x = stale.x1;
    for (const auto &span : covered) {
      if (span.first > x)
        this->copy_block_(this->fbs_[this->front_], this->width_, this->height_, x, y1, this->fbs_[this->back_],
                          x, y1, span.first - x, y2 - y1);
      x = std::max(x, span.second);
    }
    if (x < stale.x2)
      this->copy_block_(this->fbs_[this->front_], this->width_, this->height_, x, y1, this->fbs_[this->back_], x,
                        y1, stale.x2 - x, y2 - y1);
  }
}

// Asks the DMA to scan fbs_[index] from the next frame boundary on. A pointer
// inside a frame buffer makes the driver write back the cached rows and switch
// buffers; nothing is copied.
bool MipiDsi::request_swap_(uint8_t index, int y1, int y2) {
  const size_t line = this->width_ * (3 - this->color_depth_);
  esp_err_t err = esp_lcd_panel_draw_bitmap(this->handle_, 0, y1, this->width_, y2, this->fbs_[index] + y1 * line);
  xSemaphoreTake(this->io_lock_, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "frame buffer swap failed: %s", esp_err_to_name(err));
    return false;
  }
  // A frame-done already pending may predate the request; the next one
  // guarantees the DMA has re-read the buffer index.
  xSemaphoreTake(this->frame_done_, 0);
  this->queued_ = index;
  return true;
}

// Waits until the queued buffer is being scanned, freeing the previous one.
void MipiDsi::wait_swap_() {
  if (this->queued_ < 0)
    return;
  if (xSemaphoreTake(this->frame_done_, pdMS_TO_TICKS(100)) != pdTRUE)
    ESP_LOGW(TAG, "No frame boundary after buffer swap");
  this->front_ = this->queued_;
  this->queued_ = -1;
}

bool MipiDsi::swap_to_(uint8_t index, int y1, int y2) {
  if (!this->request_swap_(index, y1, y2))
    return false;
  this->wait_swap_();
  return true;
}

void MipiDsi::present_() {
  // The back buffer must hold the complete frame before it is shown.
  this->sync_stale_area_();
  const Area dirty = this->dirty_area_;
  this->dirty_ = false;
  this->drawn_.clear();
  const uint8_t old_front = this->front_;
  if (!this->swap_to_(this->back_, dirty.y1, dirty.y2))
    return;
  this->back_ = old_front;
  // The new back buffer lacks this frame's changes; they are copied lazily at
  // the next swap, minus whatever that frame redraws itself.
  this->stale_area_ = dirty;
  this->stale_ = true;
}

void MipiDsi::begin_animation_() {
  // Whatever is on screen (a normal or a composed frame) is the old frame.
  this->wait_swap_();
  this->copy_block_(this->fbs_[this->front_], this->width_, this->height_, 0, 0, this->snapshot_, 0, 0, this->width_,
                    this->height_);
  if (!this->animating_) {
    // LVGL's new frame moves to target_, where it keeps drawing until the end.
    this->copy_block_(this->fbs_[this->back_], this->width_, this->height_, 0, 0, this->target_, 0, 0, this->width_,
                      this->height_);
  }
  this->active_ = this->pending_;
  this->pending_ = {};
  this->animating_ = true;
  this->high_freq_.start();
  this->animation_start_ms_ = millis();
}

// Composes the next frame into whichever panel buffer is neither scanned nor
// queued, while the queued one waits for its frame boundary.
void MipiDsi::animate_step_() {
  const uint32_t elapsed = millis() - this->animation_start_ms_;
  const bool last = elapsed >= this->active_.duration_ms;
  uint8_t out = 0;
  while (out == this->front_ || out == this->queued_)
    out++;
  if (last) {
    this->copy_block_(this->target_, this->width_, this->height_, 0, 0, this->fbs_[out], 0, 0, this->width_,
                      this->height_);
  } else {
    this->compose_(this->fbs_[out], static_cast<int32_t>(elapsed * 1024 / this->active_.duration_ms));
  }
  this->wait_swap_();
  if (!this->request_swap_(out, 0, this->height_))
    return;
  if (!last)
    return;
  this->wait_swap_();
  // Back to plain double buffering: LVGL draws into another buffer, which
  // lacks the whole new frame until the next swap syncs it.
  this->animating_ = false;
  this->high_freq_.stop();
  this->back_ = this->front_ == 0 ? 1 : 0;
  this->stale_area_ = {0, 0, static_cast<int>(this->width_), static_cast<int>(this->height_)};
  this->stale_ = true;
}

void MipiDsi::compose_(uint8_t *out, int32_t progress) {
  const uint8_t *old_frame = this->snapshot_;
  const uint8_t *new_frame = this->target_;
  const int width = this->width_;
  const int height = this->height_;
  switch (this->active_.mode) {
    case FrameTransitionMode::CROSS_FADE:
      this->blend_(new_frame, progress * 255 / 1024, old_frame, out);
      break;
    case FrameTransitionMode::FADE_THROUGH:
      // Out to the colour over the first half, in from it over the second.
      if (progress < 512) {
        this->blend_color_(old_frame, progress * 255 / 512, this->active_.color_rgb888, out);
      } else {
        this->blend_color_(new_frame, (1024 - progress) * 255 / 512, this->active_.color_rgb888, out);
      }
      break;
    case FrameTransitionMode::MOVE: {
      const int dx = this->active_.dx, dy = this->active_.dy;
      const int remaining = 1024 - progress;
      // New frame's top-left, starting one screen behind its direction of travel.
      const int bx = -dx * width * remaining / 1024;
      const int by = -dy * height * remaining / 1024;
      const int nx1 = std::max(bx, 0), ny1 = std::max(by, 0);
      const int nx2 = std::min(bx + width, width), ny2 = std::min(by + height, height);
      // The uncovered band left for the old frame.
      int ox1 = 0, oy1 = 0, ox2 = width, oy2 = height;
      if (dx > 0)
        ox1 = nx2;
      if (dx < 0)
        ox2 = nx1;
      if (dy > 0)
        oy1 = ny2;
      if (dy < 0)
        oy2 = ny1;
      // Pushed, the old frame sits one screen ahead of the new one.
      const int ax = this->active_.push ? bx + dx * width : 0;
      const int ay = this->active_.push ? by + dy * height : 0;
      if (ox2 > ox1 && oy2 > oy1)
        this->copy_block_(old_frame, width, height, ox1 - ax, oy1 - ay, out, ox1, oy1, ox2 - ox1, oy2 - oy1);
      if (nx2 > nx1 && ny2 > ny1)
        this->copy_block_(new_frame, width, height, nx1 - bx, ny1 - by, out, nx1, ny1, nx2 - nx1, ny2 - ny1);
      break;
    }
    case FrameTransitionMode::NONE:
    default:
      this->copy_block_(new_frame, width, height, 0, 0, out, 0, 0, width, height);
      break;
  }
}

bool MipiDsi::blend_(const uint8_t *fg, uint8_t fg_alpha, const uint8_t *bg, uint8_t *out) {
  const auto color_mode =
      this->color_depth_ == display::COLOR_BITNESS_888 ? PPA_BLEND_COLOR_MODE_RGB888 : PPA_BLEND_COLOR_MODE_RGB565;
  ppa_blend_oper_config_t config{};
  for (auto *in : {&config.in_bg, &config.in_fg}) {
    in->pic_w = this->width_;
    in->pic_h = this->height_;
    in->block_w = this->width_;
    in->block_h = this->height_;
    in->blend_cm = color_mode;
  }
  config.in_bg.buffer = bg;
  config.in_fg.buffer = fg;
  config.out.buffer = out;
  config.out.buffer_size = this->width_ * this->height_ * (3 - this->color_depth_);
  config.out.pic_w = this->width_;
  config.out.pic_h = this->height_;
  config.out.blend_cm = color_mode;
  config.bg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  config.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  config.fg_alpha_fix_val = fg_alpha;
  config.mode = PPA_TRANS_MODE_BLOCKING;
  esp_err_t err = ppa_do_blend(this->blend_client_, &config);
  if (err != ESP_OK)
    ESP_LOGW(TAG, "PPA blend failed: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

// Blends a solid colour over bg in one pass: an A4 foreground whose alpha is
// overridden carries the colour, so its pixel data is never used and any
// readable buffer of at least half a frame's pixel count will do.
bool MipiDsi::blend_color_(const uint8_t *bg, uint8_t color_alpha, uint32_t rgb888, uint8_t *out) {
  const auto color_mode =
      this->color_depth_ == display::COLOR_BITNESS_888 ? PPA_BLEND_COLOR_MODE_RGB888 : PPA_BLEND_COLOR_MODE_RGB565;
  ppa_blend_oper_config_t config{};
  for (auto *in : {&config.in_bg, &config.in_fg}) {
    in->pic_w = this->width_;
    in->pic_h = this->height_;
    in->block_w = this->width_;
    in->block_h = this->height_;
  }
  config.in_bg.buffer = bg;
  config.in_bg.blend_cm = color_mode;
  config.in_fg.buffer = this->snapshot_;
  config.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_A4;
  config.out.buffer = out;
  config.out.buffer_size = this->width_ * this->height_ * (3 - this->color_depth_);
  config.out.pic_w = this->width_;
  config.out.pic_h = this->height_;
  config.out.blend_cm = color_mode;
  config.bg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  config.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  config.fg_alpha_fix_val = color_alpha;
  config.fg_fix_rgb_val.r = (rgb888 >> 16) & 0xFF;
  config.fg_fix_rgb_val.g = (rgb888 >> 8) & 0xFF;
  config.fg_fix_rgb_val.b = rgb888 & 0xFF;
  config.mode = PPA_TRANS_MODE_BLOCKING;
  esp_err_t err = ppa_do_blend(this->blend_client_, &config);
  if (err != ESP_OK)
    ESP_LOGW(TAG, "PPA colour blend failed: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

bool MipiDsi::check_buffer_() {
  if (this->is_failed())
    return false;
  if (this->buffer_ != nullptr)
    return true;
  // this is dependent on the enum values.
  auto bytes_per_pixel = 3 - this->color_depth_;
  RAMAllocator<uint8_t> allocator;
  this->buffer_ = allocator.allocate(this->height_ * this->width_ * bytes_per_pixel);
  if (this->buffer_ == nullptr) {
    this->mark_failed(LOG_STR("Could not allocate buffer for display!"));
    return false;
  }
  return true;
}

void MipiDsi::draw_pixel_at(int x, int y, Color color) {
  if (!this->get_clipping().inside(x, y))
    return;

  switch (this->rotation_) {
    case display::DISPLAY_ROTATION_0_DEGREES:
      break;
    case display::DISPLAY_ROTATION_90_DEGREES:
      std::swap(x, y);
      x = this->width_ - x - 1;
      break;
    case display::DISPLAY_ROTATION_180_DEGREES:
      x = this->width_ - x - 1;
      y = this->height_ - y - 1;
      break;
    case display::DISPLAY_ROTATION_270_DEGREES:
      std::swap(x, y);
      y = this->height_ - y - 1;
      break;
  }
  if (x >= this->get_width_internal() || x < 0 || y >= this->get_height_internal() || y < 0) {
    return;
  }
  if (!this->check_buffer_())
    return;
  size_t pos = (y * this->width_) + x;
  switch (this->color_depth_) {
    case display::COLOR_BITNESS_565: {
      auto *ptr_16 = reinterpret_cast<uint16_t *>(this->buffer_);
      uint8_t hi_byte = static_cast<uint8_t>(color.r & 0xF8) | (color.g >> 5);
      uint8_t lo_byte = static_cast<uint8_t>((color.g & 0x1C) << 3) | (color.b >> 3);
      uint16_t new_color = lo_byte | (hi_byte << 8);  // little endian
      if (ptr_16[pos] == new_color)
        return;
      ptr_16[pos] = new_color;
      break;
    }
    case display::COLOR_BITNESS_888:
      if (this->color_mode_ == display::COLOR_ORDER_BGR) {
        this->buffer_[pos * 3] = color.b;
        this->buffer_[pos * 3 + 1] = color.g;
        this->buffer_[pos * 3 + 2] = color.r;
      } else {
        this->buffer_[pos * 3] = color.r;
        this->buffer_[pos * 3 + 1] = color.g;
        this->buffer_[pos * 3 + 2] = color.b;
      }
      break;
    case display::COLOR_BITNESS_332:
      break;
  }
  // low and high watermark may speed up drawing from buffer
  if (x < this->x_low_)
    this->x_low_ = x;
  if (y < this->y_low_)
    this->y_low_ = y;
  if (x > this->x_high_)
    this->x_high_ = x;
  if (y > this->y_high_)
    this->y_high_ = y;
}
void MipiDsi::fill(Color color) {
  if (!this->check_buffer_())
    return;

  // If clipping is active, fall back to base implementation
  if (this->get_clipping().is_set()) {
    Display::fill(color);
    return;
  }

  switch (this->color_depth_) {
    case display::COLOR_BITNESS_565: {
      auto *ptr_16 = reinterpret_cast<uint16_t *>(this->buffer_);
      uint8_t hi_byte = static_cast<uint8_t>(color.r & 0xF8) | (color.g >> 5);
      uint8_t lo_byte = static_cast<uint8_t>((color.g & 0x1C) << 3) | (color.b >> 3);
      uint16_t new_color = lo_byte | (hi_byte << 8);  // little endian
      std::fill_n(ptr_16, this->width_ * this->height_, new_color);
      break;
    }

    case display::COLOR_BITNESS_888:
      if (this->color_mode_ == display::COLOR_ORDER_BGR) {
        for (size_t i = 0; i != this->width_ * this->height_; i++) {
          this->buffer_[i * 3 + 0] = color.b;
          this->buffer_[i * 3 + 1] = color.g;
          this->buffer_[i * 3 + 2] = color.r;
        }
      } else {
        for (size_t i = 0; i != this->width_ * this->height_; i++) {
          this->buffer_[i * 3 + 0] = color.r;
          this->buffer_[i * 3 + 1] = color.g;
          this->buffer_[i * 3 + 2] = color.b;
        }
      }

    default:
      break;
  }
}

int MipiDsi::get_width() {
  switch (this->rotation_) {
    case display::DISPLAY_ROTATION_90_DEGREES:
    case display::DISPLAY_ROTATION_270_DEGREES:
      return this->get_height_internal();
    case display::DISPLAY_ROTATION_0_DEGREES:
    case display::DISPLAY_ROTATION_180_DEGREES:
    default:
      return this->get_width_internal();
  }
}

int MipiDsi::get_height() {
  switch (this->rotation_) {
    case display::DISPLAY_ROTATION_0_DEGREES:
    case display::DISPLAY_ROTATION_180_DEGREES:
      return this->get_height_internal();
    case display::DISPLAY_ROTATION_90_DEGREES:
    case display::DISPLAY_ROTATION_270_DEGREES:
    default:
      return this->get_width_internal();
  }
}

static const uint8_t PIXEL_MODES[] = {0, 16, 18, 24};

void MipiDsi::dump_config() {
  ESP_LOGCONFIG(TAG,
                "MIPI_DSI RGB LCD"
                "\n  Model: %s"
                "\n  Width: %u"
                "\n  Height: %u"
                "\n  Rotation: %d degrees"
                "\n  DSI Lanes: %u"
                "\n  Lane Bit Rate: %.0fMbps"
                "\n  HSync Pulse Width: %u"
                "\n  HSync Back Porch: %u"
                "\n  HSync Front Porch: %u"
                "\n  VSync Pulse Width: %u"
                "\n  VSync Back Porch: %u"
                "\n  VSync Front Porch: %u"
                "\n  Buffer Color Depth: %d bit"
                "\n  Display Pixel Mode: %d bit"
                "\n  Invert Colors: %s"
                "\n  Pixel Clock: %.1fMHz",
                this->model_, this->width_, this->height_, this->rotation_, this->lanes_, this->lane_bit_rate_,
                this->hsync_pulse_width_, this->hsync_back_porch_, this->hsync_front_porch_, this->vsync_pulse_width_,
                this->vsync_back_porch_, this->vsync_front_porch_, (3 - this->color_depth_) * 8, this->pixel_mode_,
                YESNO(this->invert_colors_), this->pclk_frequency_);
  LOG_PIN("  Reset Pin ", this->reset_pin_);
  ESP_LOGCONFIG(TAG, "  Double buffer: %s", YESNO(this->double_buffer_));
  ESP_LOGCONFIG(TAG, "  Frame transitions: %s", YESNO(this->frame_transitions_));
}
}  // namespace esphome::mipi_dsi
#endif  // USE_ESP32_VARIANT_ESP32P4
