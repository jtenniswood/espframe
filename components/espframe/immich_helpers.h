#pragma once
#include "date_utils.h"
#include "esp_random.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>
#include <vector>

static constexpr uint16_t ZOOM_IDENTITY = 256;
static constexpr uint16_t IMMICH_ALBUM_PAGE_SIZE = 16;
static constexpr uint16_t IMMICH_METADATA_PAGE_SIZE = 5;
static constexpr uint16_t IMMICH_RANDOM_POOL_SIZE = 6;
static constexpr uint16_t IMMICH_COMPANION_SEARCH_SIZE = 20;
// Sixteen midpoint probes reduce a 10,000-page upper bound below one page,
// covering very sparse date-filtered albums without a first-page bias.
static constexpr uint8_t MAX_EMPTY_METADATA_PAGE_PROBES = 16;

struct ImmichMetadataCountCacheEntry {
  std::string key;
  uint32_t total = 0;
  bool count_is_upper_bound = false;
};

// Owns the complete state of the Immich request pipeline. Keeping these values
// together makes reset, retry, and cooldown transitions atomic instead of
// relying on loosely related YAML globals being updated in the right order.
struct ImmichRequestState {
  int api_retries = 0;
  int last_http_status = 0;
  int consecutive_failures = 0;
  uint32_t failure_window_started_ms = 0;
  uint32_t retry_delay_ms = 2000;
  uint32_t retry_cooldown_until_ms = 0;

  bool memory_fallback = false;
  std::string memory_asset_id;
  int memory_window_offset = -2;
  int memory_image_count = 0;

  std::string metadata_album_id;
  std::string metadata_person_id;
  std::string metadata_tag_ids;
  int metadata_page = 1;
  int metadata_page_size = 1;
  uint32_t metadata_max_page = 1;
  uint8_t metadata_empty_page_probes = 0;
  bool metadata_page_bound_is_upper = false;
  bool metadata_page1_fallback_attempted = false;
  uint32_t metadata_bypass_until_ms = 0;
  int album_order_index = 0;
  std::string metadata_count_cache_key;
  bool metadata_count_cache_hit = false;
  std::vector<ImmichMetadataCountCacheEntry> metadata_count_cache;
  bool candidate_pool_hit = false;
  std::string candidate_pool_source_filter_id;

  void reset() { *this = ImmichRequestState{}; }

  // Kept for the photo-source flush path. Album pagination no longer has a
  // statistics fallback cache, but applying a source must still clear its
  // in-progress page-bound probes.
  void reset_album_metadata_fallbacks() {
    this->metadata_max_page = 1;
    this->metadata_empty_page_probes = 0;
    this->metadata_page_bound_is_upper = false;
    this->metadata_page1_fallback_attempted = false;
    this->metadata_count_cache.clear();
    this->metadata_count_cache_key.clear();
    this->metadata_count_cache_hit = false;
  }

  bool find_metadata_count(const std::string &key, uint32_t *total,
                           bool *count_is_upper_bound) const {
    if (total == nullptr || count_is_upper_bound == nullptr) return false;
    for (const auto &entry : this->metadata_count_cache) {
      if (entry.key != key) continue;
      *total = entry.total;
      *count_is_upper_bound = entry.count_is_upper_bound;
      return entry.total > 0;
    }
    return false;
  }

  void remember_metadata_count(const std::string &key, uint32_t total,
                               bool count_is_upper_bound) {
    if (key.empty() || total == 0) return;
    for (auto &entry : this->metadata_count_cache) {
      if (entry.key != key) continue;
      entry.total = total;
      entry.count_is_upper_bound = count_is_upper_bound;
      return;
    }
    // Keep this cache deliberately small and FIFO. A miss costs one count
    // request but never changes the selected photos.
    if (this->metadata_count_cache.size() >= 12) {
      this->metadata_count_cache.erase(this->metadata_count_cache.begin());
    }
    this->metadata_count_cache.push_back({key, total, count_is_upper_bound});
  }

  void begin_memory_search() {
    this->memory_fallback = false;
    this->memory_asset_id.clear();
    this->memory_window_offset = -2;
    this->memory_image_count = 0;
  }

  bool advance_memory_window() {
    this->memory_window_offset++;
    return this->memory_window_offset <= 2;
  }

  // Reservoir sampling (Algorithm R, k=1): keep one candidate and replace it with
  // probability 1/n as each asset is seen. Collecting every id first and indexing
  // into the collection afterwards needs memory proportional to the window size —
  // a five-day window can hold several hundred assets, which is a ~15kB string
  // grown by repeated reallocation, and each reallocation needs the old and new
  // buffers alive at the same time. That aborts on devices whose largest free
  // internal-DRAM block is smaller than the pair. This is O(1) and picks
  // uniformly, exactly as selecting a random index into the full list did.
  void add_memory_image(const std::string &asset_id) {
    if (asset_id.empty()) return;
    this->memory_image_count++;
    if (esp_random() % static_cast<uint32_t>(this->memory_image_count) == 0) {
      this->memory_asset_id = asset_id;
    }
  }

  bool cooldown_active(uint32_t now_ms) const {
    return this->retry_cooldown_until_ms != 0 && now_ms < this->retry_cooldown_until_ms;
  }

  void pause_for(uint32_t now_ms, uint32_t duration_ms) {
    this->retry_cooldown_until_ms = now_ms + duration_ms;
  }

  uint32_t register_fetch_failure(uint32_t now_ms) {
    this->api_retries = 0;
    this->record_failure_(now_ms);
    uint32_t cooldown_ms = 3000;
    if (this->consecutive_failures >= 3) cooldown_ms = 7000;
    if (this->consecutive_failures >= 6) cooldown_ms = 15000;
    this->pause_for(now_ms, cooldown_ms);
    return cooldown_ms;
  }

  uint32_t register_download_failure(uint32_t now_ms) {
    this->record_failure_(now_ms);
    uint32_t cooldown_ms = 5000;
    if (this->consecutive_failures >= 4) cooldown_ms = 10000;
    if (this->consecutive_failures >= 8) cooldown_ms = 20000;
    this->pause_for(now_ms, cooldown_ms);
    return cooldown_ms;
  }

  void register_success() {
    this->api_retries = 0;
    this->clear_failures();
    this->retry_delay_ms = 2000;
    this->retry_cooldown_until_ms = 0;
    this->metadata_max_page = 1;
    this->metadata_empty_page_probes = 0;
    this->metadata_page_bound_is_upper = false;
    this->metadata_page1_fallback_attempted = false;
  }

  int register_request_error() { return ++this->api_retries; }

  bool retry_available(int max_retries) const { return this->api_retries < max_retries; }

  void clear_http_status() { this->last_http_status = 0; }

  void clear_failures() {
    this->last_http_status = 0;
    this->consecutive_failures = 0;
    this->failure_window_started_ms = 0;
  }

  void note_http_failure(int status, uint32_t now_ms) {
    this->last_http_status = status;
    if (this->failure_window_started_ms == 0) this->failure_window_started_ms = now_ms;
  }

  void reset_retries_and_pause(uint32_t now_ms, uint32_t cooldown_ms = 30000) {
    this->api_retries = 0;
    this->pause_for(now_ms, cooldown_ms);
  }

  uint32_t prepare_retry_delay() {
    uint32_t delay_ms = 2000;
    if (this->api_retries >= 2) delay_ms = 5000;
    if (this->api_retries >= 3) delay_ms = 10000;
    if (this->consecutive_failures >= 5 && delay_ms < 10000) delay_ms = 10000;
    this->retry_delay_ms = delay_ms;
    return delay_ms;
  }

  void record_http_failure(int status, uint32_t now_ms, uint32_t cooldown_ms = 30000) {
    this->note_http_failure(status, now_ms);
    this->pause_for(now_ms, cooldown_ms);
  }

 private:
  void record_failure_(uint32_t now_ms) {
    this->consecutive_failures++;
    if (this->failure_window_started_ms == 0) this->failure_window_started_ms = now_ms;
  }
};

struct ImmichAssetMeta {
  // Normalized subset of the Immich asset response used by the slideshow UI.
  // Keeping a compact struct avoids spreading JSON field names through YAML
  // lambdas.
  std::string asset_id, image_url, date, location, person;
  std::string datetime;  // localDateTime from asset, for slot display
  std::string source_filter_id;  // exact album/person/tag selection for companion searches
  int year = 0, month = 0, day = 0;
  bool is_portrait = false;
  bool orientation_known = false;
  uint16_t zoom = ZOOM_IDENTITY;
};

// ============================================================================
// Immich search body builder
// ============================================================================
// Builds the JSON POST body for /api/search/random with optional filters
// for favorites, albums, people, and tags. The `extra` parameter allows injecting
// additional JSON fields (e.g. takenAfter/takenBefore for companion search).

struct ImmichDateRange {
  std::string from;
  std::string to;
  bool relative_skipped_for_invalid_time = false;
};

struct ImmichTimelineBucketChoice {
  std::string time_bucket;
  uint32_t count = 0;
  uint32_t page = 1;
};

struct ImmichTimelineBucketInfo {
  std::string time_bucket;
  uint32_t count = 0;
};

struct ImmichTimelineAssetCandidate {
  std::string asset_id;
  bool is_image = true;
  bool has_ratio = false;
  float ratio = 0.0f;
};

struct ImmichPortraitCompanionCandidate {
  std::string asset_id;
  std::string datetime;
  bool is_portrait = false;
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
  char buf[40];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
  return std::string(buf);
}

inline std::string immich_format_iso_date_offset(int year, int month, int day, int offset_days) {
  int shifted_year = 0;
  int shifted_month = 0;
  int shifted_day = 0;
  civil_from_days(days_from_civil(year, month, day) + offset_days,
                  shifted_year, shifted_month, shifted_day);
  return immich_format_iso_date(shifted_year, shifted_month, shifted_day);
}

inline int portrait_pairing_range_days(const std::string &option) {
  if (option.find('2') != std::string::npos) return 2;
  if (option.find('1') != std::string::npos) return 1;
  return 0;
}

inline void append_csv_value(std::string &csv, const std::string &value) {
  if (value.empty()) return;
  if (!csv.empty()) csv += ",";
  csv += value;
}

inline std::string csv_value_at(const std::string &csv, int index) {
  if (index < 0) return "";
  size_t start = 0;
  for (int i = 0; i < index; i++) {
    start = csv.find(',', start);
    if (start == std::string::npos) return "";
    start++;
  }
  size_t end = csv.find(',', start);
  if (end == std::string::npos) end = csv.size();
  return csv.substr(start, end - start);
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
                                                           const ImmichDateRange &range,
                                                           int range_days = 0) {
  if (range_days < 0) range_days = 0;
  int year = day.size() >= 10 ? atoi(day.substr(0, 4).c_str()) : 0;
  int month = day.size() >= 10 ? atoi(day.substr(5, 2).c_str()) : 0;
  int day_of_month = day.size() >= 10 ? atoi(day.substr(8, 2).c_str()) : 0;
  std::string from_day = day;
  std::string to_day = day;
  if (is_valid_date_parts(year, month, day_of_month) && range_days > 0) {
    from_day = immich_format_iso_date_offset(year, month, day_of_month, -range_days);
    to_day = immich_format_iso_date_offset(year, month, day_of_month, range_days);
  }
  if (!range.from.empty() && range.from > from_day) from_day = range.from;
  if (!range.to.empty() && range.to < to_day) to_day = range.to;
  std::string after = from_day + "T00:00:00.000Z";
  std::string before = to_day + "T23:59:59.999Z";
  return "\"takenAfter\":\"" + after + "\",\"takenBefore\":\"" + before + "\"";
}

inline bool immich_datetime_sort_value(const std::string &raw, int64_t &value) {
  if (raw.size() < 10) return false;
  int year = atoi(raw.substr(0, 4).c_str());
  int month = atoi(raw.substr(5, 2).c_str());
  int day = atoi(raw.substr(8, 2).c_str());
  if (!is_valid_date_parts(year, month, day)) return false;
  int hour = raw.size() >= 13 ? atoi(raw.substr(11, 2).c_str()) : 0;
  int minute = raw.size() >= 16 ? atoi(raw.substr(14, 2).c_str()) : 0;
  int second = raw.size() >= 19 ? atoi(raw.substr(17, 2).c_str()) : 0;
  value = static_cast<int64_t>(days_from_civil(year, month, day)) * 86400 +
          hour * 3600 + minute * 60 + second;
  return true;
}

inline std::string pick_closest_immich_portrait_companion_asset_id(
    const std::vector<ImmichPortraitCompanionCandidate> &candidates,
    const std::string &primary_asset_id,
    const std::string &primary_datetime) {
  int64_t primary_sort_value = 0;
  bool primary_has_sort_value = immich_datetime_sort_value(primary_datetime, primary_sort_value);
  bool found = false;
  bool best_has_distance = false;
  int64_t best_distance = std::numeric_limits<int64_t>::max();
  std::string best_asset_id;
  for (const auto &candidate : candidates) {
    if (candidate.asset_id.empty() || candidate.asset_id == primary_asset_id ||
        !candidate.is_portrait) {
      continue;
    }
    int64_t candidate_sort_value = 0;
    bool candidate_has_sort_value = primary_has_sort_value &&
        immich_datetime_sort_value(candidate.datetime, candidate_sort_value);
    int64_t distance = candidate_has_sort_value
                           ? (candidate_sort_value >= primary_sort_value
                                  ? candidate_sort_value - primary_sort_value
                                  : primary_sort_value - candidate_sort_value)
                           : std::numeric_limits<int64_t>::max();
    if (!found || (candidate_has_sort_value &&
                   (!best_has_distance || distance < best_distance))) {
      found = true;
      best_has_distance = candidate_has_sort_value;
      best_distance = distance;
      best_asset_id = candidate.asset_id;
    }
  }
  return best_asset_id;
}

inline std::vector<std::string> split_uuid_csv(const std::string &csv) {
  // Home Assistant text fields store source IDs as comma-separated text;
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

inline std::string select_immich_tag_ids(const std::string &csv,
                                         const std::string &matching_mode) {
  if (matching_mode == "All selected tags") return csv;
  return pick_one_uuid_from_csv(csv);
}

inline std::string build_immich_metadata_count_cache_key(
    const std::string &photo_source, const std::string &album_id,
    const std::string &person_id, const std::string &tag_ids,
    const std::string &filter_extra) {
  return photo_source + "|" + album_id + "|" + person_id + "|" +
         tag_ids + "|" + filter_extra;
}

inline std::string pick_album_id_for_metadata_search(const std::string &csv,
                                                     const std::string &album_order,
                                                     int &next_index) {
  std::vector<std::string> ids = split_uuid_csv(csv);
  if (ids.empty()) {
    next_index = 0;
    return "";
  }

  if (album_order != "Album list order") {
    return pick_one_uuid_from_csv(csv);
  }

  if (next_index < 0 || next_index >= static_cast<int>(ids.size())) {
    next_index = 0;
  }
  std::string selected = ids[next_index];
  next_index = (next_index + 1) % static_cast<int>(ids.size());
  return selected;
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

inline bool immich_source_has_required_ids(const std::string &photo_source,
                                           const std::string &album_ids,
                                           const std::string &person_ids,
                                           const std::string &tag_ids) {
  if (photo_source == "Album")
    return !split_uuid_csv(album_ids).empty();
  if (photo_source == "Person")
    return !split_uuid_csv(person_ids).empty();
  if (photo_source == "Tag")
    return !split_uuid_csv(tag_ids).empty();
  return true;
}

inline std::string immich_source_setup_title(const std::string &photo_source) {
  if (photo_source == "Album") return "Album source needs setup";
  if (photo_source == "Person") return "Person source needs setup";
  if (photo_source == "Tag") return "Tag source needs setup";
  return "Photo source needs setup";
}

inline std::string immich_source_setup_message(const std::string &photo_source) {
  std::string item = "photo source";
  if (photo_source == "Album") item = "album";
  else if (photo_source == "Person") item = "person";
  else if (photo_source == "Tag") item = "tag";
  return "Open ESPFrame settings and add at least one " + item +
         ", or choose All Photos.";
}

inline bool immich_dimensions_are_portrait(int width, int height,
                                           const std::string &orientation,
                                           bool dimensions_are_raw_exif) {
  if (width <= 0 || height <= 0) return false;
  // Immich's top-level width/height are already normalized for display. Only
  // raw EXIF dimensions still need the orientation transform.
  if (dimensions_are_raw_exif &&
      (orientation == "5" || orientation == "6" ||
       orientation == "7" || orientation == "8")) {
    std::swap(width, height);
  }
  return height > width;
}

inline std::string build_immich_search_body(int size, bool with_people,
                                             const std::string &photo_source,
                                             const std::string &album_ids,
                                             const std::string &person_ids,
                                             const std::string &tag_ids,
                                             const std::string &extra = "") {
  // Construct the small JSON request body by hand to keep this header usable
  // from ESPHome lambdas without bringing in another JSON writer.
  std::string body = "{\"size\":" + std::to_string(size) +
                      ",\"type\":\"IMAGE\",\"visibility\":\"timeline\",\"withExif\":true";
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
  } else if (photo_source == "Tag" && !tag_ids.empty()) {
    body += ",\"tagIds\":" + build_uuid_json_array(tag_ids);
  }
  body += "}";
  return body;
}

inline uint32_t immich_metadata_page_for_total(uint32_t total,
                                               uint16_t page_size = IMMICH_METADATA_PAGE_SIZE) {
  if (page_size == 0) page_size = IMMICH_METADATA_PAGE_SIZE;
  if (total == 0) return 1;
  uint32_t pages = (total + page_size - 1) / page_size;
  if (pages == 0) pages = 1;
  return (esp_random() % pages) + 1;
}

inline uint32_t immich_metadata_page_count_for_total(uint32_t total,
                                                      uint16_t page_size = IMMICH_METADATA_PAGE_SIZE) {
  if (page_size == 0) page_size = IMMICH_METADATA_PAGE_SIZE;
  if (total == 0) return 1;
  uint32_t pages = (total + page_size - 1) / page_size;
  if (pages == 0) pages = 1;
  return pages;
}

inline void initialize_immich_metadata_page_range(ImmichRequestState &state,
                                                   uint32_t total,
                                                   uint16_t page_size,
                                                   bool count_is_upper_bound) {
  if (page_size == 0) page_size = IMMICH_METADATA_PAGE_SIZE;
  state.metadata_page_size = page_size;
  state.metadata_max_page = immich_metadata_page_count_for_total(total, page_size);
  state.metadata_page = (esp_random() % state.metadata_max_page) + 1;
  state.metadata_empty_page_probes = 0;
  state.metadata_page_bound_is_upper = count_is_upper_bound;
  state.metadata_page1_fallback_attempted = false;
}

// Album assetCount includes videos plus assets hidden by Immich or excluded by
// Espframe's date filter. It is therefore an upper bound rather than an exact
// metadata-search total. An empty metadata page proves every later page is
// empty too, so reduce the range and probe its midpoint without changing
// albums. Midpoint probes converge on a selective filter's actual last page,
// whereas repeated random probes can keep landing above that boundary.
inline bool retry_empty_immich_metadata_page(ImmichRequestState &state) {
  if (!state.metadata_page_bound_is_upper) return false;

  if (state.metadata_page > 1 && state.metadata_empty_page_probes < MAX_EMPTY_METADATA_PAGE_PROBES) {
    uint32_t previous_page = static_cast<uint32_t>(state.metadata_page);
    state.metadata_max_page = std::min(state.metadata_max_page, previous_page - 1);
    if (state.metadata_max_page == 0) return false;
    state.metadata_page = static_cast<int>((state.metadata_max_page + 1) / 2);
    state.metadata_empty_page_probes++;
    return true;
  }

  if (!state.metadata_page1_fallback_attempted && state.metadata_page != 1) {
    state.metadata_page = 1;
    state.metadata_page1_fallback_attempted = true;
    return true;
  }

  return false;
}

inline bool immich_source_uses_metadata_search(const std::string &photo_source) {
  // Album metadata search is retained because, unlike random search, Immich
  // authorizes the album first and can include assets contributed by others.
  return photo_source == "Album";
}

inline std::string build_immich_metadata_search_body(uint32_t page,
                                                     uint16_t size,
                                                     bool with_people,
                                                     const std::string &photo_source,
                                                     const std::string &album_id,
                                                     const std::string &person_id,
                                                     const std::string &tag_ids,
                                                     const std::string &extra = "") {
  if (page == 0) page = 1;
  if (size == 0) size = 1;
  std::string body = "{\"page\":" + std::to_string(page) +
                     ",\"size\":" + std::to_string(size) +
                     ",\"type\":\"IMAGE\",\"visibility\":\"timeline\",\"withExif\":true";
  if (with_people) body += ",\"withPeople\":true";
  if (!extra.empty()) body += "," + extra;
  if (photo_source == "Favorites") {
    body += ",\"isFavorite\":true";
  } else if (photo_source == "Album" && !album_id.empty()) {
    body += ",\"albumIds\":[\"" + album_id + "\"]";
  } else if (photo_source == "Person" && !person_id.empty()) {
    body += ",\"personIds\":[\"" + person_id + "\"]";
  } else if (photo_source == "Tag" && !tag_ids.empty()) {
    body += ",\"tagIds\":" + build_uuid_json_array(tag_ids);
  }
  body += "}";
  return body;
}

inline std::string build_immich_companion_search_body(
    uint16_t size, const std::string &photo_source,
    const std::string &source_filter_id, const std::string &extra = "") {
  std::string album_id = photo_source == "Album" ? source_filter_id : "";
  std::string person_id = photo_source == "Person" ? source_filter_id : "";
  std::string tag_ids = photo_source == "Tag" ? source_filter_id : "";
  // Random sampling covers the full date window without capturing a large,
  // chronologically ordered metadata page in the device's internal RAM. The
  // client evaluates every returned portrait by capture-time distance.
  return build_immich_search_body(
      size, false, photo_source, album_id, person_id, tag_ids, extra);
}

inline std::string build_immich_statistics_search_body(const std::string &photo_source,
                                                       const std::string &album_id,
                                                       const std::string &person_id,
                                                       const std::string &tag_ids,
                                                       const std::string &extra = "") {
  std::string body = "{\"type\":\"IMAGE\",\"visibility\":\"timeline\"";
  if (!extra.empty()) body += "," + extra;
  if (photo_source == "Favorites") {
    body += ",\"isFavorite\":true";
  } else if (photo_source == "Album" && !album_id.empty()) {
    body += ",\"albumIds\":[\"" + album_id + "\"]";
  } else if (photo_source == "Person" && !person_id.empty()) {
    body += ",\"personIds\":[\"" + person_id + "\"]";
  } else if (photo_source == "Tag" && !tag_ids.empty()) {
    body += ",\"tagIds\":" + build_uuid_json_array(tag_ids);
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

inline uint32_t immich_album_page_for_count(uint32_t count,
                                            uint16_t page_size = IMMICH_ALBUM_PAGE_SIZE) {
  if (page_size == 0) page_size = IMMICH_ALBUM_PAGE_SIZE;
  if (count == 0) count = 1;
  uint32_t pages = (count + page_size - 1) / page_size;
  if (pages == 0) pages = 1;
  return (esp_random() % pages) + 1;
}

inline ImmichTimelineBucketChoice pick_immich_timeline_bucket_from_choices(
    const std::vector<ImmichTimelineBucketInfo> &buckets,
    uint16_t page_size = IMMICH_ALBUM_PAGE_SIZE) {
  std::vector<ImmichTimelineBucketInfo> choices;
  uint32_t total = 0;

  for (const auto &bucket : buckets) {
    if (bucket.time_bucket.empty()) continue;
    uint32_t count = bucket.count == 0 ? 1 : bucket.count;
    choices.push_back({bucket.time_bucket, count});
    total += count;
  }

  if (choices.empty() || total == 0) return {};

  uint32_t pick = esp_random() % total;
  uint32_t seen = 0;
  for (const auto &choice : choices) {
    seen += choice.count;
    if (pick < seen) {
      return {choice.time_bucket, choice.count,
              immich_album_page_for_count(choice.count, page_size)};
    }
  }

  const auto &choice = choices.back();
  return {choice.time_bucket, choice.count,
          immich_album_page_for_count(choice.count, page_size)};
}

inline std::string pick_immich_timeline_asset_id_from_candidates(
    const std::vector<ImmichTimelineAssetCandidate> &assets,
    const std::string &orientation_filter = "Any") {
  std::vector<std::string> candidates;

  for (const auto &asset : assets) {
    if (asset.asset_id.empty() || !asset.is_image) continue;

    if (orientation_filter == "Portrait Only" || orientation_filter == "Landscape Only") {
      if (!asset.has_ratio || asset.ratio <= 0.0f) continue;
      bool portrait = asset.ratio < 1.0f;
      if (orientation_filter == "Portrait Only" && !portrait) continue;
      if (orientation_filter == "Landscape Only" && portrait) continue;
    }

    candidates.push_back(asset.asset_id);
  }

  if (candidates.empty()) return "";
  return candidates[esp_random() % candidates.size()];
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
    if (exif_w > 0 && exif_h > 0) {
      is_portrait = immich_dimensions_are_portrait(
        exif_w, exif_h, orientation, true);
      orientation_known = true;
    }
  }

  // Immich 3 exposes normalized display dimensions on the asset itself. They
  // remain available when EXIF extraction did not produce dimensions.
  if (!orientation_known) {
    int width = asset["width"].is<int>() ? asset["width"].as<int>() : 0;
    int height = asset["height"].is<int>() ? asset["height"].as<int>() : 0;
    if (width > 0 && height > 0) {
      is_portrait = height > width;
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

inline JsonArray immich_asset_array_from_document(JsonDocument &doc) {
  if (doc.is<JsonArray>()) return doc.as<JsonArray>();
  if (!doc.is<JsonObject>()) return JsonArray();
  JsonObject assets = doc.as<JsonObject>()["assets"].as<JsonObject>();
  if (assets.isNull()) return JsonArray();
  JsonArray items = assets["items"].as<JsonArray>();
  if (items.isNull()) items = assets["assets"].as<JsonArray>();
  return items;
}

inline size_t append_immich_asset_candidates(
    const std::string &body, const std::string &base_url,
    std::vector<ImmichAssetMeta> &pool,
    const std::string &orientation_filter = "Any",
    size_t max_pool_size = IMMICH_RANDOM_POOL_SIZE) {
  auto doc = esphome::json::parse_json(body);
  if (doc.isNull()) return 0;
  JsonArray assets = immich_asset_array_from_document(doc);
  if (assets.isNull()) return 0;

  size_t before = pool.size();
  if (pool.capacity() < max_pool_size) pool.reserve(max_pool_size);
  for (size_t i = 0; i < assets.size() && pool.size() < max_pool_size; i++) {
    ImmichAssetMeta candidate;
    if (parse_immich_asset_object(assets[i].as<JsonObject>(), base_url, &candidate).empty()) continue;
    if (!photo_orientation_matches(candidate, orientation_filter)) continue;
    bool duplicate = false;
    for (const auto &existing : pool) {
      if (existing.asset_id == candidate.asset_id) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) pool.push_back(std::move(candidate));
  }
  return pool.size() - before;
}

inline uint32_t parse_immich_metadata_total(const std::string &body) {
  auto doc = esphome::json::parse_json(body);
  if (doc.isNull() || !doc.is<JsonObject>()) return 0;

  JsonObject root = doc.as<JsonObject>();
  JsonObject assets = root["assets"].as<JsonObject>();
  if (assets.isNull()) return 0;

  int total = 0;
  if (assets["total"].is<int>()) total = assets["total"].as<int>();
  if (total <= 0 && assets["count"].is<int>()) total = assets["count"].as<int>();
  if (total <= 0 && assets["items"].is<JsonArray>()) {
    total = assets["items"].as<JsonArray>().size();
  }
  return total > 0 ? static_cast<uint32_t>(total) : 0;
}

inline uint32_t parse_immich_statistics_total(const std::string &body) {
  auto doc = esphome::json::parse_json(body);
  if (doc.isNull() || !doc.is<JsonObject>()) return 0;

  JsonObject root = doc.as<JsonObject>();
  int total = 0;
  if (root["total"].is<int>()) total = root["total"].as<int>();
  if (total <= 0 && root["images"].is<int>()) total = root["images"].as<int>();
  return total > 0 ? static_cast<uint32_t>(total) : 0;
}

inline bool parse_immich_album_asset_count(const std::string &body, uint32_t *out_count) {
  if (out_count == nullptr) return false;
  auto doc = esphome::json::parse_json(body);
  if (doc.isNull() || !doc.is<JsonObject>()) return false;

  JsonObject root = doc.as<JsonObject>();
  if (!root["assetCount"].is<int>()) return false;
  int count = root["assetCount"].as<int>();
  if (count < 0) return false;
  *out_count = static_cast<uint32_t>(count);
  return true;
}

// Return true only when the response has a recognised metadata result shape.
// This lets the caller distinguish an empty result page from malformed JSON or
// a page whose images simply do not match the requested orientation.
inline bool parse_immich_metadata_item_count(const std::string &body, size_t *out_count) {
  if (out_count == nullptr) return false;
  auto doc = esphome::json::parse_json(body);
  if (doc.isNull()) return false;

  if (doc.is<JsonArray>()) {
    *out_count = doc.as<JsonArray>().size();
    return true;
  }
  if (!doc.is<JsonObject>()) return false;

  JsonObject root = doc.as<JsonObject>();
  JsonObject assets = root["assets"].as<JsonObject>();
  if (assets.isNull()) return false;

  JsonArray items = assets["items"].as<JsonArray>();
  if (items.isNull() && assets["assets"].is<JsonArray>()) {
    items = assets["assets"].as<JsonArray>();
  }
  if (items.isNull()) return false;
  *out_count = items.size();
  return true;
}

inline std::string parse_immich_metadata_asset(const std::string &body,
                                               const std::string &base_url,
                                               ImmichAssetMeta *out_meta,
                                               const std::string &orientation_filter = "Any") {
  if (out_meta == nullptr) return "";
  auto doc = esphome::json::parse_json(body);
  if (doc.isNull()) return "";

  if (doc.is<JsonArray>()) {
    return parse_immich_asset(body, base_url, out_meta, orientation_filter);
  }

  if (!doc.is<JsonObject>()) return "";

  JsonObject root = doc.as<JsonObject>();
  JsonObject assets = root["assets"].as<JsonObject>();
  if (assets.isNull()) return "";

  JsonArray items = assets["items"].as<JsonArray>();
  if (items.isNull() && assets["assets"].is<JsonArray>()) {
    items = assets["assets"].as<JsonArray>();
  }
  if (items.isNull()) return "";

  size_t matching = 0;
  for (size_t i = 0; i < items.size(); i++) {
    ImmichAssetMeta candidate;
    std::string img_url = parse_immich_asset_object(items[i].as<JsonObject>(), base_url, &candidate);
    if (img_url.empty()) continue;
    if (!photo_orientation_matches(candidate, orientation_filter)) continue;
    matching++;
  }
  if (matching == 0) return "";

  size_t pick = esp_random() % matching;
  size_t seen = 0;
  for (size_t i = 0; i < items.size(); i++) {
    ImmichAssetMeta candidate;
    std::string img_url = parse_immich_asset_object(items[i].as<JsonObject>(), base_url, &candidate);
    if (img_url.empty()) continue;
    if (!photo_orientation_matches(candidate, orientation_filter)) continue;
    if (seen++ != pick) continue;
    *out_meta = candidate;
    return img_url;
  }

  return "";
}

inline ImmichTimelineBucketChoice pick_immich_timeline_bucket(
    const std::string &body,
    uint16_t page_size = IMMICH_ALBUM_PAGE_SIZE) {
  auto doc = esphome::json::parse_json(body);
  if (doc.isNull() || !doc.is<JsonArray>()) return {};

  JsonArray buckets = doc.as<JsonArray>();
  std::vector<ImmichTimelineBucketInfo> choices;

  for (size_t i = 0; i < buckets.size(); i++) {
    JsonObject bucket = buckets[i].as<JsonObject>();
    if (bucket.isNull() || !bucket["timeBucket"].is<const char *>()) continue;
    int count = bucket["count"].is<int>() ? bucket["count"].as<int>() : 1;
    if (count <= 0) count = 1;
    choices.push_back({bucket["timeBucket"].as<std::string>(),
                       static_cast<uint32_t>(count)});
  }

  return pick_immich_timeline_bucket_from_choices(choices, page_size);
}

inline std::string pick_immich_time_bucket(const std::string &body) {
  return pick_immich_timeline_bucket(body).time_bucket;
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
  std::vector<ImmichTimelineAssetCandidate> candidates;

  for (size_t i = 0; i < ids.size(); i++) {
    if (!ids[i].is<const char *>()) continue;

    ImmichTimelineAssetCandidate candidate;
    candidate.asset_id = ids[i].as<std::string>();
    if (!is_images.isNull() && is_images[i].is<bool>()) {
      candidate.is_image = is_images[i].as<bool>();
    }
    if (!ratios.isNull() &&
        (ratios[i].is<float>() || ratios[i].is<double>() || ratios[i].is<int>())) {
      candidate.has_ratio = true;
      candidate.ratio = ratios[i].as<float>();
    }
    candidates.push_back(candidate);
  }

  return pick_immich_timeline_asset_id_from_candidates(candidates, orientation_filter);
}

inline std::string find_immich_portrait_companion_url(const std::string &body,
                                                      const std::string &base_url,
                                                      const std::string &primary_asset_id,
                                                      const std::string &primary_datetime = "") {
  auto doc = esphome::json::parse_json(body);
  if (doc.isNull()) return "";

  std::vector<ImmichPortraitCompanionCandidate> candidates;
  JsonArray arr = immich_asset_array_from_document(doc);
  if (arr.isNull()) return "";
  for (size_t i = 0; i < arr.size(); i++) {
    JsonObject asset = arr[i].as<JsonObject>();
    if (asset.isNull() || !asset["id"].is<const char *>()) continue;

    std::string asset_id = asset["id"].as<std::string>();
    JsonObject exif = asset["exifInfo"].as<JsonObject>();
    if (exif.isNull()) continue;

    int width = asset["width"].is<int>() ? asset["width"].as<int>() : 0;
    int height = asset["height"].is<int>() ? asset["height"].as<int>() : 0;
    int exif_width = exif["exifImageWidth"].is<int>()
      ? exif["exifImageWidth"].as<int>() : 0;
    int exif_height = exif["exifImageHeight"].is<int>()
      ? exif["exifImageHeight"].as<int>() : 0;
    const bool dimensions_are_raw_exif = exif_width > 0 && exif_height > 0;
    if (dimensions_are_raw_exif) {
      width = exif_width;
      height = exif_height;
    }

    std::string orientation;
    if (exif["orientation"].is<const char *>()) orientation = exif["orientation"].as<std::string>();

    std::string candidate_datetime;
    if (asset["localDateTime"].is<const char *>()) {
      candidate_datetime = asset["localDateTime"].as<std::string>();
    } else if (exif["dateTimeOriginal"].is<const char *>()) {
      candidate_datetime = exif["dateTimeOriginal"].as<std::string>();
    }
    candidates.push_back({
      asset_id, candidate_datetime,
      immich_dimensions_are_portrait(
        width, height, orientation, dimensions_are_raw_exif)});
  }

  std::string asset_id = pick_closest_immich_portrait_companion_asset_id(
      candidates, primary_asset_id, primary_datetime);
  if (asset_id.empty()) return "";
  return base_url + "/api/assets/" + asset_id + "/thumbnail?size=preview";
}

#endif  // USE_JSON
