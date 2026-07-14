#pragma once

#include "configuration_contract_generated.h"
#include "esphome/components/web_server_base/web_server_base.h"

namespace esphome::espframe {

class ConfigurationApiHandler final : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest *request) const override {
    return request->method() == HTTP_GET && request->url() == contract::CAPABILITIES_PATH;
  }

  void handleRequest(AsyncWebServerRequest *request) override {
    request->send(200, "application/json", contract::CAPABILITIES_JSON);
  }
};

}  // namespace esphome::espframe
