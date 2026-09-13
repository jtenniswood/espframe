#pragma once

#include "esphome/components/espframe/photo_buffer_ownership.h"
#include "esphome/components/espframe/photo_buffer_lvgl.h"
#include "esphome/components/espframe/slideshow_component.h"
#include "esphome/components/remote_image/remote_image.h"
#include "esphome/core/application.h"
#include "esp_heap_caps.h"
#include <array>
#include <cstring>

// Optional hardware-only probe. It allocates its own images and hidden widgets;
// it never changes the slideshow's images, settings, network or history.
namespace esphome::espframe {
class PhotoBufferProbeImage : public remote_image::OnlineImage {
 public:
  PhotoBufferProbeImage()
      : OnlineImage("http://localhost/unused", 640, 800, remote_image::AUTO,
                    image::IMAGE_TYPE_RGB565, image::TRANSPARENCY_OPAQUE, 256, false) {}
  bool populate() {
    if (this->resize_(640, 800) != 1024000) return false;
    std::memset(this->buffer_, 0x5a, 1024000);
    this->data_start_ = this->buffer_;
    this->width_ = this->buffer_width_;
    this->height_ = this->buffer_height_;
    this->get_lv_image_dsc();
    return true;
  }
  bool intact() const {
    if (this->get_buffer_capacity() != 1024000) return false;
    for (size_t i = 0; i < 1024000; ++i) if (this->buffer_[i] != 0x5a) return false;
    return true;
  }
};

inline void run_photo_buffer_device_probe() {
  auto *left = lv_image_create(lv_screen_active());
  auto *right = lv_image_create(lv_screen_active());
  lv_obj_add_flag(left, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(right, LV_OBJ_FLAG_HIDDEN);
  PortraitBufferBindings bindings{left, right};
  std::array<PhotoBufferProbeImage, 4> images;
  std::array<PhotoBufferProbeImage *, 4> pointers{&images[0], &images[1], &images[2], &images[3]};
  const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  const size_t before = heap_caps_get_free_size(caps);
  const size_t largest_before = heap_caps_get_largest_free_block(caps);
  bool passed = true;
  for (auto &image : images) passed &= image.populate();
  const size_t allocated_free = heap_caps_get_free_size(caps);
  size_t released = 0;
  if (passed) {
    SlideshowRuntimeState state;
    state.active_slot_displayed = true;
    state.portrait.is_pair = state.portrait.using_preload = true;
    lv_image_set_src(left, images[2].get_lv_image_dsc());
    lv_image_set_src(right, images[3].get_lv_image_dsc());
    auto result = reclaim_portrait_buffers(portrait_buffer_owners(state, false, false, false), pointers, bindings);
    passed &= result.released_bytes == 2048000 && result.retained_bytes == 2048000;
    passed &= images[2].intact() && images[3].intact();
    released += result.released_bytes;
    state.portrait.reset();
    result = reclaim_portrait_buffers(portrait_buffer_owners(state, false, false, true), pointers, bindings);
    passed &= result.released_bytes == 2048000 && result.retained_bytes == 0;
    passed &= lv_image_get_src(left) == nullptr && lv_image_get_src(right) == nullptr;
    released += result.released_bytes;
  }
  ESP_LOGI("photo-buffer-test", "initial passed=%d released=%u psram_before=%u allocated_free=%u after=%u",
           passed, (unsigned) released, (unsigned) before, (unsigned) allocated_free,
           (unsigned) heap_caps_get_free_size(caps));
  for (int cycle = 0; cycle < 20 && passed; ++cycle) {
    for (auto &image : images) passed &= image.populate();
    if (!passed) break;
    auto result = reclaim_portrait_buffers({true, false, false, true}, pointers, bindings);
    passed &= result.released_bytes == 4096000 && result.retained_bytes == 0;
    App.feed_wdt();
  }
  lv_obj_delete(left);
  lv_obj_delete(right);
  for (auto &image : images) image.release();
  ESP_LOGI("photo-buffer-test", "cycles=20 passed=%d psram_after=%u largest_before=%u largest_after=%u",
           passed, (unsigned) heap_caps_get_free_size(caps), (unsigned) largest_before,
           (unsigned) heap_caps_get_largest_free_block(caps));
}
}  // namespace esphome::espframe
