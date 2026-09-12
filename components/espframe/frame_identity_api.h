#pragma once

#include "esphome/components/json/json_util.h"
#include "esphome/components/network/util.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "frame_identity.h"

namespace esphome::espframe {
class FrameIdentityApiHandler final : public AsyncWebHandler {
 public:
  explicit FrameIdentityApiHandler(FrameIdentity *identity) : identity_(identity) {}
  bool canHandle(AsyncWebServerRequest *request) const override {
    char path[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->url_to(path) == "/espframe/api/v1/identity" &&
           (request->method() == HTTP_GET || request->method() == HTTP_POST);
  }
  void handleRequest(AsyncWebServerRequest *request) override {
    if (identity_ == nullptr) {
      request->send(500, "text/plain", "Frame identity unavailable");
      return;
    }
    if (request->method() == HTTP_POST) {
      std::string name;
      if (!request->hasArg("name") || !normalize_frame_name(request->arg("name"), name)) {
        request->send(400, "text/plain", "Use up to 120 UTF-8 bytes without control characters");
        return;
      }
      if (!identity_->save(name)) {
        request->send(500, "text/plain", "Frame name could not be saved. Check available settings storage and retry.");
        return;
      }
    }
    json::JsonBuilder builder;
    JsonObject root = builder.root();
    root["name"] = identity_->saved_name();
    root["friendly_name"] = identity_->target_name();
    root["hostname"] = identity_->target_hostname();
    root["mac_suffix"] = identity_->suffix();
    root["restart_required"] = identity_->restart_required();
    char address[network::IP_ADDRESS_BUFFER_SIZE];
    const auto addresses = network::get_ip_addresses();
    root["ip_address"] = addresses.empty() ? std::string() : std::string(addresses[0].str_to(address));
    const auto payload = builder.serialize();
    request->send(200, "application/json", payload.c_str());
  }
 protected:
  FrameIdentity *identity_{nullptr};
};
}  // namespace esphome::espframe
