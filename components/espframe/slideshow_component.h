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

 private:
  SlideshowCommandQueue commands_{};
};
