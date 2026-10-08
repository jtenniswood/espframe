#include <cassert>
#include <iostream>
#include <string_view>
#include "components/espframe/wifi_setup_routes.h"

int main() {
  using namespace esphome::espframe;
  const std::string_view probes[] = {
      "/hotspot-detect.html", "/library/test/success.html", "/generate_204", "/gen_204",
      "/connecttest.txt", "/ncsi.txt", "/redirect", "/canonical.html", "/success.txt",
      "/fwlink", "/check_network_status.txt"};
  assert(wifi_setup_route(std::string_view("/"), true, true) == WiFiSetupRoute::PAGE);
  for (auto path : probes) {
    assert(wifi_setup_route(path, true, true) == WiFiSetupRoute::REDIRECT);
    assert(wifi_setup_route(path, false, true) == WiFiSetupRoute::PASS);
    assert(wifi_setup_route(path, true, false) == WiFiSetupRoute::PASS);
  }
  for (std::string_view path : {"/config.json", "/wifisave", "/update", "/0.js", "/0.css", "/events",
                               "/espframe/api/v1/reset", "/espframe/api/v1/configuration"}) {
    assert(wifi_setup_route(path, true, true) == WiFiSetupRoute::PASS);
  }
  assert(wifi_setup_route(std::string_view("/"), false, true) == WiFiSetupRoute::PASS);
  assert(wifi_setup_route(std::string_view("/"), true, false) == WiFiSetupRoute::PASS);
  std::cout << "WiFi setup routes preserve station, provisioning, API and upload endpoints\n";
}
