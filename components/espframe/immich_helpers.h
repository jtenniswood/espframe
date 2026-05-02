#pragma once
#include "date_utils.h"
#include "esp_random.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static constexpr uint16_t ZOOM_IDENTITY = 256;

struct ImmichAssetMeta {
  // Normalized subset of the Immich asset response used by the slideshow UI.
  // Keeping a compact struct avoids spreading JSON field names through YAML
  // lambdas.
  std::string asset_id, image_url, date, location, person;
  std::string datetime;  // localDateTime from asset, for slot display
  int year = 0, month = 0, day = 0;
  bool is_portrait = false;
  bool orientation_known = false;
  uint16_t zoom = ZOOM_IDENTITY;
};

// ============================================================================
// Immich search body builder
// ============================================================================
// Builds the JSON POST body for /api/search/random with optional filters
// for favorites, albums, and people. The `extra` parameter allows injecting
// additional JSON fields (e.g. takenAfter/takenBefore for companion search).

struct ImmichDateRange {
  std::string from;
  std::string to;
  bool relative_skipped_for_invalid_time = false;
};

inline int immich_days_in_month(int year, int month) {
  static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (month == 2) {
    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    return leap ? 29 : 28;
  }
  if (month < 1 || month > 12) return 31;
  return days[month - 1];
}

inline std::string immich_format_iso_date(int year, int month, int day) {
  char buf[11];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
  return std::string(buf);
}

inline ImmichDateRange resolve_immich_date_filter(bool enabled,
                                                  const std::string &mode,
                                                  int amount,
                                                  const std::string &unit,
                                                  bool now_valid,
                                                  int now_year,
                                                  int now_month,
                                                  int now_day,
                                                  const std::string &fixed_from,
                                                  const std::string &fixed_to) {
  ImmichDateRange range;
  if (!enabled) return range;

  if (mode == "Relative Range") {
    if (!now_valid) {
      range.relative_skipped_for_invalid_time = true;
      return range;
    }
    if (amount < 1) amount = 1;
    int year = now_year;
    int month = now_month;
    int day = now_day;
    if (unit == "Years") {
      year -= amount;
    } else {
      int total_months = year * 12 + (month - 1) - amount;
      year = total_months / 12;
      month = (total_months % 12) + 1;
    }
    int max_day = immich_days_in_month(year, month);
    if (day > max_day) day = max_day;
    range.from = immich_format_iso_date(year, month, day);
    range.to = immich_format_iso_date(now_year, now_month, now_day);
    return range;
  }

  range.from = fixed_from;
  range.to = fixed_to;
  return range;
}

inline void append_immich_taken_range(std::string &extra,
                                      const std::string &from,
                                      const std::string &to) {
  if (!from.empty()) {
    extra += "\"takenAfter\":\"" + from + "T00:00:00.000Z\"";
  }
  if (!to.empty()) {
    if (!extra.empty()) extra += ",";
    extra += "\"takenBefore\":\"" + to + "T23:59:59.999Z\"";
  }
}

inline std::string build_immich_date_filter_extra(const ImmichDateRange &range) {
  std::string extra;
  append_immich_taken_range(extra, range.from, range.to);
  return extra;
}

inline std::string build_immich_companion_date_filter_extra(const std::string &day,
                                                           const ImmichDateRange &range) {
  std::string after = day + "T00:00:00.000Z";
  std::string before = day + "T23:59:59.999Z";
  if (!range.from.empty() && range.from > day) after = range.from + "T00:00:00.000Z";
  if (!range.to.empty() && range.to < day) before = range.to + "T23:59:59.999Z";
  return "\"takenAfter\":\"" + after + "\",\"takenBefore\":\"" + before + "\"";
}

inline std::vector<std::string> split_uuid_csv(const std::string &csv) {
  // Home Assistant text fields store album/person IDs as comma-separated text;
  // normalize that into individual UUID strings before building API requests.
  std::vector<std::string> out;
  size_t start = 0;
  while (start < csv.size()) {
    size_t end = csv.find(',', start);
    if (end == std::string::npos)
      end = csv.size();
    size_t s = start, e = end;
    while (s < e && csv[s] == ' ')
      s++;
    while (e > s && csv[e - 1] == ' ')
      e--;
    if (s < e)
      out.emplace_back(csv.substr(s, e - s));
    start = end + 1;
  }
  return out;
}

// Immich treats multiple personIds as AND (asset must include every person).
// For Person source we send one UUID per request so results are any-of over time.
inline std::string pick_one_person_id_for_random_search(const std::string &csv) {
  std::vector<std::string> ids = split_uuid_csv(csv);
  if (ids.empty())
    return "";
  if (ids.size() == 1)
    return ids[0];
  return ids[esp_random() % ids.size()];
}

inline std::string pick_one_uuid_from_csv(const std::string &csv) {
  std::vector<std::string> ids = split_uuid_csv(csv);
  if (ids.empty())
    return "";
  if (ids.size() == 1)
    return ids[0];
  return ids[esp_random() % ids.size()];
}

inline std::string build_uuid_json_array(const std::string &csv) {
  std::vector<std::string> ids = split_uuid_csv(csv);
  std::string result = "[";
  for (size_t i = 0; i < ids.size(); i++) {
    if (i)
      result += ",";
    result += "\"" + ids[i] + "\"";
  }
  result += "]";
  return result;
}

inline std::string build_immich_search_body(int size, bool with_people,
                                             const std::string &photo_source,
                                             const std::string &album_ids,
                                             const std::string &person_ids,
                                             const std::string &extra = "") {
  // Construct the small JSON request body by hand to keep this header usable
  // from ESPHome lambdas without bringing in another JSON writer.
  std::string body = "{\"size\":" + std::to_string(size) +
                      ",\"type\":\"IMAGE\",\"withExif\":true";
  if (with_people) body += ",\"withPeople\":true";
  if (!extra.empty()) body += "," + extra;
  if (photo_source == "Favorites") {
    body += ",\"isFavorite\":true";
  } else if (photo_source == "Album" && !album_ids.empty()) {
    body += ",\"albumIds\":" + build_uuid_json_array(album_ids);
  } else if (photo_source == "Person" && !person_ids.empty()) {
    std::string one = pick_one_person_id_for_random_search(person_ids);
    if (!one.empty())
      body += ",\"personIds\":" + build_uuid_json_array(one);
  }
  body += "}";
  return body;
}

inline bool photo_orientation_matches(const ImmichAssetMeta &meta, const std::string &filter) {
  if (filter == "Any" || filter.empty()) return true;
  if (!meta.orientation_known) return false;
  if (filter == "Portrait Only") return meta.is_portrait;
  if (filter == "Landscape Only") return !meta.is_portrait;
  return true;
}

// ============================================================================
// Immich asset parser — parse JSON asset and fill meta
// ============================================================================
// body: JSON string (single asset object or array with one object).
// base_url: Immich server base URL (no trailing slash).
// out_meta: filled with asset_id, image_url, date, location, person, year,
//           month, day, is_portrait, zoom. Returns the image URL on success,
//           empty string on parse failure.

#ifdef USE_JSON
#include "esphome/components/json/json_util.h"

inline std::string parse_immich_asset_object(JsonObject asset,
                                             const std::string &base_url,
                                             ImmichAssetMeta *out_meta) {
  if (out_meta == nullptr) return "";
  if (asset.isNull() || !asset["id"].is<const char *>())
    return "";

  std::string asset_id = asset["id"].as<std::string>();
  std::string photo_date, photo_location, photo_person;
  int photo_year = 0, photo_month = 0, photo_day = 0;
  bool is_portrait = false;
  bool orientation_known = false;
  auto read_date = [](const std::string &raw, int &year, int &month, int &day) {
    if (raw.size() < 10) return false;
    year = atoi(raw.substr(0, 4).c_str());
    month = atoi(raw.substr(5, 2).c_str());
    day = atoi(raw.substr(8, 2).c_str());
    return year > 0 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
  };

  std::string local_datetime;
  if (asset["localDateTime"].is<const char *>()) {
    std::string raw = asset["localDateTime"].as<std::string>();
    local_datetime = raw;
    if (read_date(raw, photo_year, photo_month, photo_day))
      photo_date = format_photo_date_full(photo_year, photo_month, photo_day);
  }

  JsonObject exif = asset["exifInfo"].as<JsonObject>();
  if (!exif.isNull()) {
    // Prefer location and date from EXIF when available; Immich's localDateTime
    // remains the first choice because it already reflects the library's time
    // zone handling.
    std::string city, country;
    if (exif["city"].is<const char *>()) city = exif["city"].as<std::string>();
    if (exif["country"].is<const char *>()) country = exif["country"].as<std::string>();
    if (!city.empty() && !country.empty()) photo_location = city + ", " + country;
    else if (!city.empty()) photo_location = city;
    else if (!country.empty()) photo_location = country;

    if (photo_date.empty() && exif["dateTimeOriginal"].is<const char *>()) {
      std::string raw = exif["dateTimeOriginal"].as<std::string>();
      if (read_date(raw, photo_year, photo_month, photo_day))
        photo_date = format_photo_date_full(photo_year, photo_month, photo_day);
    }

    int exif_w = 0, exif_h = 0;
    if (exif["exifImageWidth"].is<int>()) exif_w = exif["exifImageWidth"].as<int>();
    if (exif["exifImageHeight"].is<int>()) exif_h = exif["exifImageHeight"].as<int>();
    std::string orientation;
    if (exif["orientation"].is<const char *>()) orientation = exif["orientation"].as<std::string>();
    // EXIF orientations 5-8 mean the stored dimensions are rotated relative to
    // the displayed photo, so swap before deciding whether it is portrait.
    if (orientation == "5" || orientation == "6" || orientation == "7" || orientation == "8")
      std::swap(exif_w, exif_h);
    if (exif_w > 0 && exif_h > 0) {
      is_portrait = (exif_h > exif_w);
      orientation_known = true;
    }
  }

  if (asset["people"].is<JsonArray>()) {
    JsonArray people = asset["people"].as<JsonArray>();
    if (people.size() > 0) {
      JsonObject person = people[0].as<JsonObject>();
      if (person["name"].is<const char *>())
        photo_person = person["name"].as<std::string>();
    }
  }

  std::string img_url = base_url + "/api/assets/" + asset_id + "/thumbnail?size=preview";
  out_meta->asset_id = asset_id;
  out_meta->image_url = img_url;
  out_meta->date = photo_date;
  out_meta->location = photo_location;
  out_meta->year = photo_year;
  out_meta->month = photo_month;
  out_meta->day = photo_day;
  out_meta->person = photo_person;
  out_meta->datetime = local_datetime;
  out_meta->is_portrait = is_portrait;
  out_meta->orientation_known = orientation_known;
  out_meta->zoom = ZOOM_IDENTITY;
  return img_url;
}

inline std::string parse_immich_asset(const std::string &body,
                                      const std::string &base_url,
                                      ImmichAssetMeta *out_meta,
                                      const std::string &orientation_filter = "Any") {
  if (out_meta == nullptr) return "";
  auto doc = esphome::json::parse_json(body);
  if (doc.isNull()) return "";

  if (doc.is<JsonArray>()) {
    JsonArray arr = doc.as<JsonArray>();
    for (size_t i = 0; i < arr.size(); i++) {
      ImmichAssetMeta candidate;
      std::string img_url = parse_immich_asset_object(arr[i].as<JsonObject>(), base_url, &candidate);
      if (img_url.empty()) continue;
      if (!photo_orientation_matches(candidate, orientation_filter)) continue;
      *out_meta = candidate;
      return img_url;
    }
    return "";
  }

  if (doc.is<JsonObject>()) {
    ImmichAssetMeta candidate;
    std::string img_url = parse_immich_asset_object(doc.as<JsonObject>(), base_url, &candidate);
    if (img_url.empty() || !photo_orientation_matches(candidate, orientation_filter)) return "";
    *out_meta = candidate;
    return img_url;
  }

  return "";
}

inline std::string pick_immich_time_bucket(const std::string &body) {
  auto doc = esphome::json::parse_json(body);
  if (doc.isNull() || !doc.is<JsonArray>()) return "";

  struct BucketChoice {
    std::string time_bucket;
    uint32_t count;
  };

  JsonArray buckets = doc.as<JsonArray>();
  std::vector<BucketChoice> choices;
  uint32_t total = 0;

  for (size_t i = 0; i < buckets.size(); i++) {
    JsonObject bucket = buckets[i].as<JsonObject>();
    if (bucket.isNull() || !bucket["timeBucket"].is<const char *>()) continue;
    int count = bucket["count"].is<int>() ? bucket["count"].as<int>() : 1;
    if (count <= 0) count = 1;
    choices.push_back({bucket["timeBucket"].as<std::string>(), static_cast<uint32_t>(count)});
    total += static_cast<uint32_t>(count);
  }

  if (choices.empty() || total == 0) return "";

  uint32_t pick = esp_random() % total;
  uint32_t seen = 0;
  for (const auto &choice : choices) {
    seen += choice.count;
    if (pick < seen) return choice.time_bucket;
  }

  return choices.back().time_bucket;
}

inline std::string pick_immich_timeline_asset_id(const std::string &body,
                                                 const std::string &orientation_filter = "Any") {
  auto doc = esphome::json::parse_json(body);
  if (doc.isNull() || !doc.is<JsonObject>()) return "";

  JsonObject bucket = doc.as<JsonObject>();
  JsonArray ids = bucket["id"].as<JsonArray>();
  if (ids.isNull() || ids.size() == 0) return "";

  JsonArray is_images = bucket["isImage"].as<JsonArray>();
  JsonArray ratios = bucket["ratio"].as<JsonArray>();
  std::vector<std::string> candidates;

  for (size_t i = 0; i < ids.size(); i++) {
    if (!ids[i].is<const char *>()) continue;
    if (!is_images.isNull() && is_images[i].is<bool>() && !is_images[i].as<bool>()) continue;

    if (orientation_filter == "Portrait Only" || orientation_filter == "Landscape Only") {
      if (ratios.isNull()) continue;
      float ratio = 0.0f;
      if (ratios[i].is<float>() || ratios[i].is<double>() || ratios[i].is<int>()) {
        ratio = ratios[i].as<float>();
      }
      if (ratio <= 0.0f) continue;
      bool portrait = ratio < 1.0f;
      if (orientation_filter == "Portrait Only" && !portrait) continue;
      if (orientation_filter == "Landscape Only" && portrait) continue;
    }

    candidates.push_back(ids[i].as<std::string>());
  }

  if (candidates.empty()) return "";
  return candidates[esp_random() % candidates.size()];
}

inline std::string find_immich_portrait_companion_url(const std::string &body,
                                                      const std::string &base_url,
                                                      const std::string &primary_asset_id) {
  auto doc = esphome::json::parse_json(body);
  if (doc.isNull() || !doc.is<JsonArray>()) return "";

  JsonArray arr = doc.as<JsonArray>();
  for (size_t i = 0; i < arr.size(); i++) {
    JsonObject asset = arr[i].as<JsonObject>();
    if (asset.isNull() || !asset["id"].is<const char *>()) continue;

    std::string asset_id = asset["id"].as<std::string>();
    if (asset_id == primary_asset_id) continue;

    JsonObject exif = asset["exifInfo"].as<JsonObject>();
    if (exif.isNull()) continue;

    int width = 0;
    int height = 0;
    if (exif["exifImageWidth"].is<int>()) width = exif["exifImageWidth"].as<int>();
    if (exif["exifImageHeight"].is<int>()) height = exif["exifImageHeight"].as<int>();

    std::string orientation;
    if (exif["orientation"].is<const char *>()) orientation = exif["orientation"].as<std::string>();
    if (orientation == "5" || orientation == "6" || orientation == "7" || orientation == "8")
      std::swap(width, height);

    if (width <= 0 || height <= 0 || height <= width) continue;
    return base_url + "/api/assets/" + asset_id + "/thumbnail?size=preview";
  }

  return "";
}

#endif  // USE_JSON
