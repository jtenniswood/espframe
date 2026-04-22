#pragma once

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
};

struct SlideshowCommand {
  SlideshowCommandKind kind = SLIDESHOW_COMMAND_NONE;
  int slot = -1;
  uint32_t delay_ms = 0;
};

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

class EspFrameSlideshow {
 public:
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

  SlideshowAction on_slot_download_finished(int slot, SlotMeta &meta, SlotFlags &flags,
                                            int &noncritical_count, int &download_retries,
                                            int active_slot, bool portrait_pairing_enabled,
                                            bool &active_slot_displayed, DisplayMeta &current_display,
                                            PortraitState &portrait, int &companion_target_slot,
                                            int portrait_preload_slot,
                                            std::string &portrait_search_datetime,
                                            std::string &portrait_primary_asset_id) {
    SlideshowAction action = SlideshowController::handle_slot_download_finished(
        slot, meta, flags, noncritical_count, download_retries, active_slot,
        portrait_pairing_enabled, active_slot_displayed, current_display, portrait,
        companion_target_slot, portrait_preload_slot, portrait_search_datetime,
        portrait_primary_asset_id);
    this->emit_action(action, slot);
    return action;
  }

  void on_slot_download_error(int slot, SlotFlags &flags, int &noncritical_count,
                              std::string &diag_reason, int &last_downloaded_slot,
                              const char *reason) {
    SlideshowController::handle_slot_download_error(
        slot, flags, noncritical_count, diag_reason, last_downloaded_slot, reason);
    this->emit_command(SLIDESHOW_COMMAND_LOG_DIAG, slot);
    this->emit_command(SLIDESHOW_COMMAND_HANDLE_SLOT_DOWNLOAD_ERROR, slot);
  }

  bool start_active_portrait(int active_slot, SlotMeta &slot0, SlotMeta &slot1, SlotMeta &slot2,
                             PortraitState &portrait, bool active_slot_displayed,
                             std::string &portrait_primary_asset_id,
                             std::string &portrait_companion_url,
                             std::string &portrait_search_datetime,
                             int &companion_target_slot) {
    if (active_slot_displayed && portrait.is_pair && portrait.using_preload) return false;

    SlotMeta &meta = this->slot_mut_(active_slot, slot0, slot1, slot2);
    if (portrait.workflow_busy && portrait_primary_asset_id == meta.asset_id) return false;

    portrait.left_ready = false;
    portrait.right_ready = false;
    portrait.no_companion_active = false;
    portrait.left_requested = false;
    portrait.right_requested = false;
    portrait.using_preload = false;
    portrait.workflow_busy = true;
    portrait_primary_asset_id = meta.asset_id;

    if (!meta.companion_url.empty()) {
      portrait.companion_found = true;
      portrait_companion_url = meta.companion_url;
      portrait.left_requested = true;
      this->emit_command(SLIDESHOW_COMMAND_START_PORTRAIT_LEFT, active_slot);
      return true;
    }

    portrait.companion_found = false;
    portrait_companion_url = "";
    portrait_search_datetime = meta.datetime;
    companion_target_slot = active_slot;
    this->emit_command(SLIDESHOW_COMMAND_DEFER_COMPANION_SEARCH, active_slot, 200);
    return true;
  }

  void on_portrait_left_finished(PortraitState &portrait) {
    portrait.left_ready = true;
    portrait.left_requested = false;
    if (portrait.companion_found && !portrait.right_ready && !portrait.right_requested) {
      portrait.right_requested = true;
      this->emit_command(SLIDESHOW_COMMAND_START_PORTRAIT_RIGHT);
    }
    if (portrait.left_ready && portrait.right_ready) {
      this->emit_command(SLIDESHOW_COMMAND_DISPLAY_PORTRAIT_PAIR);
    }
  }

  void on_portrait_right_finished(PortraitState &portrait) {
    portrait.right_ready = true;
    portrait.right_requested = false;
    if (portrait.left_ready && portrait.right_ready) {
      this->emit_command(SLIDESHOW_COMMAND_DISPLAY_PORTRAIT_PAIR);
    }
  }

  void on_portrait_left_error(PortraitState &portrait, std::string &diag_reason,
                              bool &active_slot_displayed) {
    portrait.left_ready = false;
    portrait.left_requested = false;
    portrait.companion_found = false;
    portrait.no_companion_active = true;
    diag_reason = "portrait left error";
    this->emit_command(SLIDESHOW_COMMAND_LOG_DIAG);
    if (!active_slot_displayed) {
      active_slot_displayed = true;
      portrait.workflow_busy = false;
      this->emit_command(SLIDESHOW_COMMAND_DISPLAY_CURRENT);
    }
  }

  void on_portrait_right_error(PortraitState &portrait, std::string &diag_reason,
                               bool &active_slot_displayed) {
    portrait.right_ready = false;
    portrait.right_requested = false;
    portrait.companion_found = false;
    portrait.no_companion_active = true;
    diag_reason = "portrait right error";
    this->emit_command(SLIDESHOW_COMMAND_LOG_DIAG);
    if (!active_slot_displayed) {
      active_slot_displayed = true;
      portrait.workflow_busy = false;
      this->emit_command(SLIDESHOW_COMMAND_DISPLAY_CURRENT);
    }
  }

  void on_preload_left_finished(int portrait_preload_slot, SlotMeta &slot0, SlotMeta &slot1,
                                SlotMeta &slot2, bool &portrait_preload_left_ready,
                                bool &portrait_preload_right_ready,
                                bool &preload_noncritical_in_flight, int &noncritical_count) {
    portrait_preload_left_ready = true;
    if (portrait_preload_slot >= 0) {
      SlotMeta &meta = this->slot_mut_(portrait_preload_slot, slot0, slot1, slot2);
      if (!meta.companion_url.empty()) {
        this->emit_command(SLIDESHOW_COMMAND_START_PRELOAD_RIGHT, portrait_preload_slot);
        return;
      }
    }
    portrait_preload_right_ready = false;
    this->clear_preload_noncritical_(preload_noncritical_in_flight, noncritical_count);
  }

  void on_preload_left_error(std::string &diag_reason, bool &portrait_preload_left_ready,
                             bool &preload_noncritical_in_flight, int &noncritical_count) {
    portrait_preload_left_ready = false;
    diag_reason = "portrait preload left error";
    this->clear_preload_noncritical_(preload_noncritical_in_flight, noncritical_count);
    this->emit_command(SLIDESHOW_COMMAND_LOG_DIAG);
  }

  void on_preload_right_finished(bool &portrait_preload_right_ready,
                                 bool &preload_noncritical_in_flight,
                                 int &noncritical_count) {
    portrait_preload_right_ready = true;
    this->clear_preload_noncritical_(preload_noncritical_in_flight, noncritical_count);
  }

  void on_preload_right_error(std::string &diag_reason, bool &portrait_preload_right_ready,
                              bool &preload_noncritical_in_flight, int &noncritical_count) {
    portrait_preload_right_ready = false;
    diag_reason = "portrait preload right error";
    this->clear_preload_noncritical_(preload_noncritical_in_flight, noncritical_count);
    this->emit_command(SLIDESHOW_COMMAND_LOG_DIAG);
  }

  void advance_forward(uint32_t now_ms, bool retry_cooldown_active,
                       int &active_slot, int &target_slot, bool &active_slot_displayed,
                       uint32_t &last_advance_ms, SlotMeta &slot0, SlotMeta &slot1,
                       SlotMeta &slot2, DisplayMeta &current_display,
                       DisplayMeta &prev_display, PortraitState &portrait,
                       SlotFlags &flags, int &noncritical_count,
                       bool portrait_pairing_enabled, int portrait_preload_slot,
                       bool portrait_preload_left_ready, bool portrait_preload_right_ready,
                       std::string &diag_reason) {
    if (!active_slot_displayed) {
      int slot = active_slot;
      SlotMeta &meta = this->slot_mut_(slot, slot0, slot1, slot2);
      if (meta.ready && portrait.workflow_busy) {
        portrait.workflow_busy = false;
        portrait.left_ready = false;
        portrait.right_ready = false;
        portrait.right_requested = false;
        portrait.companion_found = false;
        active_slot_displayed = true;
        copy_slot_to_display(meta, current_display);
        this->emit_command(SLIDESHOW_COMMAND_DISPLAY_CURRENT, slot);
        return;
      }
      if (!meta.ready) {
        bool in_flight = flags.fetch_in_flight[slot];
        uint32_t fetch_age = slot_fetch_age_ms(slot, flags, now_ms);
        if (in_flight && fetch_age > 15000) {
          clear_noncritical(slot, flags, noncritical_count);
          clear_slot_fetch_in_flight(slot, flags);
          diag_reason = "h3 stuck slot";
          this->emit_command(SLIDESHOW_COMMAND_ABORT_SLOT_DOWNLOAD, slot);
          this->emit_command(SLIDESHOW_COMMAND_LOG_DIAG, slot);
          in_flight = false;
        }
        if (!in_flight && !any_slot_fetch_in_flight(flags) && !retry_cooldown_active) {
          portrait.workflow_busy = false;
          last_advance_ms = now_ms;
          target_slot = slot;
          this->emit_command(SLIDESHOW_COMMAND_DEFER_FETCH_INTO_SLOT, slot, 200);
        }
      }
      return;
    }

    if (!current_display.asset_id.empty()) {
      int old_slot = active_slot;
      SlotMeta &old_meta = this->slot_mut_(old_slot, slot0, slot1, slot2);
      prev_display = current_display;
      prev_display.zoom = old_meta.zoom;
      prev_display.valid = true;
    }

    portrait = PortraitState{};
    int old_slot = active_slot;
    active_slot = (old_slot + 1) % 3;
    active_slot_displayed = false;
    SlotMeta &cleared = this->slot_mut_(old_slot, slot0, slot1, slot2);
    cleared.ready = false;
    last_advance_ms = now_ms;

    SlotMeta &meta = this->slot_mut_(active_slot, slot0, slot1, slot2);
    if (meta.ready) {
      copy_slot_to_display(meta, current_display);
      bool pair = meta.is_portrait && portrait_pairing_enabled;
      if (!pair) active_slot_displayed = true;

      bool preload_ready = pair && portrait_preload_slot == active_slot &&
                           portrait_preload_left_ready && portrait_preload_right_ready;
      if (!pair || preload_ready) {
        this->emit_command(SLIDESHOW_COMMAND_DISPLAY_CURRENT, active_slot);
      } else {
        this->emit_command(SLIDESHOW_COMMAND_START_ACTIVE_PAIR, active_slot);
      }
      this->emit_command(SLIDESHOW_COMMAND_PREFETCH_AFTER_DELAY, active_slot, 500);
      return;
    }

    if (!flags.fetch_in_flight[active_slot]) {
      target_slot = active_slot;
      this->emit_command(SLIDESHOW_COMMAND_DEFER_FETCH_INTO_SLOT, active_slot, 200);
    }
  }

  bool show_previous(uint32_t now_ms, int &active_slot, bool &active_slot_displayed,
                     SlotMeta &slot0, SlotMeta &slot1, SlotMeta &slot2,
                     DisplayMeta &current_display, DisplayMeta &prev_display,
                     PortraitState &portrait, SlotFlags &flags) {
    if (!prev_display.valid) {
      this->emit_command(SLIDESHOW_COMMAND_LOG_NO_PREVIOUS);
      return false;
    }

    int current_slot = active_slot;
    SlotMeta &current_meta = this->slot_mut_(current_slot, slot0, slot1, slot2);
    DisplayMeta tmp = current_display;
    tmp.zoom = current_meta.zoom;

    current_display = prev_display;
    prev_display = tmp;

    portrait = PortraitState{};
    int previous_slot = (active_slot + 2) % 3;
    active_slot = previous_slot;
    active_slot_displayed = false;

    SlotMeta &meta = this->slot_mut_(previous_slot, slot0, slot1, slot2);
    meta.ready = false;
    meta.is_portrait = false;
    meta.companion_url = "";
    meta.datetime = "";
    copy_display_to_slot(current_display, meta);
    meta.pending_asset_id = current_display.asset_id;
    mark_slot_fetch_in_flight(previous_slot, flags, now_ms);

    this->emit_command(SLIDESHOW_COMMAND_LOAD_PREVIOUS_SLOT, previous_slot);
    return true;
  }

  void handle_companion_not_found(PortraitState &portrait,
                                  std::string &portrait_companion_url,
                                  int companion_target_slot, int active_slot,
                                  SlotMeta &slot0, SlotMeta &slot1, SlotMeta &slot2,
                                  bool &active_slot_displayed) {
    portrait.companion_found = false;
    portrait_companion_url = "";
    portrait.left_requested = false;
    portrait.right_requested = false;
    SlotMeta &meta = this->slot_mut_(companion_target_slot, slot0, slot1, slot2);
    meta.companion_url = "";
    if (companion_target_slot == active_slot) {
      portrait.no_companion_active = true;
      portrait.workflow_busy = false;
      if (!active_slot_displayed) {
        active_slot_displayed = true;
        this->emit_command(SLIDESHOW_COMMAND_DISPLAY_CURRENT, active_slot);
      }
    }
  }

  bool on_companion_found(const std::string &companion_url, PortraitState &portrait,
                          std::string &portrait_companion_url, int companion_target_slot,
                          int active_slot, SlotMeta &slot0, SlotMeta &slot1,
                          SlotMeta &slot2, int &portrait_preload_slot,
                          bool &portrait_preload_left_ready,
                          bool &portrait_preload_right_ready) {
    if (companion_url.empty()) return false;

    portrait.no_companion_active = false;
    portrait.companion_found = true;
    portrait_companion_url = companion_url;
    SlotMeta &meta = this->slot_mut_(companion_target_slot, slot0, slot1, slot2);
    meta.companion_url = companion_url;

    if (companion_target_slot == active_slot && !portrait.left_requested && !portrait.left_ready) {
      portrait.left_ready = false;
      portrait.right_ready = false;
      portrait.left_requested = true;
      portrait.right_requested = false;
      this->emit_command(SLIDESHOW_COMMAND_START_PORTRAIT_LEFT, companion_target_slot);
      return true;
    }

    if (companion_target_slot != active_slot) {
      portrait_preload_slot = companion_target_slot;
      portrait_preload_left_ready = false;
      portrait_preload_right_ready = false;
      this->emit_command(SLIDESHOW_COMMAND_START_PRELOAD_LEFT, companion_target_slot);
      return true;
    }

    return true;
  }

  bool begin_display_current(int active_slot, SlotMeta &slot0, SlotMeta &slot1,
                             SlotMeta &slot2, PortraitState &portrait,
                             bool portrait_pairing_enabled, bool &active_slot_displayed) {
    SlotMeta &meta = this->slot_mut_(active_slot, slot0, slot1, slot2);
    portrait.is_pair = false;
    bool pair = meta.is_portrait && portrait_pairing_enabled;
    if (!pair) {
      active_slot_displayed = true;
      portrait.workflow_busy = false;
    }
    return pair;
  }

  void after_display_current(int active_slot, SlotMeta &slot0, SlotMeta &slot1,
                             SlotMeta &slot2, PortraitState &portrait,
                             bool portrait_pairing_enabled, bool &active_slot_displayed,
                             int &portrait_preload_slot,
                             bool portrait_preload_left_ready,
                             bool portrait_preload_right_ready) {
    SlotMeta &meta = this->slot_mut_(active_slot, slot0, slot1, slot2);
    bool pair = meta.is_portrait && portrait_pairing_enabled;
    if (!pair) return;

    if (portrait_preload_slot == active_slot && portrait_preload_left_ready &&
        portrait_preload_right_ready) {
      portrait.is_pair = true;
      portrait.using_preload = true;
      portrait_preload_slot = -1;
      active_slot_displayed = true;
      portrait.workflow_busy = false;
      this->emit_command(SLIDESHOW_COMMAND_DISPLAY_PRELOADED_PAIR, active_slot);
      return;
    }

    if (!portrait.is_pair && !portrait.no_companion_active) {
      this->emit_command(SLIDESHOW_COMMAND_START_ACTIVE_PAIR, active_slot);
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

  bool request_prefetch(bool backlight_paused, bool retry_cooldown_active, uint32_t now_ms,
                        uint32_t &last_prefetch_start_ms, int active_slot, int &target_slot,
                        const SlotMeta &slot0, const SlotMeta &slot1, const SlotMeta &slot2,
                        const SlotFlags &flags, FetchQueue &queue, const PortraitState &portrait,
                        bool active_slot_displayed, int noncritical_count, int portrait_preload_slot,
                        bool portrait_preload_left_ready, bool portrait_preload_right_ready) {
    if (backlight_paused || retry_cooldown_active) return false;
    if (!active_slot_displayed) return false;

    const SlotMeta &active = this->slot_const_(active_slot, slot0, slot1, slot2);
    bool active_portrait_busy = false;
    if (active.is_portrait) {
      bool left_pending = !portrait.left_ready;
      bool right_pending = portrait.companion_found && !portrait.right_ready;
      active_portrait_busy = left_pending || right_pending;
    }

    bool preload_busy = (portrait_preload_slot != -1) &&
                        !(portrait_preload_left_ready && portrait_preload_right_ready);
    if (any_slot_fetch_in_flight(flags) || active_portrait_busy || preload_busy ||
        portrait.workflow_busy || noncritical_count > 0)
      return false;
    if ((now_ms - last_prefetch_start_ms) < 600) return false;

    if (!SlideshowController::enqueue_prefetch_slots(
            queue, active_slot, slot0, slot1, slot2, flags, now_ms)) {
      return false;
    }

    FetchJob job;
    if (!queue.pop(job)) return false;
    target_slot = job.slot;
    last_prefetch_start_ms = now_ms;
    this->emit_command(SLIDESHOW_COMMAND_FETCH_INTO_SLOT, job.slot);
    return true;
  }

  bool request_deferred_slot_update(int slot, int active_slot, SlotFlags &flags,
                                    bool portrait_workflow_busy, int &noncritical_count) {
    if (!prepare_deferred_slot_update(
            slot, active_slot, flags, portrait_workflow_busy, noncritical_count)) {
      this->emit_command(SLIDESHOW_COMMAND_PREFETCH_AFTER_DELAY, slot, 500);
      return false;
    }
    this->emit_command(SLIDESHOW_COMMAND_UPDATE_SLOT_IMAGE, slot);
    return true;
  }

  bool request_portrait_left_update(const PortraitState &portrait) {
    if (portrait.left_ready) return false;
    this->emit_command(SLIDESHOW_COMMAND_UPDATE_PORTRAIT_LEFT);
    return true;
  }

  bool request_portrait_right_update(const PortraitState &portrait) {
    if (portrait.right_ready) return false;
    this->emit_command(SLIDESHOW_COMMAND_UPDATE_PORTRAIT_RIGHT);
    return true;
  }

  bool request_preload_left_update(bool portrait_workflow_busy,
                                   bool &preload_noncritical_in_flight,
                                   int &noncritical_count) {
    if (portrait_workflow_busy) {
      this->emit_command(SLIDESHOW_COMMAND_PREFETCH_AFTER_DELAY, -1, 500);
      return false;
    }
    if (!preload_noncritical_in_flight) {
      preload_noncritical_in_flight = true;
      noncritical_count++;
    }
    this->emit_command(SLIDESHOW_COMMAND_UPDATE_PRELOAD_LEFT);
    return true;
  }

  bool request_preload_right_update(bool portrait_workflow_busy,
                                    bool &preload_noncritical_in_flight,
                                    int &noncritical_count) {
    if (portrait_workflow_busy) {
      if (preload_noncritical_in_flight) {
        preload_noncritical_in_flight = false;
        if (noncritical_count > 0) noncritical_count--;
      }
      this->emit_command(SLIDESHOW_COMMAND_PREFETCH_AFTER_DELAY, -1, 500);
      return false;
    }
    this->emit_command(SLIDESHOW_COMMAND_UPDATE_PRELOAD_RIGHT);
    return true;
  }

 private:
  static SlotMeta &slot_mut_(int slot, SlotMeta &slot0, SlotMeta &slot1, SlotMeta &slot2) {
    return slot == 0 ? slot0 : (slot == 1 ? slot1 : slot2);
  }

  static const SlotMeta &slot_const_(int slot, const SlotMeta &slot0, const SlotMeta &slot1,
                                     const SlotMeta &slot2) {
    return slot == 0 ? slot0 : (slot == 1 ? slot1 : slot2);
  }

  static void clear_preload_noncritical_(bool &preload_noncritical_in_flight,
                                         int &noncritical_count) {
    if (preload_noncritical_in_flight) {
      preload_noncritical_in_flight = false;
      if (noncritical_count > 0) noncritical_count--;
    }
  }

  SlideshowCommandQueue commands_{};
};
