#pragma once

#include "../components/espframe/slideshow_component.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

namespace esphome::remote_image {
enum VerticalAlignment { VERTICAL_ALIGN_START, VERTICAL_ALIGN_CENTER };
}

inline void dispatch_require(bool passed, const char *message) {
  if (passed) return;
  std::fprintf(stderr, "Slideshow dispatch FAIL: %s\n", message);
  std::exit(1);
}

struct DispatchTestCore {
  EspFrameSlideshow model;
  EspFrameSlideshow &slideshow() { return model; }
  void record_memory(const char *) {}
};

// Hardware endpoints are replaced; the production command queue, dispatch
// lambda, and ESPHome Script/Delay/Scheduler implementations run unchanged.
struct DispatchTestEndpoint {
  bool state = false;
  int calls = 0;
  std::function<void()> on_call;
  void execute() { ++calls; if (on_call) on_call(); }
  void update() { execute(); }
  bool is_downloading() const { return false; }
  bool is_animating() const { return false; }
  std::string current_option() const { return "0"; }
  void set_fill_mode(bool) {}
  void set_target_size(int, int) {}
  void set_vertical_align(esphome::remote_image::VerticalAlignment) {}
  void set_url(const std::string &) {}
  void abort_download() {}
};

// The host has no LVGL surfaces bound to decoder buffers.
inline constexpr int LV_OBJ_FLAG_HIDDEN = 0;
inline bool lv_obj_has_flag(const DispatchTestEndpoint &, int) { return true; }

struct PortraitBufferBindings {
  PortraitBufferBindings(const DispatchTestEndpoint &, const DispatchTestEndpoint &) {}
  bool is_bound(const DispatchTestEndpoint &) const { return false; }
};

inline bool dispatch_response_active = false;
inline std::vector<int> dispatch_order;

struct DispatchTestRequestState {
  bool current = true;
  bool pending = true;
  int filter_scope_slot = 0;
  bool filter_scope_request_pending() const { return pending; }
  bool filter_scope_request_is_current() const { return pending && current; }
  void begin_filter_scope_request() { pending = true; current = true; }
  void clear_filter_scope_request() { pending = false; current = false; }
};
