#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Included by espframe_helpers.h after the slot/display state structs and
// small slot helper functions are declared.

enum SlideshowAction : uint8_t {
  SLIDESHOW_ACTION_NONE = 0,
  SLIDESHOW_ACTION_DISPLAY_CURRENT = 1,
  SLIDESHOW_ACTION_START_ACTIVE_PAIR = 2,
  SLIDESHOW_ACTION_FETCH_COMPANION = 3,
  SLIDESHOW_ACTION_PREFETCH = 4,
};

enum FetchJobKind : uint8_t {
  FETCH_JOB_SLOT = 0,
  FETCH_JOB_PORTRAIT_LEFT = 1,
  FETCH_JOB_PORTRAIT_RIGHT = 2,
  FETCH_JOB_COMPANION = 3,
};

struct SlotState {
  int slot = -1;
  bool ready = false;
  bool in_flight = false;
  bool active = false;
  bool portrait = false;
};

struct PortraitPairState {
  bool pair = false;
  bool workflow_busy = false;
  bool preload_ready = false;
};

struct FetchJob {
  FetchJobKind kind = FETCH_JOB_SLOT;
  int slot = -1;
  uint8_t priority = 0;
  uint32_t queued_ms = 0;
};

class FetchQueue {
 public:
  static constexpr size_t CAPACITY = 6;

  void clear() { this->count_ = 0; }
  size_t size() const { return this->count_; }
  bool empty() const { return this->count_ == 0; }

  bool contains(FetchJobKind kind, int slot) const {
    for (size_t i = 0; i < this->count_; i++) {
      if (this->jobs_[i].kind == kind && this->jobs_[i].slot == slot) return true;
    }
    return false;
  }

  bool enqueue(FetchJobKind kind, int slot, uint8_t priority, uint32_t now_ms) {
    if (this->contains(kind, slot)) return true;
    if (this->count_ >= CAPACITY) return false;
    FetchJob job;
    job.kind = kind;
    job.slot = slot;
    job.priority = priority;
    job.queued_ms = now_ms;
    this->jobs_[this->count_++] = job;
    return true;
  }

  bool pop(FetchJob &out) {
    if (this->count_ == 0) return false;
    size_t best = 0;
    for (size_t i = 1; i < this->count_; i++) {
      if (this->jobs_[i].priority > this->jobs_[best].priority) best = i;
    }
    out = this->jobs_[best];
    for (size_t i = best + 1; i < this->count_; i++) {
      this->jobs_[i - 1] = this->jobs_[i];
    }
    this->count_--;
    return true;
  }

 private:
  FetchJob jobs_[CAPACITY]{};
  size_t count_ = 0;
};

class SlideshowController {
 public:
  static SlideshowAction handle_slot_download_finished(int slot, SlotMeta &meta,
                                                       SlotFlags &flags,
                                                       int &noncritical_count,
                                                       int &download_retries,
                                                       int active_slot,
                                                       bool portrait_pairing_enabled,
                                                       bool &active_slot_displayed,
                                                       DisplayMeta &current_display,
                                                       PortraitState &portrait,
                                                       int &companion_target_slot,
                                                       int portrait_preload_slot,
                                                       std::string &portrait_search_datetime,
                                                       std::string &portrait_primary_asset_id) {
    if (!handle_slot_download_complete(slot, meta, flags, noncritical_count, download_retries))
      return SLIDESHOW_ACTION_NONE;

    bool is_active = active_slot == slot;
    bool pair = meta.is_portrait && portrait_pairing_enabled;
    if (is_active) {
      copy_slot_to_display(meta, current_display);
      if (!pair) active_slot_displayed = true;
    }

    if (is_active && !pair) {
      return SLIDESHOW_ACTION_DISPLAY_CURRENT;
    }
    if (is_active && pair) {
      if (!(active_slot_displayed && portrait.is_pair)) {
        return SLIDESHOW_ACTION_START_ACTIVE_PAIR;
      }
      return SLIDESHOW_ACTION_NONE;
    }
    if (!is_active && pair) {
      if (companion_target_slot == slot || portrait_preload_slot == slot) {
        return SLIDESHOW_ACTION_NONE;
      }
      companion_target_slot = slot;
      portrait_search_datetime = meta.datetime;
      portrait_primary_asset_id = meta.asset_id;
      return SLIDESHOW_ACTION_FETCH_COMPANION;
    }
    return SLIDESHOW_ACTION_PREFETCH;
  }

  static void handle_slot_download_error(int slot, SlotFlags &flags,
                                         int &noncritical_count,
                                         std::string &diag_reason,
                                         int &last_downloaded_slot,
                                         const char *reason) {
    clear_slot_fetch_in_flight(slot, flags);
    clear_noncritical(slot, flags, noncritical_count);
    diag_reason = reason;
    last_downloaded_slot = slot;
  }

  static bool enqueue_prefetch_slots(FetchQueue &queue, int active_slot,
                                     const SlotMeta &slot0,
                                     const SlotMeta &slot1,
                                     const SlotMeta &slot2,
                                     const SlotFlags &flags,
                                     uint32_t now_ms) {
    queue.clear();
    int next1 = (active_slot + 1) % 3;
    int next2 = (active_slot + 2) % 3;
    const SlotMeta &n1 = get_slot_const_(next1, slot0, slot1, slot2);
    const SlotMeta &n2 = get_slot_const_(next2, slot0, slot1, slot2);
    if (!n1.ready && !flags.fetch_in_flight[next1]) {
      queue.enqueue(FETCH_JOB_SLOT, next1, 20, now_ms);
    }
    if (!n2.ready && !flags.fetch_in_flight[next2]) {
      queue.enqueue(FETCH_JOB_SLOT, next2, 10, now_ms);
    }
    return !queue.empty();
  }

 private:
  static const SlotMeta &get_slot_const_(int s, const SlotMeta &s0,
                                         const SlotMeta &s1,
                                         const SlotMeta &s2) {
    return (s == 0) ? s0 : (s == 1) ? s1 : s2;
  }
};
