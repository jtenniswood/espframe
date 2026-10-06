#pragma once

#include <cstdint>
#include <atomic>

#include "esphome/components/web_server_base/web_server_base.h"

namespace esphome::espframe {

enum class ResetMode : uint8_t { NONE = 0, CUSTOMIZATION = 1, FACTORY = 2 };

class ResetCoordinator {
 public:
  void setup();
  bool request(ResetMode mode);
  bool pending() const { return this->mode_ != ResetMode::NONE; }
  ResetMode mode() const { return this->mode_; }
  uint32_t epoch() const { return this->epoch_; }
  bool failed() const { return this->failed_.load(); }
  void set_firmware_update_in_progress(bool value) { this->firmware_update_in_progress_.store(value); }
  bool firmware_update_in_progress() const { return this->firmware_update_in_progress_.load(); }
  void set_settings_update_in_progress(bool value) { this->settings_update_in_progress_.store(value); }
  bool settings_update_in_progress() const { return this->settings_update_in_progress_.load(); }

 private:
  bool load_();
  bool save_();
  bool clear_preferences_(ResetMode mode);

  ResetMode mode_{ResetMode::NONE};
  uint32_t epoch_{0};
  std::atomic<bool> failed_{false};
  std::atomic<bool> firmware_update_in_progress_{false};
  std::atomic<bool> settings_update_in_progress_{false};
};

class ResetApiHandler final : public AsyncWebHandler {
 public:
  explicit ResetApiHandler(ResetCoordinator *coordinator) : coordinator_(coordinator) {}
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;

 private:
  ResetCoordinator *coordinator_;
};

}  // namespace esphome::espframe
