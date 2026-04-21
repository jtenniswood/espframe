#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

#include "components/espframe/date_utils.h"
#include "components/espframe/immich_helpers.h"

struct PhotoMeta {
  std::string asset_id, image_url, date, location, person;
  int year = 0, month = 0, day = 0;
  uint16_t zoom = ZOOM_IDENTITY;
};

struct SlotMeta : PhotoMeta {
  std::string datetime, companion_url, pending_asset_id;
  bool ready = false, is_portrait = false;
};

struct DisplayMeta : PhotoMeta {
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
}

inline void copy_display_to_slot(const DisplayMeta &disp, SlotMeta &slot) {
  static_cast<PhotoMeta &>(slot) = static_cast<const PhotoMeta &>(disp);
}

#include "components/espframe/slideshow_controller.h"
#include "components/espframe/slideshow_component.h"

static void test_date_and_url_helpers() {
  assert(normalize_immich_base_url(" immich.local:2283/") == "http://immich.local:2283");
  assert(normalize_immich_base_url("photos.example.com") == "https://photos.example.com");
  assert(normalize_immich_base_url("photos.example.com:443/") == "https://photos.example.com:443");
  assert(normalize_immich_base_url("//photos.example.com/") == "https://photos.example.com");
  assert(normalize_immich_base_url("HTTPS://photos.example.com///") == "https://photos.example.com");
  assert(format_photo_age(2026, 4, 21, 2026, 4, 21) == "today");
  assert(format_photo_age(2026, 4, 1, 2026, 4, 21) == "20 days ago");
  assert(format_photo_date_full(2026, 4, 21) == "21 April 2026");
}

static void test_immich_body_helpers() {
  ImmichDateRange range = resolve_immich_date_filter(
      true, "Relative Range", 1, "Months", true, 2026, 3, 31, "", "");
  assert(range.from == "2026-02-28");
  assert(range.to == "2026-03-31");
  assert(!range.relative_skipped_for_invalid_time);
  assert(build_immich_date_filter_extra(range) ==
         "\"takenAfter\":\"2026-02-28T00:00:00.000Z\","
         "\"takenBefore\":\"2026-03-31T23:59:59.999Z\"");

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

  assert(build_uuid_json_array(" a, b ,, c ") == "[\"a\",\"b\",\"c\"]");
  assert(build_immich_search_body(1, true, "Favorites", "", "").find("\"isFavorite\":true") !=
         std::string::npos);
  assert(build_immich_search_body(1, false, "Person", "", "p1,p2").find("\"personIds\":[\"p1\"]") !=
         std::string::npos);
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
  assert(current.asset_id == "portrait-active");

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

  int noncritical_count = 0;
  bool update = slideshow.request_deferred_slot_update(1, 0, flags, false, noncritical_count);
  assert(update);
  assert(noncritical_count == 1);
  assert(flags.noncritical_update[1]);
  assert(flags.fetch_in_flight[1]);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_UPDATE_SLOT_IMAGE);
  assert(cmd.slot == 1);

  clear_slot_fetch_in_flight(1, flags);
  clear_noncritical(1, flags, noncritical_count);
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

  bool started = slideshow.start_active_portrait(
      0, slot0, slot1, slot2, portrait, displayed, primary_id, companion_url,
      search_datetime, companion_slot);
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
      search_datetime, companion_slot);
  assert(search_started);
  assert(searching.workflow_busy);
  assert(!searching.companion_found);
  assert(companion_url.empty());
  assert(search_datetime == no_companion.datetime);
  assert(companion_slot == 0);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_DEFER_COMPANION_SEARCH);

  std::string reason;
  slideshow.on_portrait_left_error(searching, reason, displayed);
  assert(reason == "portrait left error");
  assert(displayed);
  assert(!searching.workflow_busy);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_LOG_DIAG);
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_DISPLAY_CURRENT);
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
                             noncritical_count, true, -1, false, false, reason);
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
                             noncritical_count, true, -1, false, false, reason);
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
                             noncritical_count, true, -1, false, false, reason);
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
  SlotMeta slot1 = make_slot("slot1", false);
  SlotMeta slot2 = make_slot("slot2", false);
  DisplayMeta current;
  copy_slot_to_display(slot0, current);
  DisplayMeta previous;
  previous.asset_id = "previous";
  previous.image_url = "https://example.test/previous";
  previous.valid = true;
  PortraitState portrait;
  SlotFlags flags;
  int active_slot = 0;
  bool displayed = true;

  bool shown = slideshow.show_previous(1234, active_slot, displayed, slot0, slot1, slot2,
                                       current, previous, portrait, flags);
  assert(shown);
  assert(active_slot == 2);
  assert(!displayed);
  assert(current.asset_id == "previous");
  assert(slot2.pending_asset_id == "previous");
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
      portrait, companion_url, 0, 0, slot0, slot1, slot2, displayed);
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

  bool pair = slideshow.begin_display_current(0, slot0, slot1, slot2, portrait, true, displayed);
  assert(!pair);
  assert(displayed);
  assert(!portrait.workflow_busy);

  displayed = false;
  portrait = PortraitState{};
  portrait.workflow_busy = true;
  int preload_slot = 1;
  pair = slideshow.begin_display_current(1, slot0, slot1, slot2, portrait, true, displayed);
  assert(pair);
  assert(!displayed);
  slideshow.after_display_current(1, slot0, slot1, slot2, portrait, true, displayed,
                                  preload_slot, true, true);
  assert(displayed);
  assert(portrait.is_pair);
  assert(portrait.using_preload);
  assert(preload_slot == -1);
  SlideshowCommand cmd;
  assert(slideshow.pop_command(cmd));
  assert(cmd.kind == SLIDESHOW_COMMAND_DISPLAY_PRELOADED_PAIR);

  bool preload_left = true;
  bool preload_right = true;
  bool preload_in_flight = true;
  int noncritical_count = 1;
  bool cleared = slideshow.clear_preload_for_slot(
      1, preload_slot, preload_left, preload_right, preload_in_flight, noncritical_count);
  assert(!cleared);
  preload_slot = 1;
  cleared = slideshow.clear_preload_for_slot(
      1, preload_slot, preload_left, preload_right, preload_in_flight, noncritical_count);
  assert(cleared);
  assert(preload_slot == -1);
  assert(!preload_left);
  assert(!preload_right);
  assert(!preload_in_flight);
  assert(noncritical_count == 0);
}

int main() {
  test_date_and_url_helpers();
  test_immich_body_helpers();
  test_slideshow_slot_actions();
  test_fetch_queue_and_error_handling();
  test_slideshow_component_commands();
  test_slideshow_component_prefetch_and_deferred_updates();
  test_slideshow_component_portrait_flow();
  test_slideshow_component_preload_flow();
  test_slideshow_component_navigation_flow();
  test_slideshow_component_previous_flow();
  test_slideshow_component_companion_result_flow();
  test_slideshow_component_display_current_flow();
  std::cout << "espframe helper tests passed\n";
  return 0;
}
