#pragma once
#include "date_utils.h"
#include "immich_helpers.h"
#include "sun_calc.h"
#include <string>
#include <cstdint>
#include <cstring>
#include <cmath>
#include "esp_heap_caps.h"

#ifdef USE_LVGL
#include "esphome/components/image/image.h"
#endif

// Shared helpers used from ESPHome YAML lambdas. Keeping this logic in C++ keeps
// the YAML readable and gives the slideshow, backlight, accent, and warm-tone
// flows a single set of small, testable building blocks.
static constexpr int MAX_ERROR_RETRIES = 3;
static constexpr int ACCENT_GRID_SIZE = 20;
static constexpr int WARM_TONE_LEAD_MINUTES = 60;
static constexpr int ACCENT_COL_BUF_MAX = 2560;

inline int minutes_since_midnight(int h, int m) { return h * 60 + m; }

inline bool is_daytime(int now_h, int now_m, int rise_h, int rise_m, int set_h, int set_m) {
  int now_min = minutes_since_midnight(now_h, now_m);
  int rise_min = minutes_since_midnight(rise_h, rise_m);
  int set_min = minutes_since_midnight(set_h, set_m);
  return now_min >= rise_min && now_min < set_min;
}

inline std::string format_time_12h(int h, int m) {
  char buf[16];
  const char *suffix = (h >= 12) ? "PM" : "AM";
  int dh = h % 12;
  if (dh == 0) dh = 12;
  snprintf(buf, sizeof(buf), "%d:%02d %s", dh, m, suffix);
  return buf;
}

inline bool is_http_auth_error(int status) { return status == 401; }
inline bool is_http_retryable(int status) { return status >= 500 || status == 429; }
inline bool is_http_client_error(int status) {
  return status >= 400 && status < 500 && !is_http_auth_error(status) && status != 429;
}

struct PhotoMeta {
  // Stable metadata shown with a photo, independent of which slideshow slot is
  // currently carrying it.
  std::string asset_id, image_url, date, location, person;
  int year = 0, month = 0;
  uint16_t zoom = ZOOM_IDENTITY;
};

struct SlotMeta : PhotoMeta {
  // Runtime state for one slot in the 3-image ring buffer.
  std::string datetime, companion_url, pending_asset_id;
  bool ready = false, is_portrait = false;
};

struct DisplayMeta : PhotoMeta {
  bool valid = false;
};

struct SlotFlags {
  // Tracks network work that is currently in flight for each ring-buffer slot.
  bool fetch_in_flight[3] = {false, false, false};
  bool noncritical_update[3] = {false, false, false};
};

struct PortraitState {
  // Coordinates the multi-step portrait pairing workflow so a pair is only
  // displayed once both left and right images are ready.
  bool left_ready = false, right_ready = false;
  bool no_companion_active = false, left_requested = false, right_requested = false;
  bool companion_found = false, is_pair = false;
  bool using_preload = false, workflow_busy = false;
  void reset() { *this = PortraitState{}; }
};

inline void clear_noncritical(int s, SlotFlags &f, int &nc_count) {
  if (f.noncritical_update[s]) {
    f.noncritical_update[s] = false;
    if (nc_count > 0) nc_count--;
  }
}

// Returns true if the slot is ready for display logic, false if stale/ignored.
inline bool handle_slot_download_complete(int slot, SlotMeta &meta,
    SlotFlags &flags, int &nc_count, int &retries) {
  if (meta.asset_id != meta.pending_asset_id) {
    ESP_LOGW("immich", "Slot %d download stale, ignoring", slot);
    flags.fetch_in_flight[slot] = false;
    clear_noncritical(slot, flags, nc_count);
    return false;
  }
  meta.ready = true;
  flags.fetch_in_flight[slot] = false;
  clear_noncritical(slot, flags, nc_count);
  retries = 0;
  ESP_LOGD("immich", "Slot %d ready: %s", slot, meta.asset_id.c_str());
  return true;
}

// Manage noncritical_update flag for deferred slot image updates.
// Returns false if the update should be skipped (portrait workflow busy or another noncritical in-flight).
inline bool prepare_deferred_slot_update(int slot, int active_slot, SlotFlags &flags,
    bool workflow_busy, int &nc_count) {
  bool noncritical = (slot != active_slot);
  if (noncritical && (workflow_busy || nc_count > 0)) return false;
  if (noncritical && !flags.noncritical_update[slot]) {
    flags.noncritical_update[slot] = true;
    nc_count++;
  } else if (!noncritical && flags.noncritical_update[slot]) {
    flags.noncritical_update[slot] = false;
    if (nc_count > 0) nc_count--;
  }
  flags.fetch_in_flight[slot] = true;
  return true;
}

inline void log_immich_pipeline_diag(const char *reason, uint32_t now_ms, uint32_t &last_diag_ms,
    int active_slot, int target_slot, bool active_displayed, const SlotFlags &flags,
    const PortraitState &portrait, int nc_count, bool preload_nc_in_flight,
    int portrait_preload_slot, bool portrait_preload_left_ready, bool portrait_preload_right_ready,
    int companion_target_slot, int api_retries, int download_retries, uint32_t retry_cooldown_until_ms) {
  if (last_diag_ms != 0 && (now_ms - last_diag_ms) < 5000) return;
  last_diag_ms = now_ms;

  size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  size_t largest_heap = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  uint32_t cooldown_left = (retry_cooldown_until_ms != 0 && now_ms < retry_cooldown_until_ms)
                               ? (retry_cooldown_until_ms - now_ms)
                               : 0;

  ESP_LOGW("diag",
           "IMMICH-DIAG %s heap=%u largest=%u active=%d target=%d displayed=%d "
           "slot_flight=%d%d%d nc=%d preload_nc=%d portrait_busy=%d left_req=%d "
           "left_ready=%d right_req=%d right_ready=%d companion=%d pair=%d "
           "preload_slot=%d preload_ready=%d/%d companion_slot=%d retries=%d/%d cooldown=%u",
           reason, (unsigned) free_heap, (unsigned) largest_heap, active_slot, target_slot,
           active_displayed, flags.fetch_in_flight[0], flags.fetch_in_flight[1],
           flags.fetch_in_flight[2], nc_count, preload_nc_in_flight, portrait.workflow_busy,
           portrait.left_requested, portrait.left_ready, portrait.right_requested,
           portrait.right_ready, portrait.companion_found, portrait.is_pair,
           portrait_preload_slot, portrait_preload_left_ready, portrait_preload_right_ready,
           companion_target_slot, api_retries, download_retries, (unsigned) cooldown_left);
}

inline std::string decode_url_commas(const std::string &input) {
  std::string v = input;
  for (size_t p = v.find("%2C"); p != std::string::npos; p = v.find("%2C", p + 1))
    v.replace(p, 3, ",");
  for (size_t p = v.find("%2c"); p != std::string::npos; p = v.find("%2c", p + 1))
    v.replace(p, 3, ",");
  return v;
}

inline SlotMeta& get_slot(int s, SlotMeta &s0, SlotMeta &s1, SlotMeta &s2) {
  return (s == 0) ? s0 : (s == 1) ? s1 : s2;
}

inline void copy_slot_to_display(const SlotMeta &slot, DisplayMeta &disp) {
  static_cast<PhotoMeta&>(disp) = static_cast<const PhotoMeta&>(slot);
}

inline void copy_display_to_slot(const DisplayMeta &disp, SlotMeta &slot) {
  static_cast<PhotoMeta&>(slot) = static_cast<const PhotoMeta&>(disp);
}

// ============================================================================
// Immich asset parser — parse JSON and fill one of the three slot metas
// ============================================================================
// body: JSON string (single asset object or array with one object).
// base_url: Immich server base URL (no trailing slash).
// slot: target slot index (0, 1, or 2).
// s0, s1, s2: references to the three SlotMeta globals.
// Returns the image URL on success, empty string on parse failure.

// ============================================================================
// Accent color fill — detect letterbox bars and fill with dominant accent
// ============================================================================
#ifdef USE_LVGL
template<typename T> auto get_lv_image_descriptor_(T *img, int) -> decltype(img->get_lv_image_dsc()) {
  return img->get_lv_image_dsc();
}

template<typename T> auto get_lv_image_descriptor_(T *img, long) -> decltype(img->get_lv_img_dsc()) {
  return img->get_lv_img_dsc();
}

inline void fill_accent_color(esphome::image::Image *img) {
  int img_w = img->get_width();
  int img_h = img->get_height();
  if (img_w <= 0 || img_h <= 0) return;

  lv_img_dsc_t *dsc = get_lv_image_descriptor_(img, 0);
  const uint8_t *data = dsc->data;
  if (!data) return;

  // Detect black letterbox/pillarbox bars by scanning from the center lines
  // until the real photo content begins.
  int x_off = 0;
  int mid_y = img_h / 2;
  for (int x = 0; x < img_w / 2; x++) {
    int pos = (x + mid_y * img_w) * 2;
    uint16_t px = data[pos] | (data[pos + 1] << 8);
    if (px != 0x0000) { x_off = x; break; }
  }
  int y_off = 0;
  int mid_x = img_w / 2;
  for (int y = 0; y < img_h / 2; y++) {
    int pos = (mid_x + y * img_w) * 2;
    uint16_t px = data[pos] | (data[pos + 1] << 8);
    if (px != 0x0000) { y_off = y; break; }
  }

  if (x_off == 0 && y_off == 0) return;

  int content_w = img_w - 2 * x_off;
  int content_h = img_h - 2 * y_off;
  int grid = ACCENT_GRID_SIZE;
  int step_x = content_w / grid;
  int step_y = content_h / grid;
  if (step_x < 1) step_x = 1;
  if (step_y < 1) step_y = 1;

  // Sample the visible photo area and weight saturated colors more heavily so
  // the fill color feels connected to the photo instead of averaging to gray.
  int64_t r_wsum = 0, g_wsum = 0, b_wsum = 0;
  int64_t w_total = 0;

  for (int sy = y_off + step_y / 2; sy < img_h - y_off; sy += step_y) {
    for (int sx = x_off + step_x / 2; sx < img_w - x_off; sx += step_x) {
      int pos = (sx + sy * img_w) * 2;
      uint16_t rgb565 = data[pos] | (data[pos + 1] << 8);
      int r = ((rgb565 >> 11) & 0x1F);
      int g = ((rgb565 >> 5) & 0x3F);
      int b = (rgb565 & 0x1F);
      r = (r << 3) | (r >> 2);
      g = (g << 2) | (g >> 4);
      b = (b << 3) | (b >> 2);
      int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
      int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
      int sat = mx - mn;
      int weight = sat * sat + 1;
      r_wsum += (int64_t)r * weight;
      g_wsum += (int64_t)g * weight;
      b_wsum += (int64_t)b * weight;
      w_total += weight;
    }
  }

  if (w_total <= 0) return;

  int r = (int)(r_wsum / w_total);
  int g = (int)(g_wsum / w_total);
  int b = (int)(b_wsum / w_total);

  // Use a dimmer version of the sampled color so the bars support the photo
  // rather than competing with it.
  int dr = r / 2, dg = g / 2, db = b / 2;
  uint16_t accent_565 = ((dr >> 3) << 11) | ((dg >> 2) << 5) | (db >> 3);
  uint8_t lo = accent_565 & 0xFF;
  uint8_t hi = (accent_565 >> 8) & 0xFF;

  uint8_t *buf = const_cast<uint8_t*>(data);
  int row_bytes = img_w * 2;

  if (y_off > 0) {
    for (int x = 0; x < img_w; x++) {
      buf[x * 2] = lo; buf[x * 2 + 1] = hi;
    }
    for (int y = 1; y < y_off; y++)
      memcpy(buf + y * row_bytes, buf, row_bytes);
    for (int y = img_h - y_off; y < img_h; y++)
      memcpy(buf + y * row_bytes, buf, row_bytes);
  }

  if (x_off > 0) {
    int col_bytes = x_off * 2;
    uint8_t col_buf[ACCENT_COL_BUF_MAX];
    if (col_bytes > (int)sizeof(col_buf)) return;
    for (int x = 0; x < x_off; x++) {
      col_buf[x * 2] = lo; col_buf[x * 2 + 1] = hi;
    }
    for (int y = y_off; y < img_h - y_off; y++) {
      int row = y * row_bytes;
      memcpy(buf + row, col_buf, col_bytes);
      memcpy(buf + row + (img_w - x_off) * 2, col_buf, col_bytes);
    }
  }
}
#endif  // USE_LVGL

// ============================================================================
// Warm tone helpers — LUT-based RGB565 tinting
// ============================================================================

inline float calc_sun_warmth(int now_min, int rise_min, int set_min, int lead_min) {
  // Returns a 0..1 intensity: fully warm before sunrise/after sunset, then
  // cross-faded around sunrise and sunset.
  if (now_min >= set_min) return 1.0f;
  if (now_min >= set_min - lead_min)
    return (float)(now_min - (set_min - lead_min)) / lead_min;
  if (now_min < rise_min) return 1.0f;
  if (now_min < rise_min + lead_min)
    return 1.0f - (float)(now_min - rise_min) / lead_min;
  return 0.0f;
}

struct WarmToneLuts {
  uint8_t r[32];
  uint8_t g[64];
  uint8_t b[32];
};

inline void build_warm_tone_luts(float last_w, float new_w, WarmToneLuts &luts) {
  // Build lookup tables that first undo the previous tint, then apply the new
  // tint. This prevents repeated warm-tone updates from permanently drifting
  // the image colors.
  float r_undo = (last_w > 0.005f) ? 1.0f / (1.0f + last_w * 0.06f) : 1.0f;
  float g_undo = (last_w > 0.005f) ? 1.0f / (1.0f - last_w * 0.07f) : 1.0f;
  float b_undo = (last_w > 0.005f) ? 1.0f / (1.0f - last_w * 0.28f) : 1.0f;
  float r_apply = 1.0f + new_w * 0.06f;
  float g_apply = 1.0f - new_w * 0.07f;
  float b_apply = 1.0f - new_w * 0.28f;

  for (int i = 0; i < 32; i++) {
    int v = (i << 3) | (i >> 2);
    int nv = (int)(v * r_undo * r_apply);
    luts.r[i] = (uint8_t)((nv > 255 ? 255 : (nv < 0 ? 0 : nv)) >> 3);
  }
  for (int i = 0; i < 64; i++) {
    int v = (i << 2) | (i >> 4);
    int nv = (int)(v * g_undo * g_apply);
    luts.g[i] = (uint8_t)((nv > 255 ? 255 : (nv < 0 ? 0 : nv)) >> 2);
  }
  for (int i = 0; i < 32; i++) {
    int v = (i << 3) | (i >> 2);
    int nv = (int)(v * b_undo * b_apply);
    luts.b[i] = (uint8_t)((nv > 255 ? 255 : (nv < 0 ? 0 : nv)) >> 3);
  }
}

#ifdef USE_LVGL
inline void tint_image_buffer(esphome::image::Image *img, const WarmToneLuts &luts) {
  if (!img) return;
  lv_img_dsc_t *dsc = get_lv_image_descriptor_(img, 0);
  if (!dsc || !dsc->data) return;
  uint8_t *buf = const_cast<uint8_t*>(dsc->data);
  int total = img->get_width() * img->get_height();
  // RGB565 stores only 5/6/5 bits per channel, so the LUTs operate directly on
  // those channel indices instead of converting every pixel through RGB888.
  for (int i = 0; i < total; i++) {
    int pos = i * 2;
    uint16_t px = buf[pos] | (buf[pos + 1] << 8);
    uint8_t r5 = luts.r[(px >> 11) & 0x1F];
    uint8_t g6 = luts.g[(px >> 5) & 0x3F];
    uint8_t b5 = luts.b[px & 0x1F];
    uint16_t out = (r5 << 11) | (g6 << 5) | b5;
    buf[pos]     = out & 0xFF;
    buf[pos + 1] = (out >> 8) & 0xFF;
  }
}
#endif  // USE_LVGL

#ifdef USE_JSON
inline std::string parse_immich_asset_and_fill_slot(const std::string &body,
                                                    const std::string &base_url,
                                                    int slot,
                                                    SlotMeta &s0, SlotMeta &s1, SlotMeta &s2,
                                                    const std::string &orientation_filter = "Any") {
  // Parse once into an intermediate struct, then copy the fields into the slot
  // selected by the YAML state machine.
  ImmichAssetMeta tmp;
  std::string img_url = parse_immich_asset(body, base_url, &tmp, orientation_filter);
  if (img_url.empty()) return "";

  SlotMeta *meta = (slot == 0) ? &s0 : (slot == 1) ? &s1 : &s2;
  meta->asset_id = tmp.asset_id;
  meta->image_url = tmp.image_url;
  meta->date = tmp.date;
  meta->location = tmp.location;
  meta->person = tmp.person;
  meta->year = tmp.year;
  meta->month = tmp.month;
  meta->zoom = tmp.zoom;
  meta->datetime = tmp.datetime;
  meta->companion_url = "";
  meta->pending_asset_id = tmp.asset_id;
  meta->is_portrait = tmp.is_portrait;
  return img_url;
}
#endif  // USE_JSON
