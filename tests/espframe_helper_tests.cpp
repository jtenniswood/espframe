#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "components/espframe/date_utils.h"
#include "components/espframe/configuration_contract_generated.h"
#include "components/espframe/duration_helpers.h"
#include "components/espframe/immich_helpers.h"
#include "components/remote_image/jpeg_accelerator_helpers.h"

struct PhotoMeta {
  std::string asset_id, image_url, date, location, person;
  int year = 0, month = 0, day = 0;
  uint16_t zoom = ZOOM_IDENTITY;
};

struct SlotMeta : PhotoMeta {
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
  bool fetch_in_flight[3] = {false, false, false};
  uint32_t fetch_started_ms[3] = {0, 0, 0};
  bool noncritical_update[3] = {false, false, false};
};

struct PortraitState {
  bool left_ready = false, right_ready = false;
  bool no_companion_active = false, left_requested = false, right_requested = false;
  bool companion_found = false, is_pair = false;
  bool using_preload = false, workflow_busy = false;
};

inline void clear_noncritical(int s, SlotFlags &f, int &nc_count) {
  if (f.noncritical_update[s]) {
    f.noncritical_update[s] = false;
    if (nc_count > 0) nc_count--;
  }
}

inline void clear_slot_fetch_in_flight(int s, SlotFlags &f) {
  f.fetch_in_flight[s] = false;
  f.fetch_started_ms[s] = 0;
}

inline bool handle_slot_download_complete(int slot, SlotMeta &meta,
                                          SlotFlags &flags, int &nc_count,
                                          int &retries) {
  if (meta.asset_id != meta.pending_asset_id) {
    clear_slot_fetch_in_flight(slot, flags);
    clear_noncritical(slot, flags, nc_count);
    return false;
  }
  meta.ready = true;
  clear_slot_fetch_in_flight(slot, flags);
  clear_noncritical(slot, flags, nc_count);
  retries = 0;
  return true;
}

inline void mark_slot_fetch_in_flight(int s, SlotFlags &f, uint32_t now_ms) {
  f.fetch_in_flight[s] = true;
  f.fetch_started_ms[s] = now_ms;
}

inline uint32_t slot_fetch_age_ms(int s, const SlotFlags &f, uint32_t now_ms) {
  if (!f.fetch_in_flight[s] || f.fetch_started_ms[s] == 0) return 0;
  return now_ms - f.fetch_started_ms[s];
}

inline bool any_slot_fetch_in_flight(const SlotFlags &f) {
  return f.fetch_in_flight[0] || f.fetch_in_flight[1] || f.fetch_in_flight[2];
}

inline bool prepare_deferred_slot_update(int slot, int active_slot, SlotFlags &flags,
                                         bool workflow_busy, int &nc_count) {
  bool noncritical = slot != active_slot;
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
  mark_slot_fetch_in_flight(slot, flags, 1000);
  return true;
}

inline void copy_slot_to_display(const SlotMeta &slot, DisplayMeta &disp) {
  static_cast<PhotoMeta &>(disp) = static_cast<const PhotoMeta &>(slot);
  disp.datetime = slot.datetime;
  disp.companion_url = slot.companion_url;
  disp.filter_album_ids = slot.filter_album_ids;
  disp.filter_person_ids = slot.filter_person_ids;
  disp.filter_tag_ids = slot.filter_tag_ids;
  disp.is_portrait = slot.is_portrait;
}

inline void copy_display_to_slot(const DisplayMeta &disp, SlotMeta &slot) {
  static_cast<PhotoMeta &>(slot) = static_cast<const PhotoMeta &>(disp);
  slot.datetime = disp.datetime;
  slot.companion_url = disp.companion_url;
  slot.filter_album_ids = disp.filter_album_ids;
  slot.filter_person_ids = disp.filter_person_ids;
  slot.filter_tag_ids = disp.filter_tag_ids;
  slot.is_portrait = disp.is_portrait;
}

#include "components/espframe/slideshow_controller.h"
#include "components/espframe/slideshow_component.h"

static void test_date_and_url_helpers() {
  assert(normalize_immich_base_url(" immich.local:2283/") == "http://immich.local:2283");
  assert(normalize_immich_base_url("photos.example.com") == "https://photos.example.com");
  assert(normalize_immich_base_url("photos.example.com:443/") == "https://photos.example.com:443");
  assert(normalize_immich_base_url("//photos.example.com/") == "https://photos.example.com");
  assert(normalize_immich_base_url("HTTPS://photos.example.com///") == "https://photos.example.com");
  assert(is_valid_http_url("https://photos.example.com"));
  assert(is_valid_http_url("http://immich.local:2283"));
  assert(!is_valid_http_url("ftp://photos.example.com"));
  assert(!is_valid_http_url("https://photos.example.com:abc"));
  assert(!is_valid_http_url("https://photos.example.com:0"));
  assert(!is_valid_http_url("https://photos.example.com:65536"));
  assert(!is_valid_http_url("https://"));
  assert(format_photo_age(2026, 4, 21, 2026, 4, 21) == "today");
  assert(format_photo_age(2026, 4, 1, 2026, 4, 21) == "20 days ago");
  assert(format_photo_date_full(2026, 4, 21) == "21 April, 2026");
  assert(format_photo_date_full(2026, 1, 1) == "1 January, 2026");
  assert(format_photo_date_month_day_year(2026, 1, 1) == "January 1, 2026");
  int shifted_year = 0;
  int shifted_month = 0;
  int shifted_day = 0;
  civil_from_days(days_from_civil(2026, 3, 1) - 2, shifted_year, shifted_month, shifted_day);
  assert(shifted_year == 2026);
  assert(shifted_month == 2);
  assert(shifted_day == 27);
}

static void test_duration_helpers() {
  assert(parse_duration_option_seconds("10 seconds", 15, 10, 86400) == 10);
  assert(parse_duration_option_seconds("15 seconds", 15, 10, 86400) == 15);
  assert(parse_duration_option_seconds("1 minute", 15, 10, 86400) == 60);
  assert(parse_duration_option_seconds("2 minutes", 15, 10, 86400) == 120);
  assert(parse_duration_option_seconds("10 minutes", 15, 10, 86400) == 600);
  assert(parse_duration_option_seconds("20 minutes", 15, 10, 86400) == 1200);
  assert(parse_duration_option_seconds("1 hour", 15, 10, 86400) == 3600);
  assert(parse_duration_option_seconds("24 hours", 15, 10, 86400) == 86400);
  assert(parse_duration_option_seconds("5 seconds", 15, 10, 86400) == 10);
  assert(parse_duration_option_seconds("48 hours", 15, 10, 86400) == 86400);
  assert(parse_duration_option_seconds("", 15, 10, 86400) == 15);
}

static void test_p4_jpeg_accelerator_helpers() {
  const std::vector<uint8_t> baseline{
      0xFF, 0xD8,
      0xFF, 0xE0, 0x00, 0x04, 0x00, 0x00,
      0xFF, 0xC0, 0x00, 0x11, 0x08, 0x05, 0xA0, 0x07, 0x7E, 0x03,
      0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01,
      0xFF, 0xDA,
  };
  esphome::remote_image::P4JpegInfo info;
  assert(esphome::remote_image::parse_p4_baseline_jpeg(baseline.data(), baseline.size(), &info));
  assert(info.width == 1918);
  assert(info.height == 1440);
  assert(info.padded_width == 1920);
  assert(info.padded_height == 1440);
  assert(info.decoded_bytes == 1920u * 1440u * 2u);
  assert(esphome::remote_image::p4_jpeg_fits_workspace(info, baseline.size()));
  const auto baseline_info = info;

  esphome::remote_image::P4JpegInfo largest_live_preview{
      2160, 1440, 2160, 1440, 2160u * 1440u * 2u};
  assert(esphome::remote_image::p4_jpeg_fits_workspace(largest_live_preview, baseline.size()));

  auto progressive = baseline;
  progressive[9] = 0xC2;
  assert(!esphome::remote_image::parse_p4_baseline_jpeg(
      progressive.data(), progressive.size(), &info));

  auto grayscale = baseline;
  grayscale[17] = 0x01;
  assert(!esphome::remote_image::parse_p4_baseline_jpeg(grayscale.data(), grayscale.size(), &info));

  auto invalid_segment = baseline;
  invalid_segment[10] = 0x00;
  invalid_segment[11] = 0x01;
  assert(!esphome::remote_image::parse_p4_baseline_jpeg(
      invalid_segment.data(), invalid_segment.size(), &info));

  esphome::remote_image::P4JpegInfo oversized{4096, 4096, 4096, 4096, 4096u * 4096u * 2u};
  assert(!esphome::remote_image::p4_jpeg_fits_workspace(oversized, baseline.size()));
  assert(!esphome::remote_image::p4_jpeg_fits_workspace(
      baseline_info, esphome::remote_image::P4_JPEG_MAX_INPUT_BYTES + 1));
}

static void test_immich_body_helpers() {
  ImmichDateRange range = resolve_immich_date_filter(
      true, "Relative Range", 1, "Months", true, 2026, 3, 31, "", "");
  assert(range.from == "2026-02-28");
  assert(range.to == "2026-03-31");
  assert(immich_format_iso_date_offset(2026, 1, 1, -2) == "2025-12-30");
  assert(immich_format_iso_date_offset(2026, 12, 31, 2) == "2027-01-02");
  std::string csv;
  append_csv_value(csv, "a");
  append_csv_value(csv, "b");
  append_csv_value(csv, "c");
  assert(csv == "a,b,c");
  assert(csv_value_at(csv, 0) == "a");
  assert(csv_value_at(csv, 2) == "c");
  assert(csv_value_at(csv, 3).empty());
  assert(!range.relative_skipped_for_invalid_time);
  assert(build_immich_date_filter_extra(range) ==
         "\"takenAfter\":\"2026-02-28T00:00:00.000Z\","
         "\"takenBefore\":\"2026-03-31T23:59:59.999Z\"");
  ImmichFilterConfig dated_config;
  ImmichFilterBranch dated_branch;
  apply_immich_date_range(dated_config, range);
  assert(build_immich_filter_search_body(
             dated_config, dated_branch, ImmichApiGeneration::V31_FLAT, 6, true)
             .find("\"takenAfter\":\"2026-02-28T00:00:00.000Z\"") != std::string::npos);
  assert(build_immich_filter_search_body(
             dated_config, dated_branch, ImmichApiGeneration::V31_FLAT, 6, true)
             .find("\"takenBefore\":\"2026-03-31T23:59:59.999Z\"") != std::string::npos);

  ImmichDateRange skipped = resolve_immich_date_filter(
      true, "Relative Range", 2, "Years", false, 0, 0, 0, "", "");
  assert(skipped.relative_skipped_for_invalid_time);
  assert(skipped.from.empty());
  assert(skipped.to.empty());

  ImmichDateRange fixed = resolve_immich_date_filter(
      true, "Fixed Range", 1, "Months", true, 2026, 4, 21,
      "2024-05-01", "2024-05-31");
  assert(build_immich_companion_date_filter_extra("2024-05-10", fixed) ==
         "\"takenAfter\":\"2024-05-10T00:00:00.000Z\","
         "\"takenBefore\":\"2024-05-10T23:59:59.999Z\"");
  assert(portrait_pairing_range_days("Same Day") == 0);
  assert(portrait_pairing_range_days("Within 1 Day") == 1);
  assert(portrait_pairing_range_days("Within 2 Days") == 2);
  ImmichDateRange no_filter;
  assert(build_immich_companion_date_filter_extra("2026-01-01", no_filter, 2) ==
         "\"takenAfter\":\"2025-12-30T00:00:00.000Z\","
         "\"takenBefore\":\"2026-01-03T23:59:59.999Z\"");
  assert(build_immich_companion_date_filter_extra("2024-03-01", no_filter, 1) ==
         "\"takenAfter\":\"2024-02-29T00:00:00.000Z\","
         "\"takenBefore\":\"2024-03-02T23:59:59.999Z\"");
  ImmichDateRange clipped{"2026-04-20", "2026-04-22", false};
  assert(build_immich_companion_date_filter_extra("2026-04-21", clipped, 2) ==
         "\"takenAfter\":\"2026-04-20T00:00:00.000Z\","
         "\"takenBefore\":\"2026-04-22T23:59:59.999Z\"");

  std::vector<ImmichPortraitCompanionCandidate> companion_candidates = {
      {"primary", "2026-04-21T12:00:00", true},
      {"landscape-near", "2026-04-21T12:01:00", false},
      {"portrait-far", "2026-04-23T12:00:00", true},
      {"portrait-close", "2026-04-21T13:00:00", true},
      {"portrait-tie", "2026-04-21T11:00:00", true},
  };
  assert(pick_closest_immich_portrait_companion_asset_id(
             companion_candidates, "primary", "2026-04-21T12:00:00") ==
         "portrait-close");

  assert(build_uuid_json_array(" a, b ,, c ") == "[\"a\",\"b\",\"c\"]");
  assert(immich_source_has_required_ids("All Photos", "", "", ""));
  assert(immich_source_has_required_ids("Favorites", "", "", ""));
  assert(immich_source_has_required_ids("Memories", "", "", ""));
  assert(immich_source_has_required_ids("Album", " a ", "", ""));
  assert(!immich_source_has_required_ids("Album", " , ", "", ""));
  assert(immich_source_has_required_ids("Person", "", " p1 ", ""));
  assert(!immich_source_has_required_ids("Person", "", "", ""));
  assert(immich_source_has_required_ids("Tag", "", "", " t1,t2 "));
  assert(!immich_source_has_required_ids("Tag", "", "", " , "));
  assert(immich_source_setup_title("Album") == "Album source needs setup");
  assert(immich_source_setup_title("Person") == "Person source needs setup");
  assert(immich_source_setup_title("Tag") == "Tag source needs setup");
  assert(immich_source_setup_message("Album") ==
         "Open ESPFrame settings and add at least one album, or choose All Photos.");
  assert(immich_source_setup_message("Person") ==
         "Open ESPFrame settings and add at least one person, or choose All Photos.");
  assert(immich_source_setup_message("Tag") ==
         "Open ESPFrame settings and add at least one tag, or choose All Photos.");
  assert(immich_source_setup_message("Custom") ==
         "Open ESPFrame settings and add IDs to every enabled group, or choose All Photos.");
  assert(!immich_dimensions_are_portrait(1920, 1080, "6", false));
  assert(immich_dimensions_are_portrait(1920, 1080, "6", true));
  assert(immich_dimensions_are_portrait(1080, 1920, "6", false));
  assert(pick_one_uuid_from_csv(" a, b ,, c ") == "a");
  assert(select_immich_tag_ids("t1,t2", "Any selected tag") == "t1");
  assert(select_immich_tag_ids("t1,t2", "All selected tags") == "t1,t2");
  assert(build_immich_metadata_count_cache_key(
             "Album", "album-a", "", "", "dates") ==
         "Album|album-a|||dates");
  int album_order_index = 0;
  assert(pick_album_id_for_metadata_search(" a, b, c ", "Album list order", album_order_index) == "a");
  assert(album_order_index == 1);
  assert(pick_album_id_for_metadata_search(" a, b, c ", "Album list order", album_order_index) == "b");
  assert(album_order_index == 2);
  assert(pick_album_id_for_metadata_search(" a, b, c ", "Album list order", album_order_index) == "c");
  assert(album_order_index == 0);
  album_order_index = 9;
  assert(pick_album_id_for_metadata_search(" a, b ", "Album list order", album_order_index) == "a");
  assert(album_order_index == 1);
  assert(pick_album_id_for_metadata_search(" , , ", "Album list order", album_order_index).empty());
  assert(album_order_index == 0);
  album_order_index = 1;
  assert(pick_album_id_for_metadata_search(" a, b, c ", "Random albums", album_order_index) == "a");
  assert(album_order_index == 1);
  assert(build_immich_search_body(1, true, "Favorites", "", "", "").find("\"isFavorite\":true") !=
         std::string::npos);
  assert(build_immich_search_body(1, true, "All Photos", "", "", "").find("\"visibility\":\"timeline\"") !=
         std::string::npos);
  assert(build_immich_search_body(1, false, "Person", "", "p1,p2", "").find("\"personIds\":[\"p1\"]") !=
         std::string::npos);
  assert(build_immich_search_body(1, false, "Tag", "", "", "t1,t2").find("\"tagIds\":[\"t1\",\"t2\"]") !=
         std::string::npos);
  assert(immich_metadata_page_for_total(0) == 1);
  assert(immich_metadata_page_for_total(848) == 1);
  assert(immich_metadata_page_for_total(848, 5) <= 170);
  assert(immich_metadata_page_count_for_total(0) == 1);
  assert(immich_metadata_page_count_for_total(753, 1) == 753);
  assert(immich_metadata_page_count_for_total(753, 5) == 151);
  ImmichRequestState metadata_state;
  initialize_immich_metadata_page_range(metadata_state, 753, 1, true);
  assert(metadata_state.metadata_page_size == 1);
  assert(metadata_state.metadata_max_page == 753);
  assert(metadata_state.metadata_page == 1);
  assert(metadata_state.metadata_page_bound_is_upper);
  metadata_state.metadata_page = 151;
  metadata_state.metadata_max_page = 151;
  assert(retry_empty_immich_metadata_page(metadata_state));
  assert(metadata_state.metadata_max_page == 150);
  assert(metadata_state.metadata_page == 75);
  assert(metadata_state.metadata_empty_page_probes == 1);
  // Probing is internal to the chosen album; it must not advance list order.
  assert(album_order_index == 1);
  metadata_state.metadata_page = 10;
  metadata_state.metadata_max_page = 10;
  for (uint8_t probe = metadata_state.metadata_empty_page_probes;
       probe < MAX_EMPTY_METADATA_PAGE_PROBES; probe++) {
    metadata_state.metadata_page = 10;
    metadata_state.metadata_max_page = 10;
    assert(retry_empty_immich_metadata_page(metadata_state));
  }
  assert(metadata_state.metadata_empty_page_probes == MAX_EMPTY_METADATA_PAGE_PROBES);
  metadata_state.metadata_page = 42;
  metadata_state.metadata_empty_page_probes = MAX_EMPTY_METADATA_PAGE_PROBES;
  assert(retry_empty_immich_metadata_page(metadata_state));
  assert(metadata_state.metadata_page == 1);
  assert(metadata_state.metadata_page1_fallback_attempted);
  assert(!retry_empty_immich_metadata_page(metadata_state));
  metadata_state.register_success();
  assert(metadata_state.metadata_empty_page_probes == 0);
  assert(!metadata_state.metadata_page_bound_is_upper);
  assert(!metadata_state.metadata_page1_fallback_attempted);
  initialize_immich_metadata_page_range(metadata_state, 109, 5, false);
  assert(metadata_state.metadata_max_page == 22);
  assert(!retry_empty_immich_metadata_page(metadata_state));
  assert(!immich_source_uses_metadata_search("All Photos"));
  assert(!immich_source_uses_metadata_search("Favorites"));
  assert(immich_source_uses_metadata_search("Album"));
  assert(!immich_source_uses_metadata_search("Person"));
  assert(!immich_source_uses_metadata_search("Tag"));
  std::string album_metadata = build_immich_metadata_search_body(
      7, 5, true, "Album", "album-a", "", "", "\"takenAfter\":\"2026-01-01T00:00:00.000Z\"");
  assert(album_metadata.find("\"page\":7") != std::string::npos);
  assert(album_metadata.find("\"size\":5") != std::string::npos);
  assert(album_metadata.find("\"visibility\":\"timeline\"") != std::string::npos);
  assert(album_metadata.find("\"albumIds\":[\"album-a\"]") != std::string::npos);
  assert(album_metadata.find("\"withPeople\":true") != std::string::npos);
  assert(album_metadata.find("\"takenAfter\":\"2026-01-01T00:00:00.000Z\"") !=
         std::string::npos);
  assert(build_immich_metadata_search_body(2, 1, true, "All Photos", "", "", "")
             .find("\"page\":2") != std::string::npos);
  assert(build_immich_metadata_search_body(3, 1, true, "Favorites", "", "", "")
             .find("\"isFavorite\":true") != std::string::npos);
  assert(build_immich_metadata_search_body(1, 1, false, "Person", "", "p1", "")
             .find("\"personIds\":[\"p1\"]") != std::string::npos);
  assert(build_immich_metadata_search_body(1, 1, false, "Tag", "", "", "t1,t2")
             .find("\"tagIds\":[\"t1\",\"t2\"]") != std::string::npos);
  std::string companion_search = build_immich_companion_search_body(
      IMMICH_COMPANION_SEARCH_SIZE, "Album", "album-a",
      "\"takenAfter\":\"2026-01-01T00:00:00.000Z\"");
  assert(companion_search.find("\"size\":20") != std::string::npos);
  assert(companion_search.find("\"albumIds\":[\"album-a\"]") != std::string::npos);
  assert(companion_search.find("\"page\"") == std::string::npos);
  assert(companion_search.find("\"withPeople\"") == std::string::npos);
  std::string companion_metadata = build_immich_companion_metadata_search_body(
      3, IMMICH_COMPANION_SEARCH_SIZE, "Album", "album-a",
      "\"takenAfter\":\"2026-01-01T00:00:00.000Z\"");
  assert(companion_metadata.find("\"page\":3") != std::string::npos);
  assert(companion_metadata.find("\"size\":20") != std::string::npos);
  assert(companion_metadata.find("\"albumIds\":[\"album-a\"]") != std::string::npos);
  std::string album_statistics = build_immich_statistics_search_body(
      "Album", "album-a", "", "", "\"takenAfter\":\"2026-01-01T00:00:00.000Z\"");
  assert(album_statistics.find("\"type\":\"IMAGE\"") != std::string::npos);
  assert(album_statistics.find("\"visibility\":\"timeline\"") != std::string::npos);
  assert(album_statistics.find("\"albumIds\":[\"album-a\"]") != std::string::npos);
  assert(album_statistics.find("\"takenAfter\":\"2026-01-01T00:00:00.000Z\"") !=
         std::string::npos);
  assert(album_statistics.find("\"page\"") == std::string::npos);
  assert(album_statistics.find("\"size\"") == std::string::npos);
  assert(build_immich_statistics_search_body("Person", "", "p1", "")
             .find("\"personIds\":[\"p1\"]") != std::string::npos);
  assert(build_immich_statistics_search_body("Tag", "", "", "t1,t2")
             .find("\"tagIds\":[\"t1\",\"t2\"]") != std::string::npos);

  std::vector<ImmichTimelineBucketInfo> large_album_buckets = {
      {"2026-05-01", 848},
      {"2026-04-01", 12},
  };
  ImmichTimelineBucketChoice bucket =
      pick_immich_timeline_bucket_from_choices(large_album_buckets);
  assert(bucket.time_bucket == "2026-05-01");
  assert(bucket.count == 848);
  assert(bucket.page == 1);
  assert(immich_album_page_for_count(848) == 1);
  assert(immich_album_page_for_count(848, 16) <= 53);

  std::vector<ImmichTimelineAssetCandidate> timeline_page = {
      {"video", false, true, 1.0f},
      {"portrait", true, true, 0.75f},
      {"landscape", true, true, 1.5f},
  };
  assert(pick_immich_timeline_asset_id_from_candidates(timeline_page, "Any") == "portrait");
  assert(pick_immich_timeline_asset_id_from_candidates(timeline_page, "Portrait Only") == "portrait");
  assert(pick_immich_timeline_asset_id_from_candidates(timeline_page, "Landscape Only") == "landscape");
  assert(pick_immich_timeline_asset_id_from_candidates({}, "Any").empty());
  assert(pick_immich_timeline_asset_id_from_candidates({{"no-ratio", true, false, 0.0f}},
                                                       "Portrait Only").empty());
}

static void test_smart_filter_helpers() {
  const std::string album1 = "11111111-1111-4111-8111-111111111111";
  const std::string album2 = "22222222-2222-4222-8222-222222222222";
  const std::string person1 = "33333333-3333-4333-8333-333333333333";
  const std::string tag1 = "44444444-4444-4444-8444-444444444444";
  const std::string excluded = "55555555-5555-4555-8555-555555555555";

  assert(immich_api_generation_for_version("3.1.9") == ImmichApiGeneration::V31_FLAT);
  assert(immich_api_generation_for_version("3.2.0") == ImmichApiGeneration::V32_STRUCTURED);
  assert(immich_api_generation_for_version("4.0.0") == ImmichApiGeneration::V32_STRUCTURED);
  assert(immich_api_generation_for_version("unknown") == ImmichApiGeneration::V31_FLAT);
  std::string discovered_version;
  ImmichApiGeneration discovered_generation = ImmichApiGeneration::V31_FLAT;
  assert(parse_immich_server_version(
      "{\"major\":3,\"minor\":2,\"patch\":7}", &discovered_version,
      &discovered_generation));
  assert(discovered_version == "3.2.7");
  assert(discovered_generation == ImmichApiGeneration::V32_STRUCTURED);
  assert(!parse_immich_server_version("{}", &discovered_version, &discovered_generation));
  assert(immich_json_escape("A\"B\\C\n") == "A\\\"B\\\\C\\n");
  assert(is_valid_immich_uuid(album1));
  assert(!is_valid_immich_uuid("not-a-uuid"));
  assert(build_valid_uuid_json_array(album1 + ",bad").find("bad") == std::string::npos);

  ImmichFilterConfig empty;
  assert(!immich_filter_has_missing_enabled_ids(empty));
  ImmichFilterBranch no_branch;
  std::string flat_empty = build_immich_filter_search_body(
      empty, no_branch, ImmichApiGeneration::V31_FLAT, 6, true);
  assert(flat_empty.find("\"type\":\"IMAGE\"") != std::string::npos);
  assert(flat_empty.find("\"visibility\":\"timeline\"") != std::string::npos);
  assert(flat_empty.find("\"filter\"") == std::string::npos);

  ImmichFilterConfig constrained;
  constrained.favorite_mode = "Exclude favorites";
  constrained.taken_after = "2026-01-01T00:00:00.000Z";
  constrained.taken_before = "2026-12-31T23:59:59.999Z";
  constrained.country = "New » Zealand";
  constrained.state = "Wellington";
  constrained.city = "Te Aro";
  std::string flat = build_immich_filter_search_body(
      constrained, no_branch, ImmichApiGeneration::V31_FLAT, 1, false);
  assert(flat.find("\"isFavorite\":false") != std::string::npos);
  assert(flat.find("\"takenAfter\"") != std::string::npos);
  assert(flat.find("\"country\":\"New » Zealand\"") != std::string::npos);
  assert(flat.find("\"filter\"") == std::string::npos);
  assert(immich_filter_location_is_valid(constrained));
  constrained.country.clear();
  assert(!immich_filter_location_is_valid(constrained));

  ImmichFilterConfig combined;
  combined.albums_enabled = true;
  combined.people_enabled = true;
  combined.tags_enabled = true;
  combined.album_ids = album1 + "," + album2;
  combined.person_ids = person1;
  combined.tag_ids = tag1;
  combined.album_matching = "All selected";
  combined.person_matching = "All selected";
  combined.tag_matching = "Any selected";
  combined.minimum_rating = 4;
  combined.excluded_album_ids = excluded;
  combined.favorite_mode = "Favorites only";
  assert(!immich_filter_has_missing_enabled_ids(combined));
  uint8_t group_index = 0;
  int album_index = 0;
  ImmichFilterBranch all = select_immich_filter_branch(
      combined, group_index, album_index, "Album list order",
      ImmichApiGeneration::V32_STRUCTURED);
  assert(all.group == "All");
  assert(all.album_ids == combined.album_ids);
  std::string structured = build_immich_filter_search_body(
      combined, all, ImmichApiGeneration::V32_STRUCTURED, 10, true, true, 3);
  assert(structured.find("\"page\":3") != std::string::npos);
  assert(structured.find("\"filter\":{") != std::string::npos);
  assert(structured.find("\"type\":{\"eq\":\"IMAGE\"}") != std::string::npos);
  assert(structured.find("\"rating\":{\"gte\":4}") != std::string::npos);
  assert(structured.find("\"albumIds\":{\"all\"") != std::string::npos);
  assert(structured.find("\"none\":[\"" + excluded + "\"]") != std::string::npos);
  assert(structured.find("\"visibility\":\"timeline\"") == std::string::npos);
  std::string statistics = build_immich_filter_statistics_body(
      combined, all, ImmichApiGeneration::V32_STRUCTURED);
  assert(statistics.find("\"size\"") == std::string::npos);
  assert(statistics.find("\"withExif\"") == std::string::npos);
  assert(statistics.find("\"filter\":{") != std::string::npos);
  assert(immich_filter_requires_v32(combined));
  assert(immich_filter_branch_uses_album(all));
  assert(!immich_filter_can_use_album_asset_count(combined, all));

  ImmichFilterConfig album_only;
  album_only.albums_enabled = true;
  album_only.album_ids = album1;
  ImmichFilterBranch album_only_branch;
  album_only_branch.group = "Album";
  album_only_branch.album_ids = album1;
  assert(immich_filter_can_use_album_asset_count(album_only, album_only_branch));
  album_only.favorite_mode = "Favorites only";
  assert(!immich_filter_can_use_album_asset_count(album_only, album_only_branch));
  album_only.favorite_mode = "Any";
  album_only.taken_after = "2026-01-01T00:00:00.000Z";
  assert(!immich_filter_can_use_album_asset_count(album_only, album_only_branch));
  album_only.taken_after.clear();
  album_only_branch.person_ids = person1;
  assert(!immich_filter_can_use_album_asset_count(album_only, album_only_branch));

  ImmichFilterConfig intersecting_any;
  intersecting_any.albums_enabled = true;
  intersecting_any.people_enabled = true;
  intersecting_any.album_ids = album1 + "," + album2;
  intersecting_any.person_ids = person1 + "," + excluded;
  intersecting_any.album_matching = "Any selected";
  intersecting_any.person_matching = "Any selected";
  intersecting_any.inclusion_matching = "Match all enabled groups";
  assert(immich_filter_requires_v32(intersecting_any));
  ImmichFilterBranch complete_intersection = select_immich_filter_branch(
      intersecting_any, group_index, album_index, "Album list order",
      ImmichApiGeneration::V32_STRUCTURED);
  assert(complete_intersection.group == "All");
  assert(complete_intersection.album_ids == intersecting_any.album_ids);
  assert(complete_intersection.person_ids == intersecting_any.person_ids);
  std::string complete_intersection_body = build_immich_filter_search_body(
      intersecting_any, complete_intersection, ImmichApiGeneration::V32_STRUCTURED,
      10, true);
  assert(complete_intersection_body.find("\"albumIds\":{\"any\":[\"" + album1 +
                                         "\",\"" + album2 + "\"]}") != std::string::npos);
  assert(complete_intersection_body.find("\"personIds\":{\"any\":[\"" + person1 +
                                         "\",\"" + excluded + "\"]}") != std::string::npos);
  ImmichFilterBranch sampled_flat_intersection = select_immich_filter_branch(
      intersecting_any, group_index, album_index, "Album list order",
      ImmichApiGeneration::V31_FLAT);
  assert(split_valid_uuid_csv(sampled_flat_intersection.person_ids).size() == 1);
  ImmichFilterConfig intersecting_any_tags = intersecting_any;
  intersecting_any_tags.people_enabled = false;
  intersecting_any_tags.person_ids.clear();
  intersecting_any_tags.tags_enabled = true;
  intersecting_any_tags.tag_ids = tag1 + "," + excluded;
  intersecting_any_tags.tag_matching = "Any selected";
  assert(immich_filter_requires_v32(intersecting_any_tags));
  intersecting_any.inclusion_matching = "Match any enabled group";
  assert(!immich_filter_requires_v32(intersecting_any));

  ImmichFilterConfig retry_any_person;
  retry_any_person.people_enabled = true;
  retry_any_person.person_ids = person1 + "," + excluded;
  retry_any_person.person_matching = "Any selected person";
  retry_any_person.inclusion_matching = "Match any enabled group";
  uint8_t retry_group_index = 0;
  int retry_album_index = 0;
  ImmichFilterBranch structured_any_person = select_immich_filter_branch(
      retry_any_person, retry_group_index, retry_album_index, "Random albums",
      ImmichApiGeneration::V32_STRUCTURED);
  assert(split_valid_uuid_csv(structured_any_person.person_ids).size() == 1);
  uint8_t empty_id_attempts = 0;
  assert(retry_next_any_selected_id(
      retry_any_person, ImmichApiGeneration::V32_STRUCTURED,
      empty_id_attempts, structured_any_person));
  assert(structured_any_person.person_ids == retry_any_person.person_ids);
  assert(!retry_next_any_selected_id(
      retry_any_person, ImmichApiGeneration::V32_STRUCTURED,
      empty_id_attempts, structured_any_person));

  retry_group_index = 0;
  ImmichFilterBranch flat_any_person = select_immich_filter_branch(
      retry_any_person, retry_group_index, retry_album_index, "Random albums",
      ImmichApiGeneration::V31_FLAT);
  const std::string first_flat_person = flat_any_person.person_ids;
  empty_id_attempts = 0;
  assert(retry_next_any_selected_id(
      retry_any_person, ImmichApiGeneration::V31_FLAT,
      empty_id_attempts, flat_any_person));
  assert(flat_any_person.person_ids != first_flat_person);
  assert(!retry_next_any_selected_id(
      retry_any_person, ImmichApiGeneration::V31_FLAT,
      empty_id_attempts, flat_any_person));

  ImmichFilterConfig all_albums_only;
  all_albums_only.albums_enabled = true;
  all_albums_only.album_ids = album1 + "," + album2;
  all_albums_only.album_matching = "All selected albums";
  assert(immich_filter_requires_v32(all_albums_only));
  all_albums_only.album_ids = album1;
  assert(!immich_filter_requires_v32(all_albums_only));
  all_albums_only.album_ids = album1 + "," + album2;
  all_albums_only.albums_enabled = false;
  assert(!immich_filter_requires_v32(all_albums_only));

  ImmichFilterConfig missing_enabled_ids;
  missing_enabled_ids.albums_enabled = true;
  assert(immich_filter_has_missing_enabled_ids(missing_enabled_ids));
  missing_enabled_ids.album_ids = album1;
  assert(!immich_filter_has_missing_enabled_ids(missing_enabled_ids));
  missing_enabled_ids.people_enabled = true;
  assert(immich_filter_has_missing_enabled_ids(missing_enabled_ids));
  missing_enabled_ids.person_ids = person1;
  missing_enabled_ids.tags_enabled = true;
  missing_enabled_ids.tag_ids = "not-a-uuid";
  assert(immich_filter_has_missing_enabled_ids(missing_enabled_ids));
  missing_enabled_ids.tag_ids = tag1;
  assert(!immich_filter_has_missing_enabled_ids(missing_enabled_ids));

  combined.inclusion_matching = "Match any enabled group";
  ImmichFilterBranch first = select_immich_filter_branch(
      combined, group_index, album_index, "Album list order", ImmichApiGeneration::V31_FLAT);
  ImmichFilterBranch second = select_immich_filter_branch(
      combined, group_index, album_index, "Album list order", ImmichApiGeneration::V31_FLAT);
  ImmichFilterBranch third = select_immich_filter_branch(
      combined, group_index, album_index, "Album list order", ImmichApiGeneration::V31_FLAT);
  ImmichFilterBranch fourth = select_immich_filter_branch(
      combined, group_index, album_index, "Album list order", ImmichApiGeneration::V31_FLAT);
  assert(first.group == "Album");
  assert(second.group == "Person");
  assert(third.group == "Tag");
  assert(fourth.group == "Album");
  assert(first.album_ids == combined.album_ids);  // All-selected within the sampled group.
  assert(second.album_ids.empty() && !second.person_ids.empty());
  assert(third.person_ids.empty() && !third.tag_ids.empty());

  ImmichFilterConfig legacy;
  assert(legacy_source_for_filter(legacy) == "All Photos");
  legacy.favorite_mode = "Favorites only";
  assert(legacy_source_for_filter(legacy) == "Favorites");
  legacy.favorite_mode = "Any";
  legacy.albums_enabled = true;
  legacy.album_ids = album1;
  assert(legacy_source_for_filter(legacy) == "Album");
  legacy.minimum_rating = 1;
  assert(legacy_source_for_filter(legacy) == "Custom");

  for (const std::string &source : {"All Photos", "Favorites", "Album", "Person", "Tag", "Memories"}) {
    ImmichFilterConfig migrated;
    migrated.album_ids = album1;
    migrated.person_ids = person1;
    migrated.tag_ids = tag1;
    migrated.taken_after = "2020-01-01T00:00:00.000Z";
    bool notice = false;
    apply_legacy_source_to_filter(source, &migrated, &notice);
    assert(migrated.albums_enabled == (source == "Album"));
    assert(migrated.people_enabled == (source == "Person"));
    assert(migrated.tags_enabled == (source == "Tag"));
    assert(migrated.favorite_mode == (source == "Favorites" ? "Favorites only" : "Any"));
    assert(notice == (source == "Memories"));
    assert(migrated.album_ids == album1 && migrated.person_ids == person1 && migrated.tag_ids == tag1);
    assert(!migrated.taken_after.empty());
  }
}

static void test_immich_request_state() {
  ImmichRequestState state;
  assert(!state.cooldown_active(100));
  assert(state.retry_delay_ms == 2000);
  assert(state.random_request_is_current());
  state.begin_random_request();
  assert(state.random_request_is_current());
  state.invalidate_photo_source_requests();
  assert(!state.random_request_is_current());
  state.begin_random_request();
  assert(state.random_request_is_current());
  uint32_t source_generation = state.photo_source_generation;
  state.reset();
  assert(state.photo_source_generation == source_generation + 1);
  assert(!state.random_request_is_current());
  assert(!state.filter_apply_pending);
  state.filter_apply_pending = true;
  state.reset();
  assert(!state.filter_apply_pending);
  assert(!state.register_capability_discovery_failure());
  assert(!state.register_capability_discovery_failure());
  assert(state.register_capability_discovery_failure());
  assert(state.capability_discovery_failures == IMMICH_CAPABILITY_DISCOVERY_MAX_ATTEMPTS);
  assert(state.server_version_discovered);
  state.register_capability_discovery_success();
  assert(state.capability_discovery_failures == 0);
  assert(state.server_version_discovered);
  uint32_t cached_total = 0;
  bool cached_upper_bound = false;
  assert(!state.find_metadata_count("album-a", 1000, &cached_total, &cached_upper_bound));
  state.remember_metadata_count("album-a", 123, true, 1000);
  assert(state.find_metadata_count("album-a", 1000, &cached_total, &cached_upper_bound));
  assert(cached_total == 123);
  assert(cached_upper_bound);
  assert(state.find_metadata_count(
      "album-a", 1000 + IMMICH_METADATA_COUNT_CACHE_TTL_MS - 1,
      &cached_total, &cached_upper_bound));
  assert(!state.find_metadata_count(
      "album-a", 1000 + IMMICH_METADATA_COUNT_CACHE_TTL_MS,
      &cached_total, &cached_upper_bound));
  state.remember_metadata_count("album-a", 99, false, 2000);
  assert(state.find_metadata_count("album-a", 2000, &cached_total, &cached_upper_bound));
  assert(cached_total == 99);
  assert(!cached_upper_bound);

  state.begin_memory_search();
  assert(state.memory_window_offset == -2);
  assert(state.memory_asset_id.empty());
  state.add_memory_image("asset-a");
  assert(state.memory_image_count == 1);
  assert(state.memory_asset_id == "asset-a");
  state.add_memory_image("");
  assert(state.memory_image_count == 1);
  state.add_memory_image("asset-b");
  assert(state.memory_image_count == 2);
  // One candidate is kept and replaced with probability 1/n; the stubbed
  // esp_random() always returns zero, so every replacement roll succeeds and the
  // most recent asset wins.
  assert(state.memory_asset_id == "asset-b");
  assert(state.advance_memory_window());
  assert(state.memory_window_offset == -1);

  assert(state.register_request_error() == 1);
  assert(state.prepare_retry_delay() == 2000);
  assert(state.register_request_error() == 2);
  assert(state.prepare_retry_delay() == 5000);
  assert(state.retry_available(3));
  assert(state.register_request_error() == 3);
  assert(!state.retry_available(3));
  assert(state.prepare_retry_delay() == 10000);

  state.reset();
  assert(state.register_fetch_failure(1000) == 3000);
  assert(state.cooldown_active(3999));
  assert(!state.cooldown_active(4000));
  assert(state.register_fetch_failure(5000) == 3000);
  assert(state.register_fetch_failure(9000) == 7000);
  assert(state.consecutive_failures == 3);

  state.register_success();
  assert(state.consecutive_failures == 0);
  assert(state.api_retries == 0);
  assert(!state.cooldown_active(9001));

  state.record_http_failure(503, 10000);
  assert(state.last_http_status == 503);
  assert(state.failure_window_started_ms == 10000);
  assert(state.cooldown_active(39999));
  state.reset_retries_and_pause(50000, 2500);
  assert(state.api_retries == 0);
  assert(state.cooldown_active(52499));

  state.reset();
  for (int i = 0; i < 4; i++) state.register_download_failure(60000 + i);
  assert(state.consecutive_failures == 4);
  assert(state.retry_cooldown_until_ms == 70003);
}

static SlotMeta make_slot(const std::string &asset_id, bool portrait) {
  SlotMeta meta;
  meta.asset_id = asset_id;
  meta.pending_asset_id = asset_id;
  meta.image_url = "https://example.test/" + asset_id;
  meta.datetime = "2026-04-21T12:34:56";
  meta.is_portrait = portrait;
  return meta;
}

static void test_slideshow_slot_actions() {
  SlotFlags flags;
  int noncritical_count = 0;
  int retries = 2;
  bool displayed = false;
  DisplayMeta current;
  PortraitState portrait;
  int companion_slot = -1;
  std::string search_datetime;
  std::string primary_asset_id;

  SlotMeta active = make_slot("landscape", false);
  flags.fetch_in_flight[0] = true;
  SlideshowAction action = SlideshowController::handle_slot_download_finished(
      0, active, flags, noncritical_count, retries, 0, true, displayed, current,
      portrait, companion_slot, -1, search_datetime, primary_asset_id);
  assert(action == SLIDESHOW_ACTION_DISPLAY_CURRENT);
  assert(active.ready);
  assert(displayed);
  assert(current.asset_id == "landscape");
  assert(!flags.fetch_in_flight[0]);
  assert(retries == 0);

  displayed = false;
  current = DisplayMeta{};
  SlotMeta active_portrait = make_slot("portrait-active", true);
  action = SlideshowController::handle_slot_download_finished(
      1, active_portrait, flags, noncritical_count, retries, 1, true, displayed, current,
      portrait, companion_slot, -1, search_datetime, primary_asset_id);
  assert(action == SLIDESHOW_ACTION_START_ACTIVE_PAIR);
  assert(!displayed);
  assert(current.asset_id.empty());

  SlotMeta queued_portrait = make_slot("portrait-prefetch", true);
  action = SlideshowController::handle_slot_download_finished(
      2, queued_portrait, flags, noncritical_count, retries, 0, true, displayed, current,
      portrait, companion_slot, -1, search_datetime, primary_asset_id);
  assert(action == SLIDESHOW_ACTION_FETCH_COMPANION);
  assert(companion_slot == 2);
  assert(search_datetime == queued_portrait.datetime);
  assert(primary_asset_id == queued_portrait.asset_id);

  SlotMeta stale = make_slot("new", false);
  stale.pending_asset_id = "old";
  flags.fetch_in_flight[0] = true;
  action = SlideshowController::handle_slot_download_finished(
      0, stale, flags, noncritical_count, retries, 0, true, displayed, current,
      portrait, companion_slot, -1, search_datetime, primary_asset_id);
  assert(action == SLIDESHOW_ACTION_NONE);
  assert(!stale.ready);
  assert(!flags.fetch_in_flight[0]);
}

static void test_fetch_queue_and_error_handling() {
  SlotMeta slot0 = make_slot("active", false);
  SlotMeta slot1 = make_slot("next", false);
  SlotMeta slot2 = make_slot("next-next", false);
  slot0.ready = true;
  slot1.ready = false;
  slot2.ready = false;

  SlotFlags flags;
  flags.fetch_in_flight[2] = true;
  FetchQueue queue;
  assert(SlideshowController::enqueue_prefetch_slots(queue, 0, slot0, slot1, slot2, flags, 1234));
  FetchJob job;
  assert(queue.pop(job));
  assert(job.kind == FETCH_JOB_SLOT);
  assert(job.slot == 1);
  assert(job.priority == 20);
  assert(job.queued_ms == 1234);
  assert(queue.empty());

  int noncritical_count = 1;
  std::string reason;
  int last_downloaded = -1;
  flags.fetch_in_flight[1] = true;
  flags.fetch_started_ms[1] = 4321;
  flags.noncritical_update[1] = true;
  SlideshowController::handle_slot_download_error(
      1, flags, noncritical_count, reason, last_downloaded, "slot1 image error");
  assert(!flags.fetch_in_flight[1]);
  assert(flags.fetch_started_ms[1] == 0);
  assert(!flags.noncritical_update[1]);
  assert(noncritical_count == 0);
  assert(reason == "slot1 image error");
  assert(last_downloaded == 1);
}

static void test_slideshow_component_commands() {
  EspFrameSlideshow slideshow;
  assert(!slideshow.has_command());
  slideshow.state().slot0 = make_slot("state-owned", false);
  slideshow.state().active_slot = 2;
  slideshow.state().portrait.workflow_busy = true;
  assert(slideshow.emit_command(SLIDESHOW_COMMAND_DISPLAY_CURRENT, 2));
  slideshow.reset_state();
  assert(slideshow.state().slot0.asset_id.empty());
  assert(slideshow.state().active_slot == 0);
  assert(!slideshow.state().portrait.workflow_busy);
  assert(!slideshow.has_command());

  assert(slideshow.emit_command(SLIDESHOW_COMMAND_DISPLAY_CURRENT, 1));
  assert(slideshow.emit_action(SLIDESHOW_ACTION_PREFETCH, 2));
  assert(slideshow.command_count() == 2);

  SlideshowCommand cmd;
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_DISPLAY_CURRENT);
  assert(cmd.slot == 1);
  assert(cmd.delay_ms == 0);

  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_PREFETCH_AFTER_DELAY);
  assert(cmd.slot == 2);
  assert(cmd.delay_ms == 500);
  assert(!slideshow.has_command());

  SlotFlags flags;
  int noncritical_count = 0;
  int retries = 1;
  bool displayed = false;
  DisplayMeta current;
  PortraitState portrait;
  int companion_slot = -1;
  std::string search_datetime;
  std::string primary_asset_id;
  SlotMeta active = make_slot("active-landscape", false);
  flags.fetch_in_flight[0] = true;

  SlideshowAction action = slideshow.on_slot_download_finished(
      0, active, flags, noncritical_count, retries, 0, true, displayed, current,
      portrait, companion_slot, -1, search_datetime, primary_asset_id);
  assert(action == SLIDESHOW_ACTION_DISPLAY_CURRENT);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_DISPLAY_CURRENT);
  assert(cmd.slot == 0);

  std::string reason;
  int last_downloaded = -1;
  flags.fetch_in_flight[2] = true;
  slideshow.on_slot_download_error(2, flags, noncritical_count, reason, last_downloaded,
                                   "slot2 image error");
  assert(reason == "slot2 image error");
  assert(last_downloaded == 2);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_LOG_DIAG);
  assert(cmd.slot == 2);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_HANDLE_SLOT_DOWNLOAD_ERROR);
  assert(cmd.slot == 2);
  assert(!slideshow.has_command());
}

static void test_slideshow_component_prefetch_and_deferred_updates() {
  EspFrameSlideshow slideshow;
  SlotMeta slot0 = make_slot("active", false);
  SlotMeta slot1 = make_slot("next", false);
  SlotMeta slot2 = make_slot("next-next", false);
  slot0.ready = true;
  slot1.ready = false;
  slot2.ready = false;
  SlotFlags flags;
  FetchQueue queue;
  PortraitState portrait;
  uint32_t last_prefetch = 0;
  int target_slot = 0;

  bool queued = slideshow.request_prefetch(
      false, false, 1000, last_prefetch, 0, target_slot, slot0, slot1, slot2,
      flags, queue, portrait, true, 0, -1, false, false);
  assert(queued);
  assert(target_slot == 1);
  assert(last_prefetch == 1000);
  SlideshowCommand cmd;
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_FETCH_INTO_SLOT);
  assert(cmd.slot == 1);

  queued = slideshow.request_prefetch(
      false, false, 1200, last_prefetch, 0, target_slot, slot0, slot1, slot2,
      flags, queue, portrait, true, 0, -1, false, false);
  assert(!queued);
  assert(!slideshow.has_command());

  queued = slideshow.request_prefetch(
      false, false, 2000, last_prefetch, 0, target_slot, slot0, slot1, slot2,
      flags, queue, portrait, false, 0, -1, false, false);
  assert(!queued);
  assert(!slideshow.has_command());

  flags.fetch_in_flight[2] = true;
  queued = slideshow.request_prefetch(
      false, false, 2600, last_prefetch, 0, target_slot, slot0, slot1, slot2,
      flags, queue, portrait, true, 0, -1, false, false);
  assert(!queued);
  assert(!slideshow.has_command());
  flags.fetch_in_flight[2] = false;

  int noncritical_count = 0;
  bool update = slideshow.request_deferred_slot_update(1, 0, flags, false, noncritical_count);
  assert(update);
  assert(noncritical_count == 1);
  assert(flags.noncritical_update[1]);
  assert(flags.fetch_in_flight[1]);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_UPDATE_SLOT_IMAGE);
  assert(cmd.slot == 1);

  slideshow.defer_slot_update_due_to_busy(1, flags, noncritical_count);
  assert(!flags.fetch_in_flight[1]);
  assert(flags.fetch_started_ms[1] == 0);
  assert(!flags.noncritical_update[1]);
  assert(noncritical_count == 0);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_PREFETCH_AFTER_DELAY);
  assert(cmd.slot == 1);

  update = slideshow.request_deferred_slot_update(2, 0, flags, true, noncritical_count);
  assert(!update);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_PREFETCH_AFTER_DELAY);

  bool preload_in_flight = false;
  update = slideshow.request_preload_left_update(false, preload_in_flight, noncritical_count);
  assert(update);
  assert(preload_in_flight);
  assert(noncritical_count == 1);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_UPDATE_PRELOAD_LEFT);

  update = slideshow.request_preload_right_update(true, preload_in_flight, noncritical_count);
  assert(!update);
  assert(!preload_in_flight);
  assert(noncritical_count == 0);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_PREFETCH_AFTER_DELAY);
}

static void test_slideshow_pipeline_watchdog() {
  EspFrameSlideshow slideshow;
  auto &state = slideshow.state();
  state.active_slot = 1;
  state.target_slot = 2;
  state.active_slot_displayed = true;
  state.slot1 = make_slot("visible", false);
  state.slot1.ready = true;
  copy_slot_to_display(state.slot1, state.current_display);
  state.slot_flags.fetch_in_flight[2] = true;
  state.slot_flags.fetch_started_ms[2] = 1000;
  state.slot_flags.noncritical_update[2] = true;
  state.noncritical_remote_updates_in_flight = 2;
  state.preload_noncritical_in_flight = true;
  state.portrait.workflow_busy = true;
  state.portrait_preload_slot = 2;
  state.portrait_preload_left_ready = true;
  state.portrait_preload_right_ready = false;
  state.companion_target_slot = 2;
  state.portrait_companion_url = "https://example.test/companion";
  state.rejected_fetch_target = 0;
  assert(slideshow.emit_command(SLIDESHOW_COMMAND_FETCH_INTO_SLOT, 2));

  assert(!slideshow.pipeline_watchdog_timed_out(1000, true, true));
  assert(state.pipeline_blocked_tracking);
  assert(!slideshow.pipeline_watchdog_timed_out(
      1000 + EspFrameSlideshow::PIPELINE_STALL_TIMEOUT_MS - 1, true, true));
  assert(slideshow.pipeline_watchdog_timed_out(
      1000 + EspFrameSlideshow::PIPELINE_STALL_TIMEOUT_MS, true, true));

  slideshow.recover_stalled_pipeline();
  assert(state.active_slot == 1);
  assert(state.active_slot_displayed);
  assert(state.slot1.ready);
  assert(state.current_display.asset_id == "visible");
  assert(state.target_slot == 1);
  assert(!any_slot_fetch_in_flight(state.slot_flags));
  assert(!state.slot_flags.noncritical_update[2]);
  assert(state.noncritical_remote_updates_in_flight == 0);
  assert(!state.preload_noncritical_in_flight);
  assert(!state.portrait.workflow_busy);
  assert(state.portrait_preload_slot == -1);
  assert(state.companion_target_slot == -1);
  assert(state.portrait_companion_url.empty());
  assert(state.rejected_fetch_target == -1);
  assert(!state.pipeline_blocked_tracking);
  assert(!slideshow.has_command());

  assert(!slideshow.pipeline_watchdog_timed_out(5000, true, true));
  assert(state.pipeline_blocked_tracking);
  assert(!slideshow.pipeline_watchdog_timed_out(6000, false, true));
  assert(!state.pipeline_blocked_tracking);
  assert(!slideshow.pipeline_watchdog_timed_out(7000, true, true));
  assert(!slideshow.pipeline_watchdog_timed_out(8000, true, false));
  assert(!state.pipeline_blocked_tracking);

  const uint32_t wrap_start = 0xFFFFFF00u;
  assert(!slideshow.pipeline_watchdog_timed_out(wrap_start, true, true));
  const uint32_t wrap_timeout = wrap_start + EspFrameSlideshow::PIPELINE_STALL_TIMEOUT_MS;
  assert(slideshow.pipeline_watchdog_timed_out(wrap_timeout, true, true));
}

static void test_slideshow_component_portrait_flow() {
  EspFrameSlideshow slideshow;
  SlotMeta slot0 = make_slot("portrait-active", true);
  slot0.companion_url = "https://example.test/companion";
  SlotMeta slot1 = make_slot("slot1", false);
  SlotMeta slot2 = make_slot("slot2", false);
  PortraitState portrait;
  bool displayed = false;
  std::string primary_id;
  std::string companion_url;
  std::string search_datetime;
  int companion_slot = -1;
  bool search_expanded = false;

  bool started = slideshow.start_active_portrait(
      0, slot0, slot1, slot2, portrait, displayed, primary_id, companion_url,
      search_datetime, companion_slot, search_expanded);
  assert(started);
  assert(portrait.workflow_busy);
  assert(portrait.companion_found);
  assert(portrait.left_requested);
  assert(primary_id == "portrait-active");
  assert(companion_url == slot0.companion_url);

  SlideshowCommand cmd;
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_START_PORTRAIT_LEFT);
  assert(cmd.slot == 0);

  slideshow.on_portrait_left_finished(portrait);
  assert(portrait.left_ready);
  assert(!portrait.left_requested);
  assert(portrait.right_requested);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_START_PORTRAIT_RIGHT);

  slideshow.on_portrait_right_finished(portrait);
  assert(portrait.right_ready);
  assert(!portrait.right_requested);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_DISPLAY_PORTRAIT_PAIR);

  PortraitState searching;
  SlotMeta no_companion = make_slot("portrait-search", true);
  no_companion.companion_url = "";
  bool search_started = slideshow.start_active_portrait(
      0, no_companion, slot1, slot2, searching, displayed, primary_id, companion_url,
      search_datetime, companion_slot, search_expanded);
  assert(search_started);
  assert(searching.workflow_busy);
  assert(!searching.companion_found);
  assert(companion_url.empty());
  assert(search_datetime == no_companion.datetime);
  assert(companion_slot == 0);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_DEFER_COMPANION_SEARCH);

  std::string reason;
  slideshow.on_portrait_left_error(searching, reason, displayed, false);
  assert(reason == "portrait left error");
  assert(displayed);
  assert(!searching.workflow_busy);
  assert(searching.no_companion_active);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_LOG_DIAG);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_DISPLAY_CURRENT);
  slideshow.after_display_current(0, no_companion, slot1, slot2, searching, true, displayed,
                                  companion_slot, false, false);
  assert(!slideshow.has_command());

  PortraitState right_error;
  right_error.workflow_busy = true;
  right_error.companion_found = true;
  right_error.left_ready = true;
  displayed = false;
  slideshow.on_portrait_right_error(right_error, reason, displayed, false);
  assert(reason == "portrait right error");
  assert(displayed);
  assert(!right_error.workflow_busy);
  assert(right_error.no_companion_active);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_LOG_DIAG);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_DISPLAY_CURRENT);
  slideshow.after_display_current(0, no_companion, slot1, slot2, right_error, true, displayed,
                                  companion_slot, false, false);
  assert(!slideshow.has_command());

  // A transient pair download failure can occur while the previous image is
  // still visible. It must release the workflow gate or slideshow advancement
  // remains blocked indefinitely.
  PortraitState visible_left_error;
  visible_left_error.workflow_busy = true;
  visible_left_error.left_requested = true;
  bool already_displayed = true;
  slideshow.on_portrait_left_error(
      visible_left_error, reason, already_displayed, false);
  assert(already_displayed);
  assert(!visible_left_error.workflow_busy);
  assert(!visible_left_error.left_requested);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_LOG_DIAG);
  assert(!slideshow.has_command());

  PortraitState visible_right_error;
  visible_right_error.workflow_busy = true;
  visible_right_error.right_requested = true;
  slideshow.on_portrait_right_error(
      visible_right_error, reason, already_displayed, false);
  assert(already_displayed);
  assert(!visible_right_error.workflow_busy);
  assert(!visible_right_error.right_requested);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_LOG_DIAG);
  assert(!slideshow.has_command());
}

static void test_slideshow_component_preload_flow() {
  EspFrameSlideshow slideshow;
  SlotMeta slot0 = make_slot("active", false);
  SlotMeta slot1 = make_slot("portrait-preload", true);
  slot1.companion_url = "https://example.test/preload-companion";
  SlotMeta slot2 = make_slot("slot2", false);
  bool left_ready = false;
  bool right_ready = false;
  bool preload_in_flight = true;
  int noncritical_count = 1;

  slideshow.on_preload_left_finished(1, slot0, slot1, slot2, left_ready, right_ready,
                                     preload_in_flight, noncritical_count);
  assert(left_ready);
  assert(preload_in_flight);
  assert(noncritical_count == 1);
  SlideshowCommand cmd;
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_START_PRELOAD_RIGHT);
  assert(cmd.slot == 1);

  slideshow.on_preload_right_finished(right_ready, preload_in_flight, noncritical_count);
  assert(right_ready);
  assert(!preload_in_flight);
  assert(noncritical_count == 0);

  std::string reason;
  preload_in_flight = true;
  noncritical_count = 1;
  slideshow.on_preload_left_error(reason, left_ready, preload_in_flight, noncritical_count);
  assert(reason == "portrait preload left error");
  assert(!left_ready);
  assert(!preload_in_flight);
  assert(noncritical_count == 0);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_LOG_DIAG);
}

static void test_slideshow_component_navigation_flow() {
  EspFrameSlideshow slideshow;
  SlotMeta slot0 = make_slot("slot0", false);
  SlotMeta slot1 = make_slot("slot1", false);
  SlotMeta slot2 = make_slot("slot2", true);
  slot0.ready = true;
  slot1.ready = true;
  slot2.ready = true;
  DisplayMeta current;
  copy_slot_to_display(slot0, current);
  DisplayMeta previous;
  PortraitState portrait;
  SlotFlags flags;
  int active_slot = 0;
  int target_slot = 0;
  bool displayed = true;
  uint32_t last_advance = 0;
  int noncritical_count = 0;
  std::string reason;

  slideshow.advance_forward(2000, false, active_slot, target_slot, displayed, last_advance,
                             slot0, slot1, slot2, current, previous, portrait, flags,
                             noncritical_count, true, false, -1, false, false, reason);
  assert(active_slot == 1);
  assert(displayed);
  assert(previous.valid);
  assert(previous.asset_id == "slot0");
  SlideshowCommand cmd;
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_DISPLAY_CURRENT);
  assert(cmd.slot == 1);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_PREFETCH_AFTER_DELAY);

  slideshow.advance_forward(3000, false, active_slot, target_slot, displayed, last_advance,
                             slot0, slot1, slot2, current, previous, portrait, flags,
                             noncritical_count, true, false, -1, false, false, reason);
  assert(active_slot == 2);
  assert(!displayed);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_START_ACTIVE_PAIR);
  assert(cmd.slot == 2);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_PREFETCH_AFTER_DELAY);

  slot2.ready = false;
  flags.fetch_in_flight[2] = true;
  flags.fetch_started_ms[2] = 1000;
  slideshow.advance_forward(20000, false, active_slot, target_slot, displayed, last_advance,
                             slot0, slot1, slot2, current, previous, portrait, flags,
                             noncritical_count, true, false, -1, false, false, reason);
  assert(reason == "h3 stuck slot");
  assert(!flags.fetch_in_flight[2]);
  assert(target_slot == 2);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_ABORT_SLOT_DOWNLOAD);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_LOG_DIAG);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_DEFER_FETCH_INTO_SLOT);
}

static void test_slideshow_component_previous_flow() {
  EspFrameSlideshow slideshow;
  SlotMeta slot0 = make_slot("current", false);
  slot0.filter_person_ids = "person-456,person-789";
  SlotMeta slot1 = make_slot("slot1", false);
  SlotMeta slot2 = make_slot("slot2", false);
  DisplayMeta current;
  copy_slot_to_display(slot0, current);
  assert(current.filter_person_ids == "person-456,person-789");
  DisplayMeta previous;
  previous.asset_id = "previous";
  previous.image_url = "https://example.test/previous";
  previous.datetime = "2026-04-20T10:00:00";
  previous.companion_url = "https://example.test/previous-companion";
  previous.filter_album_ids = "album-123";
  previous.filter_tag_ids = "tag-456";
  previous.is_portrait = true;
  previous.valid = true;
  PortraitState portrait;
  SlotFlags flags;
  int active_slot = 0;
  bool displayed = true;

  flags.fetch_in_flight[2] = true;
  bool shown = slideshow.show_previous(1200, active_slot, displayed, slot0, slot1, slot2,
                                       current, previous, portrait, flags);
  assert(!shown);
  assert(active_slot == 0);
  assert(current.asset_id == "current");
  assert(previous.asset_id == "previous");
  flags.fetch_in_flight[2] = false;

  shown = slideshow.show_previous(1234, active_slot, displayed, slot0, slot1, slot2,
                                  current, previous, portrait, flags);
  assert(shown);
  assert(active_slot == 2);
  assert(!displayed);
  assert(current.asset_id == "previous");
  assert(current.filter_album_ids == "album-123");
  assert(current.filter_tag_ids == "tag-456");
  assert(previous.filter_person_ids == "person-456,person-789");
  assert(slot2.pending_asset_id == "previous");
  assert(slot2.is_portrait);
  assert(slot2.datetime == "2026-04-20T10:00:00");
  assert(slot2.companion_url == "https://example.test/previous-companion");
  assert(slot2.filter_album_ids == "album-123");
  assert(slot2.filter_person_ids.empty());
  assert(slot2.filter_tag_ids == "tag-456");
  assert(flags.fetch_in_flight[2]);
  SlideshowCommand cmd;
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_LOAD_PREVIOUS_SLOT);
  assert(cmd.slot == 2);

  DisplayMeta empty_previous;
  slideshow.show_previous(2000, active_slot, displayed, slot0, slot1, slot2,
                           current, empty_previous, portrait, flags);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_LOG_NO_PREVIOUS);
}

static void test_slideshow_component_companion_result_flow() {
  EspFrameSlideshow slideshow;
  SlotMeta slot0 = make_slot("active-portrait", true);
  SlotMeta slot1 = make_slot("prefetch-portrait", true);
  SlotMeta slot2 = make_slot("slot2", false);
  PortraitState portrait;
  std::string companion_url;
  int preload_slot = -1;
  bool preload_left_ready = true;
  bool preload_right_ready = true;

  bool handled = slideshow.on_companion_found(
      "https://example.test/companion", portrait, companion_url, 0, 0,
      slot0, slot1, slot2, preload_slot, preload_left_ready, preload_right_ready);
  assert(handled);
  assert(portrait.companion_found);
  assert(portrait.left_requested);
  assert(companion_url == "https://example.test/companion");
  assert(slot0.companion_url == companion_url);
  SlideshowCommand cmd;
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_START_PORTRAIT_LEFT);
  assert(cmd.slot == 0);

  portrait = PortraitState{};
  companion_url = "";
  handled = slideshow.on_companion_found(
      "https://example.test/preload-companion", portrait, companion_url, 1, 0,
      slot0, slot1, slot2, preload_slot, preload_left_ready, preload_right_ready);
  assert(handled);
  assert(preload_slot == 1);
  assert(!preload_left_ready);
  assert(!preload_right_ready);
  assert(slot1.companion_url == "https://example.test/preload-companion");
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_START_PRELOAD_LEFT);
  assert(cmd.slot == 1);

  bool displayed = false;
  portrait.workflow_busy = true;
  slideshow.handle_companion_not_found(
      portrait, companion_url, 0, 0, slot0, slot1, slot2, displayed, false);
  assert(!portrait.companion_found);
  assert(portrait.no_companion_active);
  assert(!portrait.workflow_busy);
  assert(displayed);
  assert(slot0.companion_url.empty());
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_DISPLAY_CURRENT);
}

static void test_slideshow_component_display_current_flow() {
  EspFrameSlideshow slideshow;
  SlotMeta slot0 = make_slot("landscape", false);
  SlotMeta slot1 = make_slot("portrait", true);
  SlotMeta slot2 = make_slot("slot2", false);
  PortraitState portrait;
  bool displayed = false;

  DisplayMeta current;
  bool pair = slideshow.begin_display_current(0, slot0, slot1, slot2, portrait, true, displayed, current);
  assert(!pair);
  assert(displayed);
  assert(!portrait.workflow_busy);

  displayed = false;
  portrait = PortraitState{};
  portrait.workflow_busy = true;
  int preload_slot = 1;
  pair = slideshow.begin_display_current(1, slot0, slot1, slot2, portrait, true, displayed, current);
  assert(pair);
  assert(!displayed);
  slideshow.after_display_current(1, slot0, slot1, slot2, portrait, true, displayed,
                                  preload_slot, true, true);
  assert(!displayed);
  assert(portrait.is_pair);
  assert(portrait.using_preload);
  assert(preload_slot == 1);
  SlideshowCommand cmd;
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_DISPLAY_PRELOADED_PAIR);

  bool preload_left = true;
  bool preload_right = true;
  bool preload_in_flight = true;
  int noncritical_count = 1;
  bool cleared = slideshow.clear_preload_for_slot(
      1, preload_slot, preload_left, preload_right, preload_in_flight, noncritical_count);
  assert(cleared);
  assert(preload_slot == -1);
  assert(!preload_left);
  assert(!preload_right);
  assert(!preload_in_flight);
  assert(noncritical_count == 0);
}

static void test_slideshow_component_paired_only_flow() {
  EspFrameSlideshow slideshow;
  auto &state = slideshow.state();
  state.active_slot = 1;
  state.active_slot_displayed = false;
  state.current_display.asset_id = "last-visible";
  state.current_display.valid = true;
  state.slot1 = make_slot("unpaired-active", true);
  state.slot1.ready = true;
  state.portrait.workflow_busy = true;
  state.companion_target_slot = 1;
  state.portrait_preload_slot = 1;
  state.portrait_preload_left_ready = true;
  state.portrait_preload_right_ready = false;
  state.preload_noncritical_in_flight = true;
  state.noncritical_remote_updates_in_flight = 1;
  state.target_slot = 2;
  state.slot_flags.fetch_in_flight[2] = true;

  slideshow.reject_portrait_slot(5000, 1);
  assert(!state.slot1.ready);
  assert(state.slot1.companion_url.empty());
  assert(!state.active_slot_displayed);
  assert(state.current_display.asset_id == "last-visible");
  assert(!state.portrait.workflow_busy);
  assert(!slideshow.portrait_search_response_is_current());
  assert(state.portrait_preload_slot == -1);
  assert(!state.preload_noncritical_in_flight);
  assert(state.noncritical_remote_updates_in_flight == 0);
  // Rejecting a portrait must not retarget the unrelated request already in
  // flight. The rejected slot is claimed by the delayed serialized fetch.
  assert(state.target_slot == 2);
  assert(state.rejected_fetch_target == 1);
  assert(state.slot_flags.fetch_in_flight[2]);
  assert(state.last_advance_ms == 5000);
  SlideshowCommand cmd;
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_REFETCH_REJECTED_SLOT);
  assert(cmd.slot == 1);
  assert(cmd.delay_ms == 1200);

  state.slot0 = make_slot("unpaired-prefetch", true);
  state.slot0.ready = true;
  state.active_slot = 1;
  state.active_slot_displayed = true;
  slideshow.reject_portrait_slot(6000, 0);
  assert(!state.slot0.ready);
  assert(state.active_slot_displayed);
  assert(state.current_display.asset_id == "last-visible");
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_REFETCH_REJECTED_SLOT);
  assert(cmd.slot == 0);

  slideshow.clear_commands();
  PortraitState portrait;
  portrait.workflow_busy = true;
  std::string companion_url;
  bool displayed = false;
  SlotMeta slot0 = make_slot("unpaired", true);
  SlotMeta slot1 = make_slot("slot1", false);
  SlotMeta slot2 = make_slot("slot2", false);
  slideshow.handle_companion_not_found(
      portrait, companion_url, 0, 0, slot0, slot1, slot2, displayed, true);
  assert(!displayed);
  assert(!portrait.workflow_busy);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_REJECT_PORTRAIT_SLOT);
  assert(cmd.slot == 0);

  slideshow.clear_commands();
  portrait = PortraitState{};
  portrait.workflow_busy = true;
  std::string reason;
  slideshow.on_portrait_right_error(portrait, reason, displayed, true);
  assert(!displayed);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_LOG_DIAG);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_REJECT_PORTRAIT_SLOT);

  slideshow.clear_commands();
  int active_slot = 0;
  int target_slot = 0;
  uint32_t last_advance = 0;
  DisplayMeta current;
  DisplayMeta previous;
  SlotFlags flags;
  int noncritical_count = 0;
  portrait = PortraitState{};
  portrait.workflow_busy = true;
  slot0.ready = true;
  slideshow.advance_forward(
      7000, false, active_slot, target_slot, displayed, last_advance,
      slot0, slot1, slot2, current, previous, portrait, flags, noncritical_count,
      true, true, -1, false, false, reason);
  assert(!displayed);
  assert(!portrait.workflow_busy);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_REJECT_PORTRAIT_SLOT);
  assert(cmd.slot == 0);

  state.active_slot = 2;
  state.slot2 = make_slot("complete-pair", true);
  state.portrait.workflow_busy = true;
  slideshow.mark_portrait_pair_displayed(false);
  assert(state.active_slot_displayed);
  assert(state.current_display.asset_id == "complete-pair");
  assert(state.portrait.is_pair);
  assert(!state.portrait.workflow_busy);

  slideshow.clear_commands();
  state.active_slot = 0;
  state.active_slot_displayed = false;
  state.slot0 = make_slot("invalid-preload-fallback", true);
  state.portrait_preload_slot = 0;
  state.portrait_preload_left_ready = true;
  state.portrait_preload_right_ready = true;
  state.portrait.workflow_busy = true;
  slideshow.fallback_preloaded_pair_to_single(0);
  assert(state.portrait_preload_slot == -1);
  assert(!state.portrait_preload_left_ready);
  assert(!state.portrait_preload_right_ready);
  assert(state.portrait.no_companion_active);
  assert(state.active_slot_displayed);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_DISPLAY_CURRENT);
  assert(cmd.slot == 0);

  slideshow.clear_commands();
  state.companion_target_slot = 1;
  state.portrait_search_generation = 10;
  slideshow.mark_portrait_search_request_started();
  assert(slideshow.portrait_search_response_is_current());
  slideshow.reject_portrait_slot(8000, 1);
  assert(!slideshow.portrait_search_response_is_current());

  SlotMeta stale0 = make_slot("stale0", true);
  SlotMeta stale1 = make_slot("stale1", true);
  SlotMeta stale2 = make_slot("stale2", true);
  PortraitState stale_portrait;
  std::string stale_url;
  bool stale_displayed = false;
  bool stale_handled = slideshow.on_companion_found(
      "https://example.test/stale", stale_portrait, stale_url, -1, 0,
      stale0, stale1, stale2, state.portrait_preload_slot,
      state.portrait_preload_left_ready, state.portrait_preload_right_ready);
  assert(!stale_handled);
  assert(stale2.companion_url.empty());
  slideshow.clear_commands();
  slideshow.handle_companion_not_found(
      stale_portrait, stale_url, -1, 0, stale0, stale1, stale2,
      stale_displayed, true);
  assert(!slideshow.has_command());
  assert(stale2.companion_url.empty());
}

static void test_configuration_contract_capabilities() {
  using namespace esphome::espframe::contract;
  static_assert(CONTRACT_VERSION == 2);
  static_assert(API_VERSION == 1);
  static_assert(SETTING_COUNT == 47);
  static_assert(CONFIGURATION_FIELD_COUNT == 68);
  assert(std::string(CAPABILITIES_PATH) == "/espframe/api/v1/capabilities");
  assert(std::string(CONFIGURATION_PATH) == "/espframe/api/v1/configuration");
  const std::string capabilities(CAPABILITIES_JSON);
  assert(capabilities.find("\"contract_version\":2") != std::string::npos);
  assert(capabilities.find("\"backup_versions\":[1,2]") != std::string::npos);
  assert(capabilities.find("\"legacy_entity_api\":true") != std::string::npos);
  assert(capabilities.find("\"configuration_read\":true") != std::string::npos);
  assert(capabilities.find("\"configuration_write\":true") != std::string::npos);
  assert(capabilities.find("\"configuration_parameter\":\"configuration\"") != std::string::npos);
}

int main() {
  test_date_and_url_helpers();
  test_duration_helpers();
  test_p4_jpeg_accelerator_helpers();
  test_immich_body_helpers();
  test_smart_filter_helpers();
  test_immich_request_state();
  test_slideshow_slot_actions();
  test_fetch_queue_and_error_handling();
  test_slideshow_component_commands();
  test_slideshow_component_prefetch_and_deferred_updates();
  test_slideshow_pipeline_watchdog();
  test_slideshow_component_portrait_flow();
  test_slideshow_component_preload_flow();
  test_slideshow_component_navigation_flow();
  test_slideshow_component_previous_flow();
  test_slideshow_component_companion_result_flow();
  test_slideshow_component_display_current_flow();
  test_slideshow_component_paired_only_flow();
  test_configuration_contract_capabilities();
  std::cout << "espframe helper tests passed\n";
  return 0;
}
