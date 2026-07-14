#pragma once

#include <cstring>

#include "configuration_contract_generated.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text/text.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/application.h"

namespace esphome::espframe {

class ConfigurationApiHandler final : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest *request) const override {
    if (request->method() != HTTP_GET) return false;
#ifdef USE_ESP32
    char url_buffer[AsyncWebServerRequest::URL_BUF_SIZE];
    StringRef url = request->url_to(url_buffer);
#else
    const auto &url = request->url();
#endif
    return url == contract::CAPABILITIES_PATH || url == contract::CONFIGURATION_PATH;
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

 protected:
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
};

}  // namespace esphome::espframe
