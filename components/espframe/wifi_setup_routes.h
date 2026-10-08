#pragma once

namespace esphome::espframe {

enum class WiFiSetupRoute { PASS, PAGE, REDIRECT };

template<typename Url>
WiFiSetupRoute wifi_setup_route(const Url &url, bool portal_active, bool is_get) {
  if (!portal_active || !is_get) return WiFiSetupRoute::PASS;
  if (url == "/") return WiFiSetupRoute::PAGE;
  if (url == "/hotspot-detect.html" || url == "/library/test/success.html" ||
      url == "/generate_204" || url == "/gen_204" || url == "/connecttest.txt" ||
      url == "/ncsi.txt" || url == "/redirect" || url == "/canonical.html" ||
      url == "/success.txt" || url == "/fwlink" || url == "/check_network_status.txt")
    return WiFiSetupRoute::REDIRECT;
  return WiFiSetupRoute::PASS;
}

}  // namespace esphome::espframe
