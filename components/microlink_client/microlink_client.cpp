#include "microlink_client.h"

#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace microlink_client {

static const char *const TAG = "microlink_client";

void MicrolinkClient::setup() {
  ESP_LOGCONFIG(TAG, "Setting up MicroLink Tailscale client");
  this->publish_status_(true);
}

void MicrolinkClient::loop() {
  if (!this->start_attempted_) {
    if (!network::is_connected()) {
      const uint32_t now = millis();
      if (now - this->last_publish_ms_ > 5000) {
        this->publish_status_(true);
      }
      return;
    }
    this->start_();
  }

  if (this->started_) {
    const uint32_t now = millis();
    if (now - this->last_publish_ms_ > 5000) {
      this->publish_status_();
    }
  }
}

void MicrolinkClient::dump_config() {
  ESP_LOGCONFIG(TAG, "MicroLink Tailscale Client:");
  ESP_LOGCONFIG(TAG, "  Device name: %s", this->device_name_.empty() ? "(auto)" : this->device_name_.c_str());
  ESP_LOGCONFIG(TAG, "  Max peers: %u", this->max_peers_);
  ESP_LOGCONFIG(TAG, "  State sensor: %s", YESNO(this->state_sensor_ != nullptr));
  ESP_LOGCONFIG(TAG, "  IP sensor: %s", YESNO(this->ip_sensor_ != nullptr));
}

void MicrolinkClient::start_() {
  this->start_attempted_ = true;

  if (this->auth_key_.empty()) {
    ESP_LOGE(TAG, "Cannot start MicroLink: auth_key is empty");
    if (this->state_sensor_ != nullptr) {
      this->state_sensor_->publish_state("error_empty_auth_key");
    }
    return;
  }

  microlink_config_t config = {};
  config.auth_key = this->auth_key_.c_str();
  config.device_name = this->device_name_.empty() ? microlink_default_device_name() : this->device_name_.c_str();
  config.enable_derp = true;
  config.enable_stun = true;
  config.enable_disco = true;
  config.max_peers = this->max_peers_;
  config.wifi_tx_power_dbm = this->wifi_tx_power_dbm_;

  ESP_LOGI(TAG, "Starting MicroLink for device %s", config.device_name);
  this->client_ = microlink_init(&config);
  if (this->client_ == nullptr) {
    ESP_LOGE(TAG, "microlink_init() failed");
    if (this->state_sensor_ != nullptr) {
      this->state_sensor_->publish_state("error_init");
    }
    return;
  }

  esp_err_t err = microlink_start(this->client_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "microlink_start() failed: %s", esp_err_to_name(err));
    microlink_destroy(this->client_);
    this->client_ = nullptr;
    if (this->state_sensor_ != nullptr) {
      this->state_sensor_->publish_state("error_start");
    }
    return;
  }

  this->started_ = true;
  this->publish_status_(true);
}

void MicrolinkClient::publish_status_(bool force) {
  this->last_publish_ms_ = millis();

  if (!this->started_ || this->client_ == nullptr) {
    if (force && this->state_sensor_ != nullptr) {
      this->state_sensor_->publish_state(network::is_connected() ? "starting" : "waiting_wifi");
    }
    if (force && this->ip_sensor_ != nullptr) {
      this->ip_sensor_->publish_state("");
    }
    return;
  }

  const microlink_state_t state = microlink_get_state(this->client_);
  if (force || this->last_state_ != static_cast<int>(state)) {
    const char *state_name = this->state_to_string_(state);
    ESP_LOGI(TAG, "MicroLink state: %s", state_name);
    if (this->state_sensor_ != nullptr) {
      this->state_sensor_->publish_state(state_name);
    }
    this->last_state_ = static_cast<int>(state);
  }

  const uint32_t ip = microlink_get_vpn_ip(this->client_);
  if (force || this->last_ip_ != ip) {
    char ip_str[16] = {};
    if (ip != 0) {
      microlink_ip_to_str(ip, ip_str);
      ESP_LOGI(TAG, "MicroLink VPN IP: %s", ip_str);
    }
    if (this->ip_sensor_ != nullptr) {
      this->ip_sensor_->publish_state(ip != 0 ? ip_str : "");
    }
    this->last_ip_ = ip;
  }
}

const char *MicrolinkClient::state_to_string_(microlink_state_t state) const {
  switch (state) {
    case ML_STATE_IDLE:
      return "idle";
    case ML_STATE_WIFI_WAIT:
      return "wifi_wait";
    case ML_STATE_CONNECTING:
      return "connecting";
    case ML_STATE_REGISTERING:
      return "registering";
    case ML_STATE_CONNECTED:
      return "connected";
    case ML_STATE_RECONNECTING:
      return "reconnecting";
    case ML_STATE_ERROR:
      return "error";
    default:
      return "unknown";
  }
}

}  // namespace microlink_client
}  // namespace esphome
