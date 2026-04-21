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

  bool request_prefetch(bool backlight_paused, bool retry_cooldown_active, uint32_t now_ms,
                        uint32_t &last_prefetch_start_ms, int active_slot, int &target_slot,
                        const SlotMeta &slot0, const SlotMeta &slot1, const SlotMeta &slot2,
                        const SlotFlags &flags, FetchQueue &queue, const PortraitState &portrait,
                        int noncritical_count, int portrait_preload_slot,
                        bool portrait_preload_left_ready, bool portrait_preload_right_ready) {
    if (backlight_paused || retry_cooldown_active) return false;

    const SlotMeta &active = this->slot_const_(active_slot, slot0, slot1, slot2);
    bool active_portrait_busy = false;
    if (active.is_portrait) {
      bool left_pending = !portrait.left_ready;
      bool right_pending = portrait.companion_found && !portrait.right_ready;
      active_portrait_busy = left_pending || right_pending;
    }

    bool preload_busy = (portrait_preload_slot != -1) &&
                        !(portrait_preload_left_ready && portrait_preload_right_ready);
    if (active_portrait_busy || preload_busy || portrait.workflow_busy || noncritical_count > 0)
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
  static const SlotMeta &slot_const_(int slot, const SlotMeta &slot0, const SlotMeta &slot1,
                                     const SlotMeta &slot2) {
    return slot == 0 ? slot0 : (slot == 1 ? slot1 : slot2);
  }

  SlideshowCommandQueue commands_{};
};
