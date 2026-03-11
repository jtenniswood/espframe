#pragma once
#include <string>
#include <cstring>

struct SlotMeta {
  std::string asset_id;
  std::string image_url;
  std::string date;
  std::string location;
  std::string person;
  std::string datetime;
  std::string companion_url;
  std::string pending_asset_id;
  int year = 0;
  int month = 0;
  uint16_t zoom = 256;
  bool ready = false;
  bool is_portrait = false;
};

struct DisplayMeta {
  std::string asset_id;
  std::string image_url;
  std::string date;
  std::string location;
  std::string person;
  int year = 0;
  int month = 0;
  uint16_t zoom = 256;
  bool valid = false;
};

static const char *MONTH_NAMES[] = {
  "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

inline std::string strip_trailing_slashes(const std::string &url) {
  std::string result = url;
  while (!result.empty() && result.back() == '/') result.pop_back();
  return result;
}

inline std::string format_time_ago(int photo_year, int photo_month, int now_year, int now_month) {
  if (photo_year <= 0) return "";
  int months_ago = (now_year - photo_year) * 12 + (now_month - photo_month);
  if (months_ago >= 12) {
    int years = months_ago / 12;
    if (years == 1) return "1 year ago";
    return str_sprintf("%d years ago", years);
  }
  if (months_ago == 1) return "1 month ago";
  if (months_ago > 1) return str_sprintf("%d months ago", months_ago);
  return "";
}

inline std::string format_photo_date(int year, int month) {
  if (month >= 1 && month <= 12)
    return str_sprintf("%s %d", MONTH_NAMES[month], year);
  return "";
}

inline void copy_slot_to_display(const SlotMeta &slot, DisplayMeta &disp) {
  disp.asset_id = slot.asset_id;
  disp.image_url = slot.image_url;
  disp.date = slot.date;
  disp.location = slot.location;
  disp.person = slot.person;
  disp.year = slot.year;
  disp.month = slot.month;
  disp.zoom = slot.zoom;
}

inline void copy_display_to_slot(const DisplayMeta &disp, SlotMeta &slot) {
  slot.asset_id = disp.asset_id;
  slot.image_url = disp.image_url;
  slot.date = disp.date;
  slot.location = disp.location;
  slot.person = disp.person;
  slot.year = disp.year;
  slot.month = disp.month;
  slot.zoom = disp.zoom;
}
