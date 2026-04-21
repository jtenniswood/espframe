#pragma once
#include "date_utils.h"
#include "esp_random.h"
#include <algorithm>
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
  std::string people, date_taken, image_format, camera, camera_settings, lens;
  std::string datetime;  // localDateTime from asset, for slot display
  int year = 0, month = 0;
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
//           month, is_portrait, zoom. Returns the image URL on success,
//           empty string on parse failure.

#ifdef USE_JSON
#include "esphome/components/json/json_util.h"

inline std::string upper_ascii(std::string value) {
  for (char &c : value) {
    if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
  }
  return value;
}

inline std::string format_decimal_trimmed(double value, int precision = 1) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.*f", precision, value);
  std::string out = buf;
  while (out.size() > 1 && out.back() == '0') out.pop_back();
  if (!out.empty() && out.back() == '.') out.pop_back();
  return out;
}

inline std::string json_string_value(JsonObject obj, const char *key) {
  if (obj[key].is<const char *>()) return obj[key].as<std::string>();
  return "";
}

inline std::string json_number_or_string(JsonObject obj, const char *key, int precision = 1) {
  if (obj[key].is<const char *>()) return trim_ascii_whitespace(obj[key].as<std::string>());
  if (obj[key].is<int>()) return std::to_string(obj[key].as<int>());
  if (obj[key].is<float>()) return format_decimal_trimmed(obj[key].as<float>(), precision);
  if (obj[key].is<double>()) return format_decimal_trimmed(obj[key].as<double>(), precision);
  return "";
}

inline int json_int_value(JsonObject obj, const char *key) {
  if (obj[key].is<int>()) return obj[key].as<int>();
  if (obj[key].is<const char *>()) return atoi(obj[key].as<const char *>());
  return 0;
}

inline uint64_t json_uint64_value(JsonObject obj, const char *key) {
  if (obj[key].is<const char *>()) return strtoull(obj[key].as<const char *>(), nullptr, 10);
  if (obj[key].is<unsigned long>()) return obj[key].as<unsigned long>();
  if (obj[key].is<long>()) {
    long value = obj[key].as<long>();
    return value > 0 ? (uint64_t) value : 0;
  }
  if (obj[key].is<int>()) {
    int value = obj[key].as<int>();
    return value > 0 ? (uint64_t) value : 0;
  }
  if (obj[key].is<double>()) {
    double value = obj[key].as<double>();
    return value > 0 ? (uint64_t) value : 0;
  }
  return 0;
}

inline std::string join_strings(const std::vector<std::string> &items, const char *sep = ", ") {
  std::string out;
  for (const auto &item : items) {
    if (item.empty()) continue;
    if (!out.empty()) out += sep;
    out += item;
  }
  return out;
}

inline std::string format_immich_datetime(const std::string &raw) {
  if (raw.size() < 10) return "";

  int year = 0, month = 0, day = 0, hour = -1, minute = -1;
  if (raw.size() >= 10 && raw[4] == '-' && raw[7] == '-') {
    year = atoi(raw.substr(0, 4).c_str());
    month = atoi(raw.substr(5, 2).c_str());
    day = atoi(raw.substr(8, 2).c_str());
  } else if (raw.size() >= 10 && raw[4] == ':' && raw[7] == ':') {
    year = atoi(raw.substr(0, 4).c_str());
    month = atoi(raw.substr(5, 2).c_str());
    day = atoi(raw.substr(8, 2).c_str());
  }

  size_t time_pos = raw.find('T');
  if (time_pos == std::string::npos) time_pos = raw.find(' ');
  if (time_pos != std::string::npos && raw.size() >= time_pos + 6) {
    hour = atoi(raw.substr(time_pos + 1, 2).c_str());
    minute = atoi(raw.substr(time_pos + 4, 2).c_str());
  }

  if (year <= 0 || month < 1 || month > 12 || day <= 0) return "";
  char buf[40];
  if (hour >= 0 && minute >= 0) {
    snprintf(buf, sizeof(buf), "%d %s %04d, %02d:%02d", day, MONTH_NAMES[month], year, hour, minute);
  } else {
    snprintf(buf, sizeof(buf), "%d %s %04d", day, MONTH_NAMES[month], year);
  }
  return std::string(buf);
}

inline std::string format_file_size(uint64_t bytes) {
  if (bytes == 0) return "";
  if (bytes >= 1024ULL * 1024ULL)
    return format_decimal_trimmed((double) bytes / (1024.0 * 1024.0), 1) + " MB";
  if (bytes >= 1024ULL)
    return format_decimal_trimmed((double) bytes / 1024.0, 1) + " KB";
  return std::to_string((unsigned long long) bytes) + " bytes";
}

inline std::string format_mime_type(const std::string &mime) {
  std::string lower = to_lower_ascii(trim_ascii_whitespace(mime));
  if (lower == "image/jpeg" || lower == "image/jpg") return "JPEG";
  if (lower == "image/png") return "PNG";
  if (lower == "image/webp") return "WebP";
  if (lower == "image/heic") return "HEIC";
  if (lower == "image/heif") return "HEIF";
  if (lower.rfind("image/", 0) == 0) return upper_ascii(lower.substr(6));
  return mime;
}

inline std::string format_image_details(const std::string &mime, int width, int height, uint64_t bytes) {
  std::vector<std::string> parts;
  std::string type = format_mime_type(mime);
  if (!type.empty()) parts.push_back(type);
  if (width > 0 && height > 0) {
    std::string dims = std::to_string(width) + " x " + std::to_string(height);
    double mp = ((double) width * (double) height) / 1000000.0;
    if (mp >= 0.1) dims += " (" + format_decimal_trimmed(mp, 1) + " MP)";
    parts.push_back(dims);
  }
  std::string size = format_file_size(bytes);
  if (!size.empty()) parts.push_back(size);
  return join_strings(parts, " | ");
}

inline std::string format_camera_name(const std::string &make, const std::string &model) {
  std::string clean_make = trim_ascii_whitespace(make);
  std::string clean_model = trim_ascii_whitespace(model);
  if (clean_make.empty()) return clean_model;
  if (clean_model.empty()) return clean_make;
  std::string lower_make = to_lower_ascii(clean_make);
  std::string lower_model = to_lower_ascii(clean_model);
  if (lower_model.rfind(lower_make, 0) == 0) return clean_model;
  return clean_make + " " + clean_model;
}

inline std::string format_aperture(std::string value) {
  value = trim_ascii_whitespace(value);
  if (value.empty()) return "";
  std::string lower = to_lower_ascii(value);
  if (lower.rfind("f/", 0) == 0) return value;
  return "f/" + value;
}

inline std::string format_exposure(std::string value) {
  value = trim_ascii_whitespace(value);
  if (value.empty()) return "";
  if (value.find('s') != std::string::npos || value.find('S') != std::string::npos) return value;
  return value + "s";
}

inline std::string format_focal_length(std::string value) {
  value = trim_ascii_whitespace(value);
  if (value.empty()) return "";
  std::string lower = to_lower_ascii(value);
  if (lower.find("mm") != std::string::npos) return value;
  return value + "mm";
}

inline std::string parse_immich_asset_object(JsonObject asset,
                                             const std::string &base_url,
                                             ImmichAssetMeta *out_meta) {
  if (out_meta == nullptr) return "";
  if (asset.isNull() || !asset["id"].is<const char *>())
    return "";

  std::string asset_id = asset["id"].as<std::string>();
  std::string photo_date, photo_location, photo_person;
  std::string photo_people, photo_date_taken, photo_image_format, photo_camera;
  std::string photo_camera_settings, photo_lens;
  int photo_year = 0, photo_month = 0;
  bool is_portrait = false;
  bool orientation_known = false;
  int display_w = 0, display_h = 0;
  uint64_t file_size = 0;

  std::string local_datetime;
  if (asset["localDateTime"].is<const char *>()) {
    std::string raw = asset["localDateTime"].as<std::string>();
    local_datetime = raw;
    photo_date_taken = format_immich_datetime(raw);
    if (raw.size() >= 10) {
      photo_year = atoi(raw.substr(0, 4).c_str());
      photo_month = atoi(raw.substr(5, 2).c_str());
      photo_date = format_photo_date(photo_year, photo_month);
    }
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
      if (photo_date_taken.empty()) photo_date_taken = format_immich_datetime(raw);
      if (raw.size() >= 10) {
        photo_year = atoi(raw.substr(0, 4).c_str());
        photo_month = atoi(raw.substr(5, 2).c_str());
        photo_date = format_photo_date(photo_year, photo_month);
      }
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
      display_w = exif_w;
      display_h = exif_h;
    }

    file_size = json_uint64_value(exif, "fileSizeInByte");
    photo_camera = format_camera_name(json_string_value(exif, "make"), json_string_value(exif, "model"));
    photo_lens = json_string_value(exif, "lensModel");

    std::vector<std::string> settings;
    std::string focal = format_focal_length(json_number_or_string(exif, "focalLength", 1));
    if (!focal.empty()) settings.push_back(focal);
    std::string aperture = format_aperture(json_number_or_string(exif, "fNumber", 1));
    if (!aperture.empty()) settings.push_back(aperture);
    std::string exposure = format_exposure(json_number_or_string(exif, "exposureTime", 4));
    if (!exposure.empty()) settings.push_back(exposure);
    int iso = json_int_value(exif, "iso");
    if (iso > 0) settings.push_back("ISO " + std::to_string(iso));
    photo_camera_settings = join_strings(settings, " | ");
  }

  if (asset["people"].is<JsonArray>()) {
    JsonArray people = asset["people"].as<JsonArray>();
    std::vector<std::string> names;
    if (people.size() > 0) {
      JsonObject person = people[0].as<JsonObject>();
      if (person["name"].is<const char *>())
        photo_person = person["name"].as<std::string>();
    }
    for (size_t i = 0; i < people.size(); i++) {
      JsonObject person = people[i].as<JsonObject>();
      if (!person.isNull() && person["name"].is<const char *>()) {
        std::string name = trim_ascii_whitespace(person["name"].as<std::string>());
        if (!name.empty()) names.push_back(name);
      }
    }
    photo_people = join_strings(names);
  }

  std::string mime = json_string_value(asset, "originalMimeType");
  photo_image_format = format_image_details(mime, display_w, display_h, file_size);

  std::string img_url = base_url + "/api/assets/" + asset_id + "/thumbnail?size=preview";
  out_meta->asset_id = asset_id;
  out_meta->image_url = img_url;
  out_meta->date = photo_date;
  out_meta->location = photo_location;
  out_meta->year = photo_year;
  out_meta->month = photo_month;
  out_meta->person = photo_person;
  out_meta->people = photo_people;
  out_meta->date_taken = photo_date_taken;
  out_meta->image_format = photo_image_format;
  out_meta->camera = photo_camera;
  out_meta->camera_settings = photo_camera_settings;
  out_meta->lens = photo_lens;
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

#endif  // USE_JSON
