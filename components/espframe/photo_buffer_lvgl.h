#pragma once

#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/misc/cache/instance/lv_image_header_cache.h"

// Only the pair widgets can hold portrait descriptors. A hidden pair can be
// detached after a completed single-image display; a hidden page alone cannot.
struct PortraitBufferBindings {
  lv_obj_t *left;
  lv_obj_t *right;

  template<typename Image> bool prepare_release(Image *image, bool detach_hidden_sources) {
    auto *descriptor = image->get_lv_image_dsc();
    const bool left_bound = lv_img_get_src(this->left) == descriptor;
    const bool right_bound = lv_img_get_src(this->right) == descriptor;
    if ((left_bound || right_bound) && !detach_hidden_sources) return false;
    if (left_bound) lv_img_set_src(this->left, nullptr);
    if (right_bound) lv_img_set_src(this->right, nullptr);
    lv_image_cache_drop(descriptor);
    lv_image_header_cache_drop(descriptor);
    return true;
  }
};
