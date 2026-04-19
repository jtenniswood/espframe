#pragma once

#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <cstdint>
#include <string>

extern "C" {
#include "microlink.h"
}

namespace esphome {
namespace microlink_client {

class MicrolinkClient : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_auth_key(const std::string &auth_key) { this->auth_key_ = auth_key; }
  void set_device_name(const std::string &device_name) { this->device_name_ = device_name; }
  void set_max_peers(uint8_t max_peers) { this->max_peers_ = max_peers; }
  void set_wifi_tx_power_dbm(int8_t wifi_tx_power_dbm) { this->wifi_tx_power_dbm_ = wifi_tx_power_dbm; }
  void set_state_sensor(text_sensor::TextSensor *sensor) { this->state_sensor_ = sensor; }
  void set_ip_sensor(text_sensor::TextSensor *sensor) { this->ip_sensor_ = sensor; }

 protected:
  void start_();
  void publish_status_(bool force = false);
  const char *state_to_string_(microlink_state_t state) const;

  std::string auth_key_;
  std::string device_name_;
  uint8_t max_peers_{8};
  int8_t wifi_tx_power_dbm_{0};

  text_sensor::TextSensor *state_sensor_{nullptr};
  text_sensor::TextSensor *ip_sensor_{nullptr};

  microlink_t *client_{nullptr};
  bool start_attempted_{false};
  bool started_{false};
  uint32_t last_publish_ms_{0};
  int last_state_{-1};
  uint32_t last_ip_{0};
};

}  // namespace microlink_client
}  // namespace esphome
