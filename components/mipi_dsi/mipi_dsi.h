//
// Created by Clyde Stubbs on 29/10/2023.
//
#pragma once

// only applicable on ESP32-P4
#ifdef USE_ESP32_VARIANT_ESP32P4
#include "esphome/core/component.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/core/gpio.h"

#include "esphome/components/display/display.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"

#include "esp_lcd_mipi_dsi.h"
#include "driver/ppa.h"

namespace esphome::mipi_dsi {

constexpr static const char *const TAG = "display.mipi_dsi";
const uint8_t SW_RESET_CMD = 0x01;
const uint8_t SLEEP_OUT = 0x11;
const uint8_t SDIR_CMD = 0xC7;
const uint8_t MADCTL_CMD = 0x36;
const uint8_t INVERT_OFF = 0x20;
const uint8_t INVERT_ON = 0x21;
const uint8_t DISPLAY_ON = 0x29;
const uint8_t CMD2_BKSEL = 0xFF;
const uint8_t DELAY_FLAG = 0xFF;
const uint8_t MADCTL_BGR = 0x08;
const uint8_t MADCTL_MX = 0x40;
const uint8_t MADCTL_MY = 0x80;
const uint8_t MADCTL_MV = 0x20;     // row/column swap
const uint8_t MADCTL_XFLIP = 0x02;  // Mirror the display horizontally
const uint8_t MADCTL_YFLIP = 0x01;  // Mirror the display vertically

class MipiDsi final : public display::Display {
 public:
  MipiDsi(size_t width, size_t height, display::ColorBitness color_depth, uint8_t pixel_mode)
      : width_(width), height_(height), color_depth_(color_depth), pixel_mode_(pixel_mode) {}
  display::ColorOrder get_color_mode() { return this->color_mode_; }
  void set_color_mode(display::ColorOrder color_mode) { this->color_mode_ = color_mode; }
  void set_invert_colors(bool invert_colors) { this->invert_colors_ = invert_colors; }
  display::DisplayType get_display_type() override { return display::DisplayType::DISPLAY_TYPE_COLOR; }

  void set_reset_pin(GPIOPin *reset_pin) { this->reset_pin_ = reset_pin; }
  void set_enable_pins(std::vector<GPIOPin *> enable_pins) { this->enable_pins_ = std::move(enable_pins); }
  void set_pclk_frequency(float pclk_frequency) { this->pclk_frequency_ = pclk_frequency; }
  int get_width_internal() override { return this->width_; }
  int get_height_internal() override { return this->height_; }
  void set_hsync_back_porch(uint16_t hsync_back_porch) { this->hsync_back_porch_ = hsync_back_porch; }
  void set_hsync_front_porch(uint16_t hsync_front_porch) { this->hsync_front_porch_ = hsync_front_porch; }
  void set_hsync_pulse_width(uint16_t hsync_pulse_width) { this->hsync_pulse_width_ = hsync_pulse_width; }
  void set_vsync_pulse_width(uint16_t vsync_pulse_width) { this->vsync_pulse_width_ = vsync_pulse_width; }
  void set_vsync_back_porch(uint16_t vsync_back_porch) { this->vsync_back_porch_ = vsync_back_porch; }
  void set_vsync_front_porch(uint16_t vsync_front_porch) { this->vsync_front_porch_ = vsync_front_porch; }
  void set_init_sequence(const std::vector<uint8_t> &init_sequence) { this->init_sequence_ = init_sequence; }
  void set_model(const char *model) { this->model_ = model; }
  void set_lane_bit_rate(float lane_bit_rate) { this->lane_bit_rate_ = lane_bit_rate; }
  void set_lanes(uint8_t lanes) { this->lanes_ = lanes; }
  void set_double_buffer(bool double_buffer) { this->double_buffer_ = double_buffer; }

  void smark_failed(const LogString *message, esp_err_t err);

  void update() override;

  void setup() override;
  void loop() override;

  void draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr, display::ColorOrder order,
                      display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset, int x_pad) override;

  void draw_pixel_at(int x, int y, Color color) override;
  void fill(Color color) override;
  int get_width() override;
  int get_height() override;

  void dump_config() override;

 protected:
  void write_to_display_(int x_start, int y_start, int w, int h, const uint8_t *ptr, int x_offset, int y_offset,
                         int x_pad);
  bool check_buffer_();
  void copy_to_back_buffer_(int x_start, int y_start, int w, int h, const uint8_t *ptr, size_t stride);
  void copy_block_(const uint8_t *src, int src_pic_w, int src_pic_h, int src_x, int src_y, uint8_t *dst, int dst_x,
                   int dst_y, int w, int h);
  void present_();
  void sync_stale_area_();
  GPIOPin *reset_pin_{nullptr};
  std::vector<GPIOPin *> enable_pins_{};
  size_t width_{};
  size_t height_{};
  uint16_t hsync_pulse_width_ = 10;
  uint16_t hsync_back_porch_ = 10;
  uint16_t hsync_front_porch_ = 20;
  uint16_t vsync_pulse_width_ = 10;
  uint16_t vsync_back_porch_ = 10;
  uint16_t vsync_front_porch_ = 10;
  const char *model_{"Unknown"};
  std::vector<uint8_t> init_sequence_{};
  float pclk_frequency_ = 16;  // in MHz
  float lane_bit_rate_{1500};  // in Mbps
  uint8_t lanes_{2};           // 1, 2, 3 or 4 lanes

  bool invert_colors_{};
  display::ColorOrder color_mode_{display::COLOR_ORDER_BGR};
  display::ColorBitness color_depth_;
  uint8_t pixel_mode_{};

  esp_lcd_panel_handle_t handle_{};
  esp_lcd_dsi_bus_handle_t bus_handle_{};
  esp_lcd_panel_io_handle_t io_handle_{};
  SemaphoreHandle_t io_lock_{};
  // Double buffering: LVGL draws into fbs_[1 - front_] while the panel scans
  // fbs_[front_]; loop() swaps them at a frame boundary.
  bool double_buffer_{false};
  uint8_t *fbs_[2]{};
  uint8_t front_{0};
  SemaphoreHandle_t frame_done_{};
  // Frame buffer writes go through the PPA only: it invalidates the cached
  // destination rows, which would discard pending CPU writes to them.
  ppa_client_handle_t ppa_client_{};
  struct Area {
    int x1, y1, x2, y2;  // end-exclusive
  };
  // Areas drawn into the back buffer this frame, and their bounding box.
  std::vector<Area> drawn_{};
  bool dirty_{false};
  Area dirty_area_{};
  // Part of the back buffer that is one frame behind the front buffer.
  bool stale_{false};
  Area stale_area_{};
  uint8_t *buffer_{nullptr};
  uint16_t x_low_{1};
  uint16_t y_low_{1};
  uint16_t x_high_{0};
  uint16_t y_high_{0};
};

}  // namespace esphome::mipi_dsi
#endif
