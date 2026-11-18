#pragma once

#include "slideshow_model.h"
#include "slideshow_controller.h"

#include <cstddef>
#include <cstdint>
#include <string>

enum SlideshowCommandKind : uint8_t {
  SLIDESHOW_COMMAND_NONE = 0,
  SLIDESHOW_COMMAND_DISPLAY_CURRENT = 1,
  SLIDESHOW_COMMAND_START_ACTIVE_PAIR = 2,
  SLIDESHOW_COMMAND_FETCH_COMPANION = 3,
  SLIDESHOW_COMMAND_PREFETCH_AFTER_DELAY = 4,
  SLIDESHOW_COMMAND_LOG_DIAG = 5,
  SLIDESHOW_COMMAND_HANDLE_SLOT_DOWNLOAD_ERROR = 6,
  SLIDESHOW_COMMAND_FETCH_INTO_SLOT = 7,
  SLIDESHOW_COMMAND_UPDATE_SLOT_IMAGE = 8,
  SLIDESHOW_COMMAND_UPDATE_PORTRAIT_LEFT = 9,
  SLIDESHOW_COMMAND_UPDATE_PORTRAIT_RIGHT = 10,
  SLIDESHOW_COMMAND_UPDATE_PRELOAD_LEFT = 11,
  SLIDESHOW_COMMAND_UPDATE_PRELOAD_RIGHT = 12,
  SLIDESHOW_COMMAND_START_PORTRAIT_LEFT = 13,
  SLIDESHOW_COMMAND_START_PORTRAIT_RIGHT = 14,
  SLIDESHOW_COMMAND_DISPLAY_PORTRAIT_PAIR = 15,
  SLIDESHOW_COMMAND_START_PRELOAD_LEFT = 16,
  SLIDESHOW_COMMAND_START_PRELOAD_RIGHT = 17,
  SLIDESHOW_COMMAND_DEFER_COMPANION_SEARCH = 18,
  SLIDESHOW_COMMAND_ABORT_SLOT_DOWNLOAD = 19,
  SLIDESHOW_COMMAND_DEFER_FETCH_INTO_SLOT = 20,
  SLIDESHOW_COMMAND_LOAD_PREVIOUS_SLOT = 21,
  SLIDESHOW_COMMAND_LOG_NO_PREVIOUS = 22,
  SLIDESHOW_COMMAND_DISPLAY_PRELOADED_PAIR = 23,
  SLIDESHOW_COMMAND_REJECT_PORTRAIT_SLOT = 24,
  SLIDESHOW_COMMAND_REFETCH_REJECTED_SLOT = 25,
};

struct SlideshowCommand {
  SlideshowCommandKind kind = SLIDESHOW_COMMAND_NONE;
  // Ring-buffer slots are -1 (none), 0, 1 or 2.
  int8_t slot = -1;
  uint32_t delay_ms = 0;
};

static_assert(sizeof(SlideshowCommand) == 8, "Keep the command queue compact");

class SlideshowCommandQueue {
 public:
  static constexpr size_t CAPACITY = 12;

  void clear() {
    this->head_ = 0;
    this->count_ = 0;
  }

  bool empty() const { return this->count_ == 0; }
  size_t size() const { return this->count_; }

  bool push(SlideshowCommandKind kind, int slot = -1, uint32_t delay_ms = 0) {
    if (kind == SLIDESHOW_COMMAND_NONE) return true;
    if (this->count_ >= CAPACITY) return false;
    size_t idx = (this->head_ + this->count_) % CAPACITY;
    this->commands_[idx].kind = kind;
    this->commands_[idx].slot = slot;
    this->commands_[idx].delay_ms = delay_ms;
    this->count_++;
    return true;
  }

  bool pop(SlideshowCommand &out) {
    if (this->count_ == 0) return false;
    out = this->commands_[this->head_];
    this->commands_[this->head_] = SlideshowCommand{};
    this->head_ = (this->head_ + 1) % CAPACITY;
    this->count_--;
    return true;
  }

 private:
  SlideshowCommand commands_[CAPACITY]{};
  size_t head_ = 0;
  size_t count_ = 0;
};

// Complete mutable state for the slideshow pipeline. The ESPHome YAML layer
// triggers hardware actions, while this model owns navigation, slot, portrait,
// preload, retry, and diagnostic data as one resettable unit.
struct SlideshowRuntimeState {
  SlotMeta slot0;
  SlotMeta slot1;
  SlotMeta slot2;
  DisplayMeta current_display;
  DisplayMeta previous_display;

  int active_slot = 0;
  int target_slot = 0;
  bool active_slot_displayed = false;
  uint32_t last_advance_ms = 0;
  uint32_t last_prefetch_start_ms = 0;
  uint32_t pipeline_blocked_since_ms = 0;
  bool pipeline_blocked_tracking = false;
  uint32_t last_short_tap_ms = 0;
  int last_downloaded_slot = -1;
  std::string diagnostic_reason;
  uint32_t last_diagnostic_ms = 0;
  int deferred_fetch_target = -1;
  int rejected_fetch_target = -1;
  int download_retries = 0;

  SlotFlags slot_flags;
  FetchQueue fetch_queue;
  bool preload_noncritical_in_flight = false;
  int noncritical_remote_updates_in_flight = 0;

  PortraitState portrait;
  std::string portrait_search_datetime;
  std::string portrait_primary_asset_id;
  int companion_target_slot = 0;
  std::string portrait_companion_url;
  int portrait_preload_slot = -1;
  bool portrait_preload_left_ready = false;
  bool portrait_preload_right_ready = false;
  bool portrait_search_expanded = false;
  bool portrait_search_exhaustive = false;
  uint32_t portrait_search_page = 1;
  std::string portrait_search_cursor;
  uint32_t portrait_search_generation = 0;
  uint32_t portrait_search_request_generation = 0;

  // Continue the current date window before widening it. Cursors are opaque;
  // only structured metadata requests use them, and each window starts fresh.
  bool advance_portrait_search(uint32_t next_page, const std::string &next_cursor,
                               bool structured, int range_days) {
    if (portrait_search_exhaustive) {
      if (structured && !next_cursor.empty() && next_cursor != portrait_search_cursor) {
        portrait_search_cursor = next_cursor;
        return true;
      }
      if (!structured && next_page > portrait_search_page) {
        portrait_search_page = next_page;
        return true;
      }
    } else {
      portrait_search_exhaustive = true;
      portrait_search_page = 1;
      portrait_search_cursor.clear();
      return true;
    }
    if (!portrait_search_expanded && range_days > 0) {
      portrait_search_expanded = true;
      portrait_search_page = 1;
      portrait_search_cursor.clear();
      return true;
    }
    return false;
  }

  void reset() { *this = SlideshowRuntimeState{}; }

  SlotMeta &slot(int index) {
    return index == 0 ? this->slot0 : (index == 1 ? this->slot1 : this->slot2);
  }

  const SlotMeta &slot(int index) const {
    return index == 0 ? this->slot0 : (index == 1 ? this->slot1 : this->slot2);
  }
};

class EspFrameSlideshow {
 public:
  static constexpr uint32_t PIPELINE_STALL_TIMEOUT_MS = 90000;

  SlideshowRuntimeState &state() { return this->state_; }
  const SlideshowRuntimeState &state() const { return this->state_; }
  void reset_state() {
    this->state_.reset();
    this->commands_.clear();
  }

  // Hardware downloads must be aborted before discarding their tracking.
  void invalidate_filter_slots() {
    auto &state = this->state_;
    this->commands_.clear();
    state.fetch_queue.clear();
    state.deferred_fetch_target = -1;
    state.rejected_fetch_target = -1;
    for (int slot = 0; slot < 3; ++slot) {
      clear_slot_fetch_in_flight(slot, state.slot_flags);
      state.slot_flags.noncritical_update[slot] = false;
      state.slot(slot).ready = false;
      state.slot(slot).pending_asset_id.clear();
    }
    state.portrait.left_requested = false;
    state.portrait.right_requested = false;
    state.portrait.workflow_busy = false;
    state.companion_target_slot = -1;
    state.portrait_search_generation++;
    state.portrait_preload_slot = -1;
    state.portrait_preload_left_ready = false;
    state.portrait_preload_right_ready = false;
    state.preload_noncritical_in_flight = false;
    state.noncritical_remote_updates_in_flight = 0;
    // Backward navigation must not restore a photo selected by the old
    // filter after this slot generation is discarded.
    this->state_.previous_display = DisplayMeta{};
    this->state_.active_slot_displayed = false;
    this->state_.target_slot = this->state_.active_slot;
  }

  void capture_deferred_fetch_target() {
    this->state_.deferred_fetch_target = this->state_.target_slot;
  }

  bool claim_deferred_fetch(bool rejected) {
    auto &state = this->state_;
    int &pending_slot = rejected ? state.rejected_fetch_target : state.deferred_fetch_target;
    if (pending_slot < 0 || pending_slot > 2) return false;
    state.target_slot = pending_slot;
    pending_slot = -1;
    return true;
  }

  bool has_command() const { return !this->commands_.empty(); }
  size_t command_count() const { return this->commands_.size(); }
  void clear_commands() { this->commands_.clear(); }
  bool pop_command(SlideshowCommand &out) { return this->commands_.pop(out); }

  bool emit_command(SlideshowCommandKind kind, int slot = -1, uint32_t delay_ms = 0) {
    return this->commands_.push(kind, slot, delay_ms);
  }

  bool emit_action(SlideshowAction action, int slot = -1) {
    switch (action) {
      case SLIDESHOW_ACTION_DISPLAY_CURRENT:
        return this->emit_command(SLIDESHOW_COMMAND_DISPLAY_CURRENT, slot);
      case SLIDESHOW_ACTION_START_ACTIVE_PAIR:
        return this->emit_command(SLIDESHOW_COMMAND_START_ACTIVE_PAIR, slot);
      case SLIDESHOW_ACTION_FETCH_COMPANION:
        return this->emit_command(SLIDESHOW_COMMAND_FETCH_COMPANION, slot);
      case SLIDESHOW_ACTION_PREFETCH:
        return this->emit_command(SLIDESHOW_COMMAND_PREFETCH_AFTER_DELAY, slot, 500);
      case SLIDESHOW_ACTION_NONE:
      default:
        return true;
    }
  }

  SlideshowAction on_slot_download_finished(int slot, bool portrait_pairing_enabled) {
    if (slot < 0 || slot > 2) return SLIDESHOW_ACTION_NONE;
    auto &state = this->state_;
    if (state.slot(slot).pending_asset_id.empty()) return SLIDESHOW_ACTION_NONE;
    SlideshowAction action = SlideshowController::handle_slot_download_finished(
        slot, state.slot(slot), state.slot_flags, state.noncritical_remote_updates_in_flight,
        state.download_retries, state.active_slot, portrait_pairing_enabled, state.active_slot_displayed,
        state.current_display, state.portrait, state.companion_target_slot, state.portrait_preload_slot,
        state.portrait_search_datetime, state.portrait_primary_asset_id);
    if (action == SLIDESHOW_ACTION_FETCH_COMPANION) {
      this->state_.portrait_search_expanded = false;
      this->state_.portrait_search_exhaustive = false;
      this->state_.portrait_search_page = 1;
      this->state_.portrait_search_cursor.clear();
      this->state_.portrait_search_generation++;
    }
    this->emit_action(action, slot);
    return action;
  }

  void on_slot_download_error(int slot, const char *reason) {
    if (slot < 0 || slot > 2) return;
    auto &state = this->state_;
    if (state.slot(slot).pending_asset_id.empty()) return;
    SlideshowController::handle_slot_download_error(
        slot, state.slot_flags, state.noncritical_remote_updates_in_flight,
        state.diagnostic_reason, state.last_downloaded_slot, reason);
    this->emit_command(SLIDESHOW_COMMAND_LOG_DIAG, slot);
    this->emit_command(SLIDESHOW_COMMAND_HANDLE_SLOT_DOWNLOAD_ERROR, slot);
  }

  struct SlotDownloadRecovery {
    int slot;
    int attempt;
    bool retry;
  };

  SlotDownloadRecovery recover_slot_download(int max_attempts) {
    auto &state = this->state_;
    const int slot = state.last_downloaded_slot;
    const int attempt = ++state.download_retries;
    auto &meta = state.slot(slot);
    meta.ready = false;
    meta.asset_id.clear();
    meta.pending_asset_id.clear();
    meta.image_url.clear();
    const bool retry = attempt < max_attempts;
    if (!retry) state.download_retries = 0;
    return {slot, attempt, retry};
  }

  bool start_active_portrait() {
    auto &state = this->state_;
    auto &portrait = state.portrait;
    const int active_slot = state.active_slot;
    if (state.active_slot_displayed && portrait.is_pair && portrait.using_preload) return false;
    if (state.portrait_preload_slot == active_slot &&
        state.preload_noncritical_in_flight) {
      portrait.workflow_busy = true;
      return false;
    }

    SlotMeta &meta = state.slot(active_slot);
    if (portrait.workflow_busy && state.portrait_primary_asset_id == meta.asset_id) return false;

    portrait.left_ready = false;
    portrait.right_ready = false;
    portrait.no_companion_active = false;
    portrait.left_requested = false;
    portrait.right_requested = false;
    portrait.using_preload = false;
    portrait.workflow_busy = true;
    state.portrait_primary_asset_id = meta.asset_id;

    if (!meta.companion_url.empty()) {
      portrait.companion_found = true;
      state.portrait_companion_url = meta.companion_url;
      portrait.left_requested = true;
      this->emit_command(SLIDESHOW_COMMAND_START_PORTRAIT_LEFT, active_slot);
      return true;
    }

    portrait.companion_found = false;
    state.portrait_companion_url = "";
    state.portrait_search_datetime = meta.datetime;
    state.companion_target_slot = active_slot;
    state.portrait_search_expanded = false;
    state.portrait_search_exhaustive = false;
    state.portrait_search_page = 1;
    state.portrait_search_cursor.clear();
    state.portrait_search_generation++;
    this->emit_command(SLIDESHOW_COMMAND_DEFER_COMPANION_SEARCH, active_slot, 200);
    return true;
  }

  enum class PortraitSide { LEFT, RIGHT };

  void on_portrait_download_finished(PortraitSide side) {
    auto &portrait = this->state_.portrait;
    if (side == PortraitSide::LEFT) {
      if (!portrait.left_requested) return;
      portrait.left_ready = true;
      portrait.left_requested = false;
      if (portrait.companion_found && !portrait.right_ready && !portrait.right_requested) {
        portrait.right_requested = true;
        this->emit_command(SLIDESHOW_COMMAND_START_PORTRAIT_RIGHT);
      }
    } else {
      if (!portrait.right_requested) return;
      portrait.right_ready = true;
      portrait.right_requested = false;
    }
    if (portrait.left_ready && portrait.right_ready) {
      this->emit_command(SLIDESHOW_COMMAND_DISPLAY_PORTRAIT_PAIR);
    }
  }

  void on_portrait_download_error(PortraitSide side, bool pairs_only) {
    auto &state = this->state_;
    auto &portrait = state.portrait;
    if (side == PortraitSide::LEFT) {
      if (!portrait.left_requested) return;
      portrait.left_ready = false;
      portrait.left_requested = false;
      state.diagnostic_reason = "portrait left error";
    } else {
      if (!portrait.right_requested) return;
      portrait.right_ready = false;
      portrait.right_requested = false;
      state.diagnostic_reason = "portrait right error";
    }
    portrait.companion_found = false;
    portrait.no_companion_active = true;
    portrait.workflow_busy = false;
    this->emit_command(SLIDESHOW_COMMAND_LOG_DIAG);
    if (state.active_slot_displayed) return;
    if (pairs_only) {
      this->emit_command(SLIDESHOW_COMMAND_REJECT_PORTRAIT_SLOT, state.active_slot);
    } else {
      state.active_slot_displayed = true;
      this->emit_command(SLIDESHOW_COMMAND_DISPLAY_CURRENT);
    }
  }

  void on_preload_left_finished() {
    auto &state = this->state_;
    const int slot = state.portrait_preload_slot;
    if (slot < 0 || slot > 2) return;
    state.portrait_preload_left_ready = true;
    if (!state.slot(slot).companion_url.empty()) {
      this->emit_command(SLIDESHOW_COMMAND_START_PRELOAD_RIGHT, slot);
      return;
    }
    state.portrait_preload_right_ready = false;
    this->clear_preload_noncritical_(state.preload_noncritical_in_flight,
                                     state.noncritical_remote_updates_in_flight);
  }

  void on_preload_right_finished() {
    auto &state = this->state_;
    if (state.portrait_preload_slot < 0 || !state.portrait_preload_left_ready) return;
    state.portrait_preload_right_ready = true;
    this->clear_preload_noncritical_(state.preload_noncritical_in_flight,
                                     state.noncritical_remote_updates_in_flight);
    this->display_completed_active_preload();
  }

  void advance_forward(uint32_t now_ms, bool retry_cooldown_active,
                       bool portrait_pairing_enabled, bool portrait_pairs_only) {
    auto &state = this->state_;
    auto &active_slot = state.active_slot;
    auto &portrait = state.portrait;
    auto &flags = state.slot_flags;
    if (!state.active_slot_displayed) {
      int slot = active_slot;
      SlotMeta &meta = state.slot(slot);
      if (meta.ready && portrait.workflow_busy) {
        if (meta.is_portrait && portrait_pairing_enabled) {
          if (portrait_pairs_only) {
            portrait.workflow_busy = false;
            this->emit_command(SLIDESHOW_COMMAND_REJECT_PORTRAIT_SLOT, slot);
          }
          // Keep the visible photo until the pair completes. A slideshow tick
          // must not expose the primary portrait before its companion arrives.
          return;
        }
        portrait.workflow_busy = false;
        portrait.left_ready = false;
        portrait.right_ready = false;
        portrait.right_requested = false;
        portrait.companion_found = false;
        state.active_slot_displayed = true;
        copy_slot_to_display(meta, state.current_display);
        this->emit_command(SLIDESHOW_COMMAND_DISPLAY_CURRENT, slot);
        return;
      }
      if (!meta.ready) {
        bool in_flight = flags.fetch_in_flight[slot];
        uint32_t fetch_age = slot_fetch_age_ms(slot, flags, now_ms);
        if (in_flight && fetch_age > 15000) {
          clear_noncritical(slot, flags, state.noncritical_remote_updates_in_flight);
          clear_slot_fetch_in_flight(slot, flags);
          state.diagnostic_reason = "h3 stuck slot";
          this->emit_command(SLIDESHOW_COMMAND_ABORT_SLOT_DOWNLOAD, slot);
          this->emit_command(SLIDESHOW_COMMAND_LOG_DIAG, slot);
          in_flight = false;
        }
        if (!in_flight && !any_slot_fetch_in_flight(flags) && !retry_cooldown_active) {
          portrait.workflow_busy = false;
          state.last_advance_ms = now_ms;
          state.target_slot = slot;
          this->emit_command(SLIDESHOW_COMMAND_DEFER_FETCH_INTO_SLOT, slot, 200);
        }
      }
      return;
    }

    if (!state.current_display.asset_id.empty()) {
      int old_slot = active_slot;
      SlotMeta &old_meta = state.slot(old_slot);
      state.previous_display = state.current_display;
      state.previous_display.zoom = old_meta.zoom;
      state.previous_display.valid = true;
    }

    portrait = PortraitState{};
    int old_slot = active_slot;
    active_slot = (old_slot + 1) % 3;
    state.active_slot_displayed = false;
    SlotMeta &cleared = state.slot(old_slot);
    cleared.ready = false;
    state.last_advance_ms = now_ms;

    SlotMeta &meta = state.slot(active_slot);
    if (meta.ready) {
      bool pair = meta.is_portrait && portrait_pairing_enabled;
      if (!pair) {
        copy_slot_to_display(meta, state.current_display);
        state.active_slot_displayed = true;
      }

      const bool preload_ready = pair && this->active_preloaded_pair_ready();
      if (!pair) {
        this->emit_command(SLIDESHOW_COMMAND_DISPLAY_CURRENT, active_slot);
      } else if (preload_ready) {
        this->emit_command(SLIDESHOW_COMMAND_DISPLAY_PRELOADED_PAIR, active_slot);
      } else {
        this->emit_command(SLIDESHOW_COMMAND_START_ACTIVE_PAIR, active_slot);
      }
      this->emit_command(SLIDESHOW_COMMAND_PREFETCH_AFTER_DELAY, active_slot, 500);
      return;
    }

    if (!flags.fetch_in_flight[active_slot]) {
      state.target_slot = active_slot;
      this->emit_command(SLIDESHOW_COMMAND_DEFER_FETCH_INTO_SLOT, active_slot, 200);
    }
  }

  bool show_previous(uint32_t now_ms) {
    auto &state = this->state_;
    if (!state.previous_display.valid) {
      this->emit_command(SLIDESHOW_COMMAND_LOG_NO_PREVIOUS);
      return false;
    }

    const int previous_slot = (state.active_slot + 2) % 3;
    // An API or image callback for this slot could overwrite the history that
    // is about to be restored. Let that refill finish before accepting back.
    if (state.slot_flags.fetch_in_flight[previous_slot]) return false;

    int current_slot = state.active_slot;
    SlotMeta &current_meta = state.slot(current_slot);
    DisplayMeta tmp = state.current_display;
    tmp.zoom = current_meta.zoom;

    state.current_display = state.previous_display;
    state.previous_display = tmp;

    state.portrait = PortraitState{};
    state.active_slot = previous_slot;
    state.active_slot_displayed = false;

    SlotMeta &meta = state.slot(previous_slot);
    meta.ready = false;
    copy_display_to_slot(state.current_display, meta);
    meta.pending_asset_id = state.current_display.asset_id;
    mark_slot_fetch_in_flight(previous_slot, state.slot_flags, now_ms);

    this->emit_command(SLIDESHOW_COMMAND_LOAD_PREVIOUS_SLOT, previous_slot);
    return true;
  }

  void handle_companion_not_found(bool portrait_pairs_only) {
    auto &state = this->state_;
    auto &portrait = state.portrait;
    const int companion_target_slot = state.companion_target_slot;
    if (companion_target_slot < 0 || companion_target_slot > 2) return;
    portrait.companion_found = false;
    state.portrait_companion_url = "";
    portrait.left_requested = false;
    portrait.right_requested = false;
    SlotMeta &meta = state.slot(companion_target_slot);
    meta.companion_url = "";
    if (portrait_pairs_only) {
      portrait.workflow_busy = false;
      this->emit_command(SLIDESHOW_COMMAND_REJECT_PORTRAIT_SLOT, companion_target_slot);
      return;
    }
    if (companion_target_slot == state.active_slot) {
      portrait.no_companion_active = true;
      portrait.workflow_busy = false;
      if (!state.active_slot_displayed) {
        state.active_slot_displayed = true;
        this->emit_command(SLIDESHOW_COMMAND_DISPLAY_CURRENT, state.active_slot);
      }
    }
  }

  bool on_companion_found(const std::string &companion_url) {
    auto &state = this->state_;
    auto &portrait = state.portrait;
    const int companion_target_slot = state.companion_target_slot;
    if (companion_url.empty() || !this->portrait_search_response_is_current()) return false;

    portrait.no_companion_active = false;
    portrait.companion_found = true;
    state.portrait_companion_url = companion_url;
    SlotMeta &meta = state.slot(companion_target_slot);
    meta.companion_url = companion_url;

    if (companion_target_slot == state.active_slot && !portrait.left_requested && !portrait.left_ready) {
      portrait.left_ready = false;
      portrait.right_ready = false;
      portrait.left_requested = true;
      portrait.right_requested = false;
      this->emit_command(SLIDESHOW_COMMAND_START_PORTRAIT_LEFT, companion_target_slot);
      return true;
    }

    if (companion_target_slot != state.active_slot) {
      state.portrait_preload_slot = companion_target_slot;
      state.portrait_preload_left_ready = false;
      state.portrait_preload_right_ready = false;
      this->emit_command(SLIDESHOW_COMMAND_START_PRELOAD_LEFT, companion_target_slot);
      return true;
    }

    return true;
  }

  // Never decode over pixels still referenced by the visible pair. Active
  // downloads can use the other bank; background preloads wait until navigation.
  bool prepare_portrait_download(bool preload, bool visible_buffer_bound) {
    if (!visible_buffer_bound) return true;
    auto &state = this->state_;
    if (preload) {
      this->clear_preload_for_slot(
          state.portrait_preload_slot, state.portrait_preload_slot,
          state.portrait_preload_left_ready, state.portrait_preload_right_ready,
          state.preload_noncritical_in_flight, state.noncritical_remote_updates_in_flight);
      return false;
    }
    state.portrait.left_requested = false;
    state.portrait.right_requested = false;
    state.portrait_preload_slot = state.active_slot;
    state.portrait_preload_left_ready = false;
    state.portrait_preload_right_ready = false;
    this->emit_command(SLIDESHOW_COMMAND_START_PRELOAD_LEFT, state.active_slot);
    return false;
  }

  bool active_preloaded_pair_ready() const {
    return this->state_.portrait_preload_slot == this->state_.active_slot &&
           this->state_.portrait_preload_left_ready && this->state_.portrait_preload_right_ready;
  }

  void display_completed_active_preload() {
    if (!this->state_.active_slot_displayed && this->active_preloaded_pair_ready())
      this->emit_command(SLIDESHOW_COMMAND_DISPLAY_PRELOADED_PAIR, this->state_.active_slot);
  }

  void recover_active_preloaded_pair(uint32_t now_ms, bool pairs_only) {
    const int slot = this->state_.active_slot;
    if (pairs_only) {
      this->reject_portrait_slot(now_ms, slot);
    } else {
      this->fallback_preloaded_pair_to_single(slot);
    }
  }

  // Called before any single-image widget or transition is changed. Queued
  // display commands can outlive the setting or pairing state that created them.
  bool prepare_single_display(bool portrait_pairing_enabled, bool portrait_pairs_only) {
    auto &state = this->state_;
    const auto &meta = state.slot(state.active_slot);
    if (!meta.is_portrait || !portrait_pairing_enabled) return true;
    if (state.portrait.no_companion_active) {
      if (!portrait_pairs_only) return true;
      this->emit_command(SLIDESHOW_COMMAND_REJECT_PORTRAIT_SLOT, state.active_slot);
      return false;
    }
    this->after_display_current(portrait_pairing_enabled);
    return false;
  }

  bool begin_display_current(bool portrait_pairing_enabled) {
    auto &state = this->state_;
    const auto &meta = state.slot(state.active_slot);
    copy_slot_to_display(meta, state.current_display);
    state.portrait.is_pair = false;
    const bool pair = meta.is_portrait && portrait_pairing_enabled;
    if (!pair) {
      state.active_slot_displayed = true;
      state.portrait.workflow_busy = false;
    }
    return pair;
  }

  void after_display_current(bool portrait_pairing_enabled) {
    auto &state = this->state_;
    const auto &meta = state.slot(state.active_slot);
    if (!meta.is_portrait || !portrait_pairing_enabled) return;

    if (this->active_preloaded_pair_ready()) {
      state.portrait.is_pair = true;
      state.portrait.using_preload = true;
      this->emit_command(SLIDESHOW_COMMAND_DISPLAY_PRELOADED_PAIR, state.active_slot);
      return;
    }

    if (!state.portrait.is_pair && !state.portrait.no_companion_active) {
      this->emit_command(SLIDESHOW_COMMAND_START_ACTIVE_PAIR, state.active_slot);
    }
  }

  bool clear_preload_for_slot(int slot, int &portrait_preload_slot,
                              bool &portrait_preload_left_ready,
                              bool &portrait_preload_right_ready,
                              bool &preload_noncritical_in_flight,
                              int &noncritical_count) {
    if (portrait_preload_slot != slot) return false;
    portrait_preload_slot = -1;
    portrait_preload_left_ready = false;
    portrait_preload_right_ready = false;
    this->clear_preload_noncritical_(preload_noncritical_in_flight, noncritical_count);
    return true;
  }

  void reject_portrait_slot(uint32_t now_ms, int slot) {
    if (slot < 0 || slot > 2) return;
    SlotMeta &meta = this->state_.slot(slot);
    meta.ready = false;
    meta.companion_url.clear();
    meta.pending_asset_id.clear();
    if (this->state_.portrait_preload_slot == slot) {
      this->state_.portrait_preload_slot = -1;
      this->state_.portrait_preload_left_ready = false;
      this->state_.portrait_preload_right_ready = false;
      this->clear_preload_noncritical_(this->state_.preload_noncritical_in_flight,
                                      this->state_.noncritical_remote_updates_in_flight);
    }
    if (slot == this->state_.active_slot) {
      this->state_.active_slot_displayed = false;
      this->state_.portrait = PortraitState{};
    }
    if (this->state_.companion_target_slot == slot) this->state_.companion_target_slot = -1;
    this->state_.portrait_companion_url.clear();
    // Keep target_slot bound to any request already in flight. The delayed
    // rejected-slot fetch claims target_slot only after the current request
    // has completed.
    this->state_.rejected_fetch_target = slot;
    this->state_.last_advance_ms = now_ms;
    this->state_.portrait_search_expanded = false;
    this->state_.portrait_search_exhaustive = false;
    this->state_.portrait_search_page = 1;
    this->state_.portrait_search_cursor.clear();
    this->state_.portrait_search_generation++;
    this->emit_command(SLIDESHOW_COMMAND_REFETCH_REJECTED_SLOT, slot, 1200);
  }

  void mark_portrait_search_request_started() {
    this->state_.portrait_search_request_generation =
        this->state_.portrait_search_generation;
  }

  bool portrait_search_response_is_current() const {
    return this->state_.companion_target_slot >= 0 &&
           this->state_.companion_target_slot <= 2 &&
           this->state_.portrait_search_request_generation ==
               this->state_.portrait_search_generation;
  }

  enum class LivePairPresentation { HIDE, WAIT, READY };

  LivePairPresentation prepare_live_portrait_pair(bool pairing_enabled) {
    auto &portrait = this->state_.portrait;
    if (!pairing_enabled) {
      portrait = PortraitState{};
      return LivePairPresentation::HIDE;
    }
    if (!portrait.left_ready || !portrait.right_ready) {
      this->state_.diagnostic_reason = "portrait pair deferred waiting both loaded";
      return LivePairPresentation::WAIT;
    }
    return LivePairPresentation::READY;
  }

  // Return true when the display adapter should present the single image.
  bool recover_live_portrait_pair(uint32_t now_ms, bool pairs_only) {
    auto &portrait = this->state_.portrait;
    portrait.left_ready = false;
    portrait.right_ready = false;
    portrait.is_pair = false;
    portrait.workflow_busy = false;
    if (pairs_only) {
      this->reject_portrait_slot(now_ms, this->state_.active_slot);
      return false;
    }
    this->state_.active_slot_displayed = true;
    return true;
  }

  void mark_portrait_pair_displayed(bool using_preload) {
    SlotMeta &meta = this->state_.slot(this->state_.active_slot);
    copy_slot_to_display(meta, this->state_.current_display);
    this->state_.portrait.is_pair = true;
    this->state_.portrait.using_preload = using_preload;
    // Both banks represent a complete active pair after presentation. Prefetch
    // uses these flags even when the downloads finished in the preload bank.
    this->state_.portrait.left_ready = true;
    this->state_.portrait.right_ready = true;
    this->state_.portrait.workflow_busy = false;
    this->state_.active_slot_displayed = true;
    if (using_preload) {
      this->state_.portrait_preload_slot = -1;
      this->state_.portrait_preload_left_ready = false;
      this->state_.portrait_preload_right_ready = false;
    }
  }

  void on_portrait_preload_error(PortraitSide side, uint32_t now_ms, bool pairs_only) {
    const int slot = this->state_.portrait_preload_slot;
    if (slot < 0 || slot > 2) return;
    if (side == PortraitSide::LEFT) {
      this->state_.portrait_preload_left_ready = false;
      this->state_.diagnostic_reason = "portrait preload left error";
    } else {
      this->state_.portrait_preload_right_ready = false;
      this->state_.diagnostic_reason = "portrait preload right error";
    }
    this->clear_preload_noncritical_(this->state_.preload_noncritical_in_flight,
                                     this->state_.noncritical_remote_updates_in_flight);
    this->emit_command(SLIDESHOW_COMMAND_LOG_DIAG);
    if (pairs_only) {
      this->reject_portrait_slot(now_ms, slot);
    } else if (slot == this->state_.active_slot) {
      this->fallback_preloaded_pair_to_single(slot);
    }
  }

  void fallback_preloaded_pair_to_single(int slot) {
    if (slot < 0 || slot > 2) return;
    this->clear_preload_for_slot(
        slot, this->state_.portrait_preload_slot,
        this->state_.portrait_preload_left_ready,
        this->state_.portrait_preload_right_ready,
        this->state_.preload_noncritical_in_flight,
        this->state_.noncritical_remote_updates_in_flight);
    this->state_.portrait = PortraitState{};
    this->state_.portrait.no_companion_active = true;
    this->state_.active_slot_displayed = true;
    this->emit_command(SLIDESHOW_COMMAND_DISPLAY_CURRENT, slot);
  }

  bool request_prefetch(bool backlight_paused, bool retry_cooldown_active, uint32_t now_ms,
                        bool background_memory_available = true) {
    auto &state = this->state_;
    const int active_slot = state.active_slot;
    const auto &portrait = state.portrait;
    const auto &flags = state.slot_flags;
    if (backlight_paused || retry_cooldown_active) return false;
    if (!state.active_slot_displayed) return false;

    const SlotMeta &active = state.slot(active_slot);
    bool active_portrait_busy = false;
    if (active.is_portrait) {
      bool left_pending = !portrait.left_ready;
      bool right_pending = portrait.companion_found && !portrait.right_ready;
      active_portrait_busy = left_pending || right_pending;
    }

    bool preload_busy = (state.portrait_preload_slot != -1) &&
                        !(state.portrait_preload_left_ready && state.portrait_preload_right_ready);
    if (any_slot_fetch_in_flight(flags) || active_portrait_busy || preload_busy ||
        portrait.workflow_busy || state.noncritical_remote_updates_in_flight > 0)
      return false;
    if ((now_ms - state.last_prefetch_start_ms) < 600) return false;

    if (!background_memory_available) {
      this->emit_command(SLIDESHOW_COMMAND_PREFETCH_AFTER_DELAY, active_slot, 1200);
      return false;
    }

    if (!SlideshowController::enqueue_prefetch_slots(
            state.fetch_queue, active_slot, state.slot0, state.slot1, state.slot2, flags, now_ms)) {
      return false;
    }

    FetchJob job;
    if (!state.fetch_queue.pop(job)) return false;
    state.target_slot = job.slot;
    state.last_prefetch_start_ms = now_ms;
    this->emit_command(SLIDESHOW_COMMAND_FETCH_INTO_SLOT, job.slot);
    return true;
  }

  bool request_deferred_slot_update(int slot) {
    auto &state = this->state_;
    if (slot < 0 || slot > 2 || state.slot(slot).pending_asset_id.empty()) return false;
    if (!prepare_deferred_slot_update(
            slot, state.active_slot, state.slot_flags, state.portrait.workflow_busy,
            state.noncritical_remote_updates_in_flight)) {
      this->emit_command(SLIDESHOW_COMMAND_PREFETCH_AFTER_DELAY, slot, 500);
      return false;
    }
    this->emit_command(SLIDESHOW_COMMAND_UPDATE_SLOT_IMAGE, slot);
    return true;
  }

  void defer_slot_update_due_to_busy(int slot) {
    if (slot < 0 || slot > 2) return;
    auto &state = this->state_;
    clear_noncritical(slot, state.slot_flags, state.noncritical_remote_updates_in_flight);
    clear_slot_fetch_in_flight(slot, state.slot_flags);
    this->emit_command(SLIDESHOW_COMMAND_PREFETCH_AFTER_DELAY, slot, 500);
  }

  bool pipeline_watchdog_timed_out(uint32_t now_ms, bool advance_due, bool blocked) {
    if (!advance_due || !blocked) {
      this->state_.pipeline_blocked_since_ms = 0;
      this->state_.pipeline_blocked_tracking = false;
      return false;
    }
    if (!this->state_.pipeline_blocked_tracking) {
      this->state_.pipeline_blocked_since_ms = now_ms;
      this->state_.pipeline_blocked_tracking = true;
      return false;
    }
    return (now_ms - this->state_.pipeline_blocked_since_ms) >= PIPELINE_STALL_TIMEOUT_MS;
  }

  void recover_stalled_pipeline() {
    this->commands_.clear();
    this->state_.fetch_queue.clear();
    this->state_.slot_flags = SlotFlags{};
    this->state_.preload_noncritical_in_flight = false;
    this->state_.noncritical_remote_updates_in_flight = 0;
    this->state_.portrait = PortraitState{};
    this->state_.portrait_search_datetime.clear();
    this->state_.portrait_primary_asset_id.clear();
    this->state_.companion_target_slot = -1;
    this->state_.portrait_companion_url.clear();
    this->state_.portrait_preload_slot = -1;
    this->state_.portrait_preload_left_ready = false;
    this->state_.portrait_preload_right_ready = false;
    this->state_.portrait_search_expanded = false;
    this->state_.portrait_search_exhaustive = false;
    this->state_.portrait_search_page = 1;
    this->state_.portrait_search_cursor.clear();
    this->state_.portrait_search_generation++;
    this->state_.portrait_search_request_generation = 0;
    this->state_.deferred_fetch_target = -1;
    this->state_.rejected_fetch_target = -1;
    this->state_.download_retries = 0;
    this->state_.last_prefetch_start_ms = 0;
    this->state_.pipeline_blocked_since_ms = 0;
    this->state_.pipeline_blocked_tracking = false;
    this->state_.target_slot = this->state_.active_slot;
  }

  bool request_portrait_update(PortraitSide side) {
    const auto &portrait = this->state_.portrait;
    if (side == PortraitSide::LEFT) {
      if (!portrait.left_requested || portrait.left_ready) return false;
      this->emit_command(SLIDESHOW_COMMAND_UPDATE_PORTRAIT_LEFT);
    } else {
      if (!portrait.right_requested || portrait.right_ready) return false;
      this->emit_command(SLIDESHOW_COMMAND_UPDATE_PORTRAIT_RIGHT);
    }
    return true;
  }

  bool request_preload_update(PortraitSide side) {
    auto &state = this->state_;
    if (state.portrait_preload_slot < 0 || state.portrait_preload_slot > 2) return false;
    if (state.portrait.workflow_busy && state.portrait_preload_slot != state.active_slot) {
      if (side == PortraitSide::RIGHT) {
        this->clear_preload_noncritical_(state.preload_noncritical_in_flight,
                                         state.noncritical_remote_updates_in_flight);
      }
      this->emit_command(SLIDESHOW_COMMAND_PREFETCH_AFTER_DELAY, -1, 500);
      return false;
    }
    if (side == PortraitSide::LEFT) {
      if (!state.preload_noncritical_in_flight) {
        state.preload_noncritical_in_flight = true;
        state.noncritical_remote_updates_in_flight++;
      }
      this->emit_command(SLIDESHOW_COMMAND_UPDATE_PRELOAD_LEFT);
    } else {
      this->emit_command(SLIDESHOW_COMMAND_UPDATE_PRELOAD_RIGHT);
    }
    return true;
  }

 private:
  static void clear_preload_noncritical_(bool &preload_noncritical_in_flight,
                                         int &noncritical_count) {
    if (preload_noncritical_in_flight) {
      preload_noncritical_in_flight = false;
      if (noncritical_count > 0) noncritical_count--;
    }
  }

  SlideshowCommandQueue commands_{};
  SlideshowRuntimeState state_{};
};
