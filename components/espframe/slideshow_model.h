#pragma once

#include "immich_helpers.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

struct PhotoMeta {
  // Stable metadata shown with a photo, independent of which slideshow slot is
  // currently carrying it.
  std::string asset_id, image_url, date, location, person;
  int year = 0, month = 0, day = 0;
  uint16_t zoom = ZOOM_IDENTITY;
};

struct SlotMeta : PhotoMeta {
  // Runtime state for one slot in the 3-image ring buffer.
  std::string datetime, companion_url, pending_asset_id;
  std::string filter_album_ids, filter_person_ids, filter_tag_ids;
  bool ready = false, is_portrait = false;
};

struct DisplayMeta : PhotoMeta {
  std::string datetime, companion_url;
  std::string filter_album_ids, filter_person_ids, filter_tag_ids;
  bool is_portrait = false;
  bool valid = false;
};

struct SlotFlags {
  // Tracks network work that is currently in flight for each ring-buffer slot.
  bool fetch_in_flight[3] = {false, false, false};
  uint32_t fetch_started_ms[3] = {0, 0, 0};
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

inline void mark_slot_fetch_in_flight(int s, SlotFlags &f, uint32_t now_ms) {
  f.fetch_in_flight[s] = true;
  f.fetch_started_ms[s] = now_ms;
}

inline void clear_slot_fetch_in_flight(int s, SlotFlags &f) {
  f.fetch_in_flight[s] = false;
  f.fetch_started_ms[s] = 0;
}

inline uint32_t slot_fetch_age_ms(int s, const SlotFlags &f, uint32_t now_ms) {
  if (!f.fetch_in_flight[s] || f.fetch_started_ms[s] == 0) return 0;
  return now_ms - f.fetch_started_ms[s];
}

inline bool any_slot_fetch_in_flight(const SlotFlags &f) {
  return f.fetch_in_flight[0] || f.fetch_in_flight[1] || f.fetch_in_flight[2];
}



// Returns true if the slot is ready for display logic, false if stale/ignored.
inline bool handle_slot_download_complete(int slot, SlotMeta &meta,
    SlotFlags &flags, int &nc_count, int &retries) {
  if (meta.asset_id != meta.pending_asset_id) {
    ESP_LOGW("immich", "Slot %d download stale, ignoring", slot);
    clear_slot_fetch_in_flight(slot, flags);
    clear_noncritical(slot, flags, nc_count);
    return false;
  }
  meta.ready = true;
  clear_slot_fetch_in_flight(slot, flags);
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
  if (noncritical && (workflow_busy || nc_count > 0)) {
    clear_noncritical(slot, flags, nc_count);
    clear_slot_fetch_in_flight(slot, flags);
    return false;
  }
  if (noncritical && !flags.noncritical_update[slot]) {
    flags.noncritical_update[slot] = true;
    nc_count++;
  } else if (!noncritical && flags.noncritical_update[slot]) {
    flags.noncritical_update[slot] = false;
    if (nc_count > 0) nc_count--;
  }
  mark_slot_fetch_in_flight(slot, flags, esphome::millis());
  return true;
}


inline SlotMeta& get_slot(int s, SlotMeta &s0, SlotMeta &s1, SlotMeta &s2) {
  return (s == 0) ? s0 : (s == 1) ? s1 : s2;
}

inline void copy_slot_to_display(const SlotMeta &slot, DisplayMeta &disp) {
  static_cast<PhotoMeta&>(disp) = static_cast<const PhotoMeta&>(slot);
  disp.datetime = slot.datetime;
  disp.companion_url = slot.companion_url;
  disp.filter_album_ids = slot.filter_album_ids;
  disp.filter_person_ids = slot.filter_person_ids;
  disp.filter_tag_ids = slot.filter_tag_ids;
  disp.is_portrait = slot.is_portrait;
}

inline void copy_display_to_slot(const DisplayMeta &disp, SlotMeta &slot) {
  static_cast<PhotoMeta&>(slot) = static_cast<const PhotoMeta&>(disp);
  slot.datetime = disp.datetime;
  slot.companion_url = disp.companion_url;
  slot.filter_album_ids = disp.filter_album_ids;
  slot.filter_person_ids = disp.filter_person_ids;
  slot.filter_tag_ids = disp.filter_tag_ids;
  slot.is_portrait = disp.is_portrait;
}


