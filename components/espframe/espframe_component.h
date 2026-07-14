#pragma once

#include "esphome/core/component.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "configuration_api.h"
#include "espframe_helpers.h"

namespace esphome {
namespace espframe {

class EspFrameComponent : public Component, public ConfigurationUpdateScheduler {
 public:
  EspFrameComponent() : configuration_api_(this) {}

  void setup() override {
    auto *base = web_server_base::global_web_server_base;
    if (base != nullptr) base->add_handler(&this->configuration_api_);
  }

  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void schedule_configuration_update(std::function<void()> &&update) override { this->defer(std::move(update)); }

  EspFrameSlideshow &slideshow() { return this->slideshow_; }
  const EspFrameSlideshow &slideshow() const { return this->slideshow_; }

 protected:
  ConfigurationApiHandler configuration_api_;
  EspFrameSlideshow slideshow_{};
};

}  // namespace espframe
}  // namespace esphome
