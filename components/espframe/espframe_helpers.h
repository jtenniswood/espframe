#pragma once
#include "date_utils.h"
#include "immich_helpers.h"
#include "sun_calc.h"
#include <string>
#include <cstdint>

static constexpr int MAX_ERROR_RETRIES = 3;
static constexpr int ACCENT_GRID_SIZE = 20;

struct PhotoMeta {
  std::string asset_id, image_url, date, location, person;
  int year = 0, month = 0;
  uint16_t zoom = ZOOM_IDENTITY;
};

struct SlotMeta : PhotoMeta {
  std::string datetime, companion_url, pending_asset_id;
  bool ready = false, is_portrait = false;
};

struct DisplayMeta : PhotoMeta {
  bool valid = false;
};

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

#ifdef USE_JSON
inline std::string parse_immich_asset_and_fill_slot(const std::string &body,
                                                    const std::string &base_url,
                                                    int slot,
                                                    SlotMeta &s0, SlotMeta &s1, SlotMeta &s2) {
  ImmichAssetMeta tmp;
  std::string img_url = parse_immich_asset(body, base_url, &tmp);
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
