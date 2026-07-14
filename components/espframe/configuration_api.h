#pragma once

#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "configuration_contract_generated.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text/text.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/application.h"
#include "esphome/core/component.h"

namespace esphome::espframe {

class ConfigurationUpdateScheduler {
 public:
  virtual ~ConfigurationUpdateScheduler() = default;
  virtual void schedule_configuration_update(std::function<void()> &&update) = 0;
};

class ConfigurationApiHandler final : public AsyncWebHandler {
 public:
  explicit ConfigurationApiHandler(ConfigurationUpdateScheduler *scheduler) : scheduler_(scheduler) {}

  bool canHandle(AsyncWebServerRequest *request) const override {
#ifdef USE_ESP32
    char url_buffer[AsyncWebServerRequest::URL_BUF_SIZE];
    StringRef url = request->url_to(url_buffer);
#else
    const auto &url = request->url();
#endif
    if (url == contract::CAPABILITIES_PATH) return request->method() == HTTP_GET;
    return url == contract::CONFIGURATION_PATH &&
           (request->method() == HTTP_GET || request->method() == HTTP_POST);
  }

  void handleRequest(AsyncWebServerRequest *request) override {
#ifdef USE_ESP32
    char url_buffer[AsyncWebServerRequest::URL_BUF_SIZE];
    StringRef url = request->url_to(url_buffer);
#else
    const auto &url = request->url();
#endif
    if (url == contract::CAPABILITIES_PATH) {
      request->send(200, "application/json", contract::CAPABILITIES_JSON);
      return;
    }

    if (request->method() == HTTP_POST) {
      this->handle_update_(request);
      return;
    }

    this->send_configuration_(request);
  }

 protected:
  enum class PendingDomain : uint8_t { SELECT, NUMBER, SWITCH, TEXT };

  struct PendingValue {
    PendingDomain domain;
    void *entity;
    std::string string_value;
    float number_value{0.0f};
    bool bool_value{false};
  };

  void send_configuration_(AsyncWebServerRequest *request) const {
    json::JsonBuilder builder;
    JsonObject root = builder.root();
    root["api_version"] = contract::API_VERSION;
    JsonObject values = root["values"].to<JsonObject>();
    JsonArray unavailable = root["unavailable"].to<JsonArray>();
    for (const auto &field : contract::CONFIGURATION_FIELDS) {
      if (!this->write_field_value_(values, field)) unavailable.add(field.key);
    }
    auto payload = builder.serialize();
    request->send(200, "application/json", payload.c_str());
  }

  template<typename T, typename Collection> static T *find_entity_(const Collection &entities, const char *name) {
    for (auto *entity : entities) {
      if (entity != nullptr && std::strcmp(entity->get_name().c_str(), name) == 0) return entity;
    }
    return nullptr;
  }

  bool write_field_value_(JsonObject values, const contract::ConfigurationField &field) const {
    if (std::strcmp(field.domain, "select") == 0) {
      auto *entity = find_entity_<select::Select>(App.get_selects(), field.entity_name);
      if (entity == nullptr) return false;
      values[field.key] = entity->current_option().c_str();
      return true;
    }
    if (std::strcmp(field.domain, "number") == 0) {
      auto *entity = find_entity_<number::Number>(App.get_numbers(), field.entity_name);
      if (entity == nullptr) return false;
      values[field.key] = entity->state;
      return true;
    }
    if (std::strcmp(field.domain, "switch") == 0) {
      auto *entity = find_entity_<switch_::Switch>(App.get_switches(), field.entity_name);
      if (entity == nullptr) return false;
      values[field.key] = entity->state;
      return true;
    }
    if (std::strcmp(field.domain, "text") == 0) {
      auto *entity = find_entity_<text::Text>(App.get_texts(), field.entity_name);
      if (entity == nullptr) return false;
      values[field.key] = entity->state;
      return true;
    }
    return false;
  }

  static const contract::ConfigurationField *find_field_(const char *key) {
    for (const auto &field : contract::CONFIGURATION_FIELDS) {
      if (std::strcmp(field.key, key) == 0) return &field;
    }
    return nullptr;
  }

  static void send_error_(AsyncWebServerRequest *request, int status, const char *code, const char *field = nullptr) {
    json::JsonBuilder builder;
    JsonObject root = builder.root();
    root["api_version"] = contract::API_VERSION;
    root["status"] = "rejected";
    root["error"] = code;
    if (field != nullptr) root["field"] = field;
    auto payload = builder.serialize();
    request->send(status, "application/json", payload.c_str());
  }

  static bool validate_field_(const contract::ConfigurationField &field, JsonVariantConst value,
                              PendingValue &pending, const char *&error) {
    if (std::strcmp(field.domain, "select") == 0) {
      if (!value.is<const char *>()) {
        error = "invalid_type";
        return false;
      }
      auto *entity = find_entity_<select::Select>(App.get_selects(), field.entity_name);
      const char *option = value.as<const char *>();
      if (entity == nullptr) {
        error = "unavailable";
        return false;
      }
      if (!entity->has_option(option)) {
        error = "invalid_option";
        return false;
      }
      pending.domain = PendingDomain::SELECT;
      pending.entity = entity;
      pending.string_value = option;
      return true;
    }

    if (std::strcmp(field.domain, "number") == 0) {
      if (!value.is<float>() || value.is<bool>()) {
        error = "invalid_type";
        return false;
      }
      auto *entity = find_entity_<number::Number>(App.get_numbers(), field.entity_name);
      const float number_value = value.as<float>();
      if (entity == nullptr) {
        error = "unavailable";
        return false;
      }
      const auto &traits = entity->traits;
      if (!std::isfinite(number_value) || number_value < traits.get_min_value() ||
          number_value > traits.get_max_value()) {
        error = "out_of_range";
        return false;
      }
      const float step = traits.get_step();
      const float minimum = traits.get_min_value();
      if (std::isfinite(step) && step > 0.0f && std::isfinite(minimum)) {
        const float steps = (number_value - minimum) / step;
        if (std::fabs(steps - std::round(steps)) > 0.001f) {
          error = "invalid_step";
          return false;
        }
      }
      pending.domain = PendingDomain::NUMBER;
      pending.entity = entity;
      pending.number_value = number_value;
      return true;
    }

    if (std::strcmp(field.domain, "switch") == 0) {
      if (!value.is<bool>()) {
        error = "invalid_type";
        return false;
      }
      auto *entity = find_entity_<switch_::Switch>(App.get_switches(), field.entity_name);
      if (entity == nullptr) {
        error = "unavailable";
        return false;
      }
      pending.domain = PendingDomain::SWITCH;
      pending.entity = entity;
      pending.bool_value = value.as<bool>();
      return true;
    }

    if (std::strcmp(field.domain, "text") == 0) {
      if (!value.is<const char *>()) {
        error = "invalid_type";
        return false;
      }
      auto *entity = find_entity_<text::Text>(App.get_texts(), field.entity_name);
      const char *text_value = value.as<const char *>();
      if (entity == nullptr) {
        error = "unavailable";
        return false;
      }
      const size_t length = std::strlen(text_value);
      if (length < static_cast<size_t>(entity->traits.get_min_length()) ||
          length > static_cast<size_t>(entity->traits.get_max_length())) {
        error = "invalid_length";
        return false;
      }
      pending.domain = PendingDomain::TEXT;
      pending.entity = entity;
      pending.string_value = text_value;
      return true;
    }

    error = "unsupported_domain";
    return false;
  }

  void handle_update_(AsyncWebServerRequest *request) {
    if (this->update_pending_.load()) {
      send_error_(request, 409, "update_in_progress");
      return;
    }
    // ESPHome's IDF request wrapper searches the captured form body before the
    // URL query when hasArg()/arg() are used.
    if (!request->hasArg("configuration")) {
      send_error_(request, 400, "missing_configuration");
      return;
    }
    std::string configuration = request->arg("configuration");

    JsonDocument document = json::parse_json(configuration);
    if (!document.is<JsonObject>()) {
      send_error_(request, 400, "invalid_json");
      return;
    }
    JsonObject root = document.as<JsonObject>();
    if (root["api_version"].is<unsigned int>() && root["api_version"].as<unsigned int>() != contract::API_VERSION) {
      send_error_(request, 409, "unsupported_api_version");
      return;
    }
    if (!root["values"].is<JsonObject>()) {
      send_error_(request, 400, "missing_values");
      return;
    }

    std::vector<PendingValue> pending_values;
    JsonObject values = root["values"].as<JsonObject>();
    pending_values.reserve(values.size());
    for (JsonPair pair : values) {
      const char *key = pair.key().c_str();
      const auto *field = find_field_(key);
      if (field == nullptr) {
        send_error_(request, 422, "unknown_field", key);
        return;
      }
      PendingValue pending{};
      const char *error = nullptr;
      if (!validate_field_(*field, pair.value(), pending, error)) {
        send_error_(request, 422, error, key);
        return;
      }
      pending_values.push_back(std::move(pending));
    }

    this->update_pending_.store(true);
    const size_t updated = pending_values.size();
    this->scheduler_->schedule_configuration_update([this, pending_values = std::move(pending_values)]() mutable {
      for (auto &pending : pending_values) this->apply_pending_(pending);
      this->update_pending_.store(false);
    });

    json::JsonBuilder builder;
    JsonObject response = builder.root();
    response["api_version"] = contract::API_VERSION;
    response["status"] = "accepted";
    response["updated"] = updated;
    auto payload = builder.serialize();
    request->send(202, "application/json", payload.c_str());
  }

  static void apply_pending_(PendingValue &pending) {
    switch (pending.domain) {
      case PendingDomain::SELECT: {
        auto call = static_cast<select::Select *>(pending.entity)->make_call();
        call.set_option(pending.string_value.c_str(), pending.string_value.size());
        call.perform();
        break;
      }
      case PendingDomain::NUMBER: {
        auto call = static_cast<number::Number *>(pending.entity)->make_call();
        call.set_value(pending.number_value);
        call.perform();
        break;
      }
      case PendingDomain::SWITCH:
        static_cast<switch_::Switch *>(pending.entity)->control(pending.bool_value);
        break;
      case PendingDomain::TEXT: {
        auto call = static_cast<text::Text *>(pending.entity)->make_call();
        call.set_value(pending.string_value);
        call.perform();
        break;
      }
    }
  }

  ConfigurationUpdateScheduler *scheduler_;
  std::atomic<bool> update_pending_{false};
};

}  // namespace esphome::espframe
