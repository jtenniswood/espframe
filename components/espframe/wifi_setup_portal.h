#pragma once

#include "esphome/core/defines.h"

#ifdef USE_CAPTIVE_PORTAL
#include "esphome/components/captive_portal/captive_portal.h"
#include "esphome/components/wifi/wifi_component.h"
#include "wifi_setup_routes.h"

namespace esphome::espframe {

// Registered before the normal web panel. ESPHome still owns DNS, scan results,
// credential persistence and /update; this handler supplies the setup page and
// sends OS connectivity probes to its canonical AP address.
class WiFiSetupPortal final : public AsyncWebHandler {
 public:
  void set_page(const uint8_t *data, size_t size) { this->data_ = data; this->size_ = size; }

  bool canHandle(AsyncWebServerRequest *request) const override {
    if (this->data_ == nullptr || captive_portal::global_captive_portal == nullptr ||
        !captive_portal::global_captive_portal->is_active() || request->method() != HTTP_GET) return false;
    char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
    StringRef url = request->url_to(buffer);
    return wifi_setup_route(url, true, true) != WiFiSetupRoute::PASS;
  }

  void handleRequest(AsyncWebServerRequest *request) override {
    char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
    StringRef url = request->url_to(buffer);
    AsyncWebServerResponse *response;
    if (url == "/") {
      response = request->beginResponse(200, "text/html; charset=utf-8", this->data_, this->size_);
      response->addHeader("Content-Encoding", "gzip");
    } else {
      char ip[network::IP_ADDRESS_BUFFER_SIZE];
      wifi::global_wifi_component->wifi_soft_ap_ip().str_to(ip);
      const std::string location = "http://" + std::string(ip) + "/";
      response = request->beginResponse(302, "text/plain", "Open WiFi setup");
      response->addHeader("Location", location.c_str());
    }
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("Connection", "close");
    request->send(response);
  }

 private:
  const uint8_t *data_{nullptr};
  size_t size_{0};
};

}  // namespace esphome::espframe
#endif
