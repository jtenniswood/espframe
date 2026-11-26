#include "display.h"
#if SWIPE_LINKED_SCANOUT
#include "linked_dpi.h"
#endif

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

#if defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3) != SWIPE_EARLY_P4
#error "Use a fresh build directory for each SWIPE_BOARD silicon profile"
#endif

namespace espframe::native {
namespace {

constexpr gpio_num_t panel_reset = GPIO_NUM_27;
constexpr int backlight_pin = 23;

uint32_t backlight_duty(uint8_t brightness) { return (1023u * brightness + 50) / 100; }

struct PanelCommand {
  uint8_t command;
  uint8_t parameter;
  uint16_t delay_ms;
  bool has_parameter;
};

#include "panel_init.inc"

void reset_panel() {
  ESP_ERROR_CHECK(gpio_reset_pin(panel_reset));
  ESP_ERROR_CHECK(gpio_set_direction(panel_reset, GPIO_MODE_OUTPUT));
  ESP_ERROR_CHECK(gpio_set_level(panel_reset, 1));
  vTaskDelay(pdMS_TO_TICKS(5));
  ESP_ERROR_CHECK(gpio_set_level(panel_reset, 0));
  vTaskDelay(pdMS_TO_TICKS(5));
  ESP_ERROR_CHECK(gpio_set_level(panel_reset, 1));
}

}  // namespace

void Display::init(bool request_snapshot) {
  esp_ldo_channel_config_t ldo_config{};
  ldo_config.chan_id = 3;
  ldo_config.voltage_mv = 2500;
  esp_ldo_channel_handle_t ldo{};
  ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_config, &ldo));

  esp_lcd_dsi_bus_config_t bus_config{};
  bus_config.bus_id = 0;
  bus_config.num_data_lanes = 2;
  bus_config.lane_bit_rate_mbps = lane_rate_mbps;
  esp_lcd_dsi_bus_handle_t bus{};
  ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &bus));

  esp_lcd_dbi_io_config_t io_config{};
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  esp_lcd_panel_io_handle_t io{};
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(bus, &io_config, &io));

  esp_lcd_dpi_panel_config_t panel_config{};
  panel_config.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
  panel_config.dpi_clock_freq_mhz = pixel_clock_mhz;
  panel_config.in_color_format = LCD_COLOR_FMT_RGB565;
  panel_config.num_fbs = request_snapshot ? 3 : 2;
  panel_config.video_timing.h_size = panel_width;
  panel_config.video_timing.v_size = panel_height;
  panel_config.video_timing.hsync_pulse_width = hsync_pulse;
  panel_config.video_timing.hsync_back_porch = hsync_back;
  panel_config.video_timing.hsync_front_porch = hsync_front;
  panel_config.video_timing.vsync_pulse_width = vsync_pulse;
  panel_config.video_timing.vsync_back_porch = vsync_back;
  panel_config.video_timing.vsync_front_porch = vsync_front;
  auto result = esp_lcd_new_panel_dpi(bus, &panel_config, &this->panel_);
  if (result != ESP_OK && request_snapshot) {
    panel_config.num_fbs = 2;
    result = esp_lcd_new_panel_dpi(bus, &panel_config, &this->panel_);
  }
  ESP_ERROR_CHECK(result);
  this->slots_ = FrameSlots{panel_config.num_fbs};

  reset_panel();
  const int64_t sleep_out_ready = esp_timer_get_time() + 120'000;
  ESP_ERROR_CHECK(esp_lcd_panel_init(this->panel_));
  for (const auto &entry : panel_init) {
    if (entry.delay_ms != 0) {
      vTaskDelay(pdMS_TO_TICKS(entry.delay_ms));
      continue;
    }
    if (entry.command == 0x11) {
      const int64_t remaining = sleep_out_ready - esp_timer_get_time();
      if (remaining > 0)
        vTaskDelay(pdMS_TO_TICKS((remaining + 999) / 1000));
    }
    const auto *parameters = entry.has_parameter ? &entry.parameter : nullptr;
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, entry.command, parameters, entry.has_parameter ? 1 : 0));
    if (entry.command == 0x11)
      vTaskDelay(pdMS_TO_TICKS(10));
  }

  void *first{};
  void *second{};
  void *third{};
  ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(this->panel_, panel_config.num_fbs, &first, &second, &third));
  this->buffers_[0] = static_cast<uint16_t *>(first);
  this->buffers_[1] = static_cast<uint16_t *>(second);
  this->buffers_[2] = static_cast<uint16_t *>(third);
  this->renderer_ = xTaskGetCurrentTaskHandle();
  esp_lcd_dpi_panel_event_callbacks_t callbacks{};
  callbacks.on_frame_buf_complete = frame_done_;
  ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(this->panel_, &callbacks, this));
}

void Display::init_backlight(uint8_t brightness) {
  ledc_timer_config_t timer{};
  timer.speed_mode = LEDC_LOW_SPEED_MODE;
  timer.duty_resolution = LEDC_TIMER_10_BIT;
  timer.timer_num = LEDC_TIMER_0;
  timer.freq_hz = 1000;
  timer.clk_cfg = LEDC_AUTO_CLK;
  ESP_ERROR_CHECK(ledc_timer_config(&timer));
  ledc_channel_config_t channel{};
  channel.gpio_num = backlight_pin;
  channel.speed_mode = LEDC_LOW_SPEED_MODE;
  channel.channel = LEDC_CHANNEL_0;
  channel.timer_sel = LEDC_TIMER_0;
  channel.duty = backlight_duty(brightness);
  ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

void Display::set_backlight(uint8_t brightness) const {
  // The player task owns this channel. The combined API requires the LEDC fade service.
  ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, backlight_duty(brightness)));
  ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}

unsigned Display::nominal_frame_period_us() {
  constexpr auto columns = panel_width + hsync_pulse + hsync_back + hsync_front;
  constexpr auto rows = panel_height + vsync_pulse + vsync_back + vsync_front;
  return (columns * rows + pixel_clock_mhz / 2) / pixel_clock_mhz;
}

bool Display::frame_done_([[maybe_unused]] esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *, void *context) {
  auto &display = *static_cast<Display *>(context);
  portENTER_CRITICAL_ISR(&display.boundary_lock_);
#if SWIPE_LINKED_SCANOUT
  const bool latched = display.awaiting_index_ == espframe_dpi_armed_frame(panel);
#else
  const bool latched = display.awaiting_latch_;
#endif
  if (latched) {
    display.latched_us_ = esp_timer_get_time();
#if SWIPE_LINKED_SCANOUT
    display.awaiting_index_ = FrameSlots::none;
#else
    display.awaiting_latch_ = false;
#endif
  }
  portEXIT_CRITICAL_ISR(&display.boundary_lock_);
  BaseType_t task_woken = pdFALSE;
  if (latched)
    vTaskNotifyGiveFromISR(display.renderer_, &task_woken);
  return task_woken == pdTRUE;
}

std::optional<int64_t> Display::poll_presented() {
  if (!this->slots_.queued())
    return {};
  portENTER_CRITICAL(&this->boundary_lock_);
  const auto latched_us = this->latched_us_;
  portEXIT_CRITICAL(&this->boundary_lock_);
  if (latched_us == 0)
    return {};
  this->slots_.latch();
#if SWIPE_LINKED_SCANOUT
  this->sources_.latch();
#endif
  this->position_ = this->pending_position_;
  return latched_us;
}

void Display::wait_for_frame_or_tick() const {
  // The DMA handoff can free a buffer before the next scheduler tick. Keep
  // the tick timeout so touch and preparation still progress without a swap.
  ulTaskNotifyTake(pdTRUE, 1);
}

uint16_t *Display::writable_buffer() {
  const auto index = this->slots_.writable();
  return index == FrameSlots::none ? nullptr : this->buffers_[index];
}

uint16_t *Display::preparation_buffer() {
  const auto index = this->slots_.preparation_slot();
  return index == FrameSlots::none ? nullptr : this->buffers_[index];
}

uint16_t *Display::reserve_preparation_buffer() {
  const auto index = this->slots_.reserve_preparation();
  return index == FrameSlots::none ? nullptr : this->buffers_[index];
}

void Display::release_preparation_buffer(const uint16_t *buffer) {
  for (unsigned index = 0; index < FrameSlots::none; ++index) {
    if (this->buffers_[index] == buffer) {
      this->slots_.release_preparation(index);
      return;
    }
  }
}

const uint16_t *Display::pin_handoff() {
  if (this->borrowing())
    return nullptr;
  const auto index = this->slots_.pin();
  return index == FrameSlots::none ? nullptr : this->buffers_[index];
}

bool Display::submit(PixelPosition position, [[maybe_unused]] const ScanoutFrame *frame,
                     [[maybe_unused]] std::optional<SlideRange> sources, const uint16_t *prepared) {
  auto index = this->slots_.writable();
  if (prepared) {
    for (unsigned candidate = 0; candidate < FrameSlots::none; ++candidate) {
      if (this->buffers_[candidate] == prepared && this->slots_.can_submit(candidate)) {
        index = candidate;
        break;
      }
    }
    if (index == FrameSlots::none || this->buffers_[index] != prepared)
      return false;
  }
  ESP_ERROR_CHECK(this->slots_.can_submit(index) ? ESP_OK : ESP_ERR_INVALID_STATE);
#if SWIPE_LINKED_SCANOUT
  static_assert(ScanoutFrame::segment_count == ESPFRAME_DPI_SEGMENTS);
  const auto error = espframe_dpi_submit_frame(this->panel_, index, this->buffers_[index],
                                              frame ? frame->sources.data() : nullptr,
                                              frame ? frame->lengths.data() : nullptr,
                                              frame && frame->resisted, frame ? frame->prepare_us : 0);
  if (frame && error != ESP_OK) {
    ESP_LOGW("photo-display", "Segmented submit failed, using a full frame: %s", esp_err_to_name(error));
    return false;
  }
  ESP_ERROR_CHECK(error);
  this->sources_.submit(sources);
#else
  ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(this->panel_, 0, 0, panel_width, panel_height, this->buffers_[index]));
#endif
  // Observe a boundary after the submission so the old front buffer cannot
  // be reused while the panel could still be scanning it.
  portENTER_CRITICAL(&this->boundary_lock_);
  this->latched_us_ = 0;
#if SWIPE_LINKED_SCANOUT
  // The DMA callback may already be in flight with the old index. Only a
  // callback that arms this exact slot can release the preceding photo leases.
  this->awaiting_index_ = index;
#else
  this->awaiting_latch_ = true;
#endif
  portEXIT_CRITICAL(&this->boundary_lock_);
  this->pending_position_ = position;
  this->slots_.submit(index);
  return true;
}

}  // namespace espframe::native
