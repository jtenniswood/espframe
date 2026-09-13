#include <cassert>
#include <cstdio>
#include <vector>
#include "components/espframe/photo_buffer_ownership.h"
#include "components/espframe/photo_buffer_lvgl.h"

static_assert(LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR == 5,
              "Review the cache-lifetime tests when upgrading LVGL");

struct Image {
  std::vector<uint8_t> pixels;
  lv_image_dsc_t descriptor{};
  lv_obj_t *left;
  lv_obj_t *right;
  Image(lv_obj_t *left, lv_obj_t *right) : left(left), right(right) { allocate(640, 800); }
  void allocate(int width, int height) {
    pixels.resize(width * height * 2, 0x5a);
    descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    descriptor.header.w = width;
    descriptor.header.h = height;
    descriptor.header.stride = width * 2;
    descriptor.data_size = pixels.size();
    descriptor.data = pixels.data();
  }
  size_t get_buffer_capacity() const { return pixels.size(); }
  bool is_downloading() const { return false; }
  lv_image_dsc_t *get_lv_image_dsc() { return &descriptor; }
  void release() {
    assert(lv_image_get_src(left) != &descriptor && lv_image_get_src(right) != &descriptor);
    pixels.clear();
    pixels.shrink_to_fit();
    descriptor = {};
  }
};

int main() {
  lv_init();
  auto *display = lv_display_create(1280, 800);
  auto *left = lv_image_create(lv_screen_active());
  auto *right = lv_image_create(lv_screen_active());
  PortraitBufferBindings bindings{left, right};
  std::array<Image, 4> images{Image(left, right), Image(left, right), Image(left, right), Image(left, right)};
  std::array<Image *, 4> pointers{&images[0], &images[1], &images[2], &images[3]};
  lv_image_set_src(left, &images[2].descriptor);
  lv_image_set_src(right, &images[3].descriptor);
  // The displayed preload pair remains pinned even with stale model ownership.
  auto result = esphome::espframe::reclaim_portrait_buffers({true, false, false, false}, pointers, bindings);
  assert(result.released_bytes == 2048000 && result.retained_bytes == 2048000);
  assert(images[2].pixels.front() == 0x5a && images[3].pixels.back() == 0x5a);
  // Once the pair really is hidden, detach both real LVGL source references.
  lv_obj_add_flag(left, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(right, LV_OBJ_FLAG_HIDDEN);
  result = esphome::espframe::reclaim_portrait_buffers({true, false, false, true}, pointers, bindings);
  assert(result.released_bytes == 2048000 && result.retained_bytes == 0);
  assert(lv_image_get_src(left) == nullptr && lv_image_get_src(right) == nullptr);
  // Reuse descriptor addresses with different dimensions. The old cached header
  // must not survive release; repeat to exercise cache/descriptor address reuse.
  for (int cycle = 0; cycle < 100; ++cycle) {
    images[0].allocate(20 + cycle, 30);
    lv_image_header_t header{};
    assert(lv_image_decoder_get_info(&images[0].descriptor, &header) == LV_RESULT_OK);
    assert(header.w == 20 + cycle && header.h == 30);
    lv_image_set_src(left, &images[0].descriptor);
    result = esphome::espframe::reclaim_portrait_buffers({true, false, false, true}, pointers, bindings);
    assert(result.released_bytes == size_t((20 + cycle) * 30 * 2));
    assert(lv_image_get_src(left) == nullptr);
  }
  lv_display_delete(display);
  lv_deinit();
  std::puts("LVGL photo buffer tests passed: live sources pinned, hidden sources detached, 100 cache-reuse cycles");
}
