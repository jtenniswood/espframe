#include "frame_blend.h"

#include "slide_geometry.h"

#include <initializer_list>

namespace espframe::native {

bool FrameBlend::init() {
  ppa_client_config_t config{};
  config.oper_type = PPA_OPERATION_BLEND;
  config.max_pending_trans_num = 1;
  // Match the existing Guition driver's burst length to leave time for scanout.
  config.data_burst_length = PPA_DATA_BURST_LENGTH_32;
  if (ppa_register_client(&config, &client_) != ESP_OK)
    return false;

  ppa_event_callbacks_t callbacks{};
  callbacks.on_trans_done = completed_;
  if (ppa_client_register_event_callbacks(client_, &callbacks) == ESP_OK)
    return true;
  release();
  return false;
}

void FrameBlend::release() {
  if (client_) {
    ppa_unregister_client(client_);
    client_ = nullptr;
  }
}

bool FrameBlend::completed_(ppa_client_handle_t, ppa_event_data_t *, void *context) {
  auto &blend = *static_cast<FrameBlend *>(context);
  blend.complete_.store(true, std::memory_order_release);
  return false;
}

bool FrameBlend::start(const uint16_t *background, const uint16_t *foreground, uint16_t *output, uint8_t opacity,
                       std::optional<uint32_t> solid_foreground) {
  if (!client_ || pending_)
    return false;
  ppa_blend_oper_config_t config{};
  for (auto *input : {&config.in_bg, &config.in_fg}) {
    input->pic_w = input->block_w = panel_width;
    input->pic_h = input->block_h = panel_height;
    input->blend_cm = PPA_BLEND_COLOR_MODE_RGB565;
  }
  config.in_bg.buffer = background;
  config.in_fg.buffer = foreground;
  config.out.buffer = output;
  config.out.buffer_size = pixel_count * sizeof(*output);
  config.out.pic_w = panel_width;
  config.out.pic_h = panel_height;
  config.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;
  config.bg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  config.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  config.fg_alpha_fix_val = opacity;
  if (solid_foreground) {
    // A4 with fixed alpha and color ignores the foreground pixels, so the
    // other cached slide can back the read without allocating a solid frame.
    config.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_A4;
    config.fg_fix_rgb_val.r = (*solid_foreground >> 16) & 0xFF;
    config.fg_fix_rgb_val.g = (*solid_foreground >> 8) & 0xFF;
    config.fg_fix_rgb_val.b = *solid_foreground & 0xFF;
  }
  config.mode = PPA_TRANS_MODE_NON_BLOCKING;
  config.user_data = this;
  complete_.store(false, std::memory_order_relaxed);
  pending_ = ppa_do_blend(client_, &config) == ESP_OK;
  return pending_;
}

bool FrameBlend::poll_complete() {
  if (!pending_ || !complete_.load(std::memory_order_acquire))
    return false;
  pending_ = false;
  return true;
}

}  // namespace espframe::native
