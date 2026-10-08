#pragma once

#include "esphome/core/component.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/components/update/update_entity.h"
#include "configuration_api.h"
#include "reset_coordinator.h"
#include "frame_identity_api.h"
#include "espframe_helpers.h"
#include "memory_pressure.h"
#include "automation_controller.h"
#include "memory_diagnostics.h"
#include "wifi_setup_portal.h"

namespace esphome {
namespace espframe {

class EspFrameComponent : public Component, public ConfigurationUpdateScheduler {
 public:
  EspFrameComponent() : configuration_api_(this, &this->reset_), reset_api_(&this->reset_), identity_api_(&this->identity_) {}

  void setup() override {
    this->reset_.setup();
    this->identity_.setup();
    auto *base = web_server_base::global_web_server_base;
    if (base != nullptr) {
      base->add_handler(&this->configuration_api_);
      base->add_handler(&this->reset_api_);
      base->add_handler(&this->identity_api_);
#ifdef USE_CAPTIVE_PORTAL
      base->add_handler_without_auth(&this->wifi_setup_portal_);
#endif
    }
    this->set_interval("memory-sample", 1000, [this]() { this->sample_memory_(); });
    this->set_interval("memory-report", 60000, [this]() { this->record_memory("periodic"); });
  }

  bool allow_background_work() {
    const size_t free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t largest = this->sample_memory_();
    const bool allowed = this->memory_pressure_.allow_background_work(free, largest);
    if (allowed != this->background_memory_available_) {
      this->background_memory_available_ = allowed;
      this->record_memory(allowed ? "prefetch-resumed" : "prefetch-paused");
    }
    return allowed;
  }

  // The allocator low-water mark also catches dips between one-second samples.
  // Largest-block minima are sampled, and are labelled accordingly in logs.
  void record_memory(const char *phase) {
#ifdef ESPFRAME_MEMORY_DIAGNOSTICS
    record_loop_stack(phase);
#endif
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const size_t largest = this->sample_memory_();
    ESP_LOGI("memory", "%s internal_free=%u internal_low_water=%u largest=%u largest_sampled_min=%u psram_free=%u",
             phase, (unsigned) heap_caps_get_free_size(caps),
             (unsigned) heap_caps_get_minimum_free_size(caps), (unsigned) largest,
             (unsigned) this->largest_sampled_min_,
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }

  // Identity must apply after entity registration and before networking starts.
  // WebServerBase queues handlers here; timers run after application setup.
  float get_setup_priority() const override { return 1000.0f; }

  void schedule_configuration_update(std::function<void()> &&update) override { this->defer(std::move(update)); }

  void set_firmware_update_in_progress(bool value) { this->reset_.set_firmware_update_in_progress(value); }
  bool clear_factory_wifi_reset() { return this->reset_.clear_factory_wifi_reset(); }
#ifdef USE_CAPTIVE_PORTAL
  void set_wifi_setup_page(const uint8_t *data, size_t size) { this->wifi_setup_portal_.set_page(data, size); }
#endif

  void set_c6_update_entity(update::UpdateEntity *entity) {
    if (entity == nullptr) return;
    auto sync_update_state = [this, entity]() {
      this->reset_.set_c6_update_in_progress(entity->state == update::UPDATE_STATE_INSTALLING);
    };
    sync_update_state();
    entity->add_on_state_callback(sync_update_state);
  }

  template<typename... Scripts> void stop_slideshow_workers(Scripts *...scripts) {
    stop_scripts_in_order(scripts...);
  }

  template<typename Display, typename PairingSwitch>
  void apply_screen_rotation(const std::string &option, const std::array<int, 4> &rotations,
                             Display *display, PairingSwitch *pairing) {
    apply_rotation_plan(rotation_plan(option, rotations), display, pairing);
  }

  SlotFetchDecision check_slot_fetch(bool wifi_connected, bool configured, bool paused,
                                    bool cooldown, bool image_downloading) const {
    return slot_fetch_decision(this->slideshow_.state(), wifi_connected, configured, paused, cooldown,
                               image_downloading);
  }

  EspFrameSlideshow &slideshow() { return this->slideshow_; }
  const EspFrameSlideshow &slideshow() const { return this->slideshow_; }

 protected:
  size_t sample_memory_() {
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    this->largest_sampled_min_ = std::min(this->largest_sampled_min_, largest);
    return largest;
  }

  MemoryPressurePolicy memory_pressure_{};
  size_t largest_sampled_min_ = SIZE_MAX;
  bool background_memory_available_ = true;
  ConfigurationApiHandler configuration_api_;
  ResetCoordinator reset_;
  ResetApiHandler reset_api_;
  FrameIdentity identity_;
  FrameIdentityApiHandler identity_api_;
  EspFrameSlideshow slideshow_{};
#ifdef USE_CAPTIVE_PORTAL
  WiFiSetupPortal wifi_setup_portal_;
#endif
};

}  // namespace espframe
}  // namespace esphome
