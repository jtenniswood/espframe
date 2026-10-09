"""Exercise the production AP lambda and reset boot method with host adapters.

Storage and WiFi hardware are modeled; this checks station/preference selection
across reset, reprovisioning and reboot, not physical flash persistence.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import textwrap

ROOT = Path(__file__).resolve().parents[1]
connectivity = (ROOT / "common/addon/connectivity.yaml").read_text()
on_connect = connectivity.split("  on_connect:", 1)[1].split("  on_disconnect:", 1)[0]
assert "clear_factory_wifi_reset" not in on_connect, "Connecting must not erase the persistent compiled-WiFi override"
ap_lambda = re.search(r"(?m)^        - lambda: \|-\n((?:^ {12}.*\n)+)", connectivity)
assert ap_lambda, "Missing setup AP boot lambda"
ap_body = textwrap.dedent(ap_lambda.group(1))

reset_source = (ROOT / "components/espframe/reset_coordinator.cpp").read_text()
start = reset_source.index("void ResetCoordinator::setup()")
brace = reset_source.index("{", start)
depth = 1
end = brace + 1
while depth:
    depth += (reset_source[end] == "{") - (reset_source[end] == "}")
    end += 1
setup = reset_source[start:end]

source = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
std::string mac;
std::string get_mac_address() { return mac; }
namespace wifi {
struct WiFiAP {
  std::string ssid{"espframe"}, password{"ap-password"};
  const std::string &get_ssid() const { return ssid; }
  void set_ssid(const std::string &value) { ssid = value; }
};
struct WiFiComponent {
  WiFiAP ap;
  bool compiled_sta{true};
  unsigned clear_calls{0};
  WiFiAP get_ap() const { return ap; }
  void set_ap(const WiFiAP &value) { ap = value; }
  bool has_sta() const { return compiled_sta; }
  void clear_sta() { compiled_sta = false; ++clear_calls; }
};
WiFiComponent component;
WiFiComponent *global_wifi_component = &component;
}
bool persistent_override = false;
std::map<uint32_t, std::string> saved_networks;
constexpr uint32_t compiled_key = 1234567, fallback_key = 88491487;
uint32_t preference_key() { return wifi::component.has_sta() ? compiled_key : fallback_key; }
bool factory_wifi_reset_pending() { return persistent_override; }
bool save_factory_wifi_reset() { persistent_override = true; return true; }
enum class ResetMode { NONE, CUSTOMIZATION, FACTORY };
struct ResetCoordinator {
  ResetMode mode_{ResetMode::NONE};
  bool failed_{false};
  unsigned epoch_{0};
  bool load_() { return true; }
  bool save_() { return true; }
  bool clear_preferences_(ResetMode mode) {
    if (mode == ResetMode::FACTORY) saved_networks.clear();
    return true;
  }
  void setup();
};
''' + setup + '\nvoid configure_ap() {\n' + ap_body + r'''
}
int main() {
  mac = "30:ED:A0:E2:F3:6A";
  configure_ap();
  assert(wifi::component.ap.ssid == "espframe_E2F36A");
  assert(wifi::component.ap.password == "ap-password");
  wifi::component.ap.ssid = "My custom recovery AP";
  configure_ap();
  assert(wifi::component.ap.ssid == "My custom recovery AP");
  wifi::component.ap.ssid = "espframe";
  mac = "30-ED-A0-E2-F3-6A";
  configure_ap();
  assert(wifi::component.ap.ssid == "espframe_E2F36A");
  wifi::component.ap.ssid = "espframe";
  mac.clear();
  configure_ap();
  assert(wifi::component.ap.ssid == "espframe_SETUP");

  ResetCoordinator untouched;
  untouched.setup();
  assert(wifi::component.has_sta() && preference_key() == compiled_key);
  saved_networks[compiled_key] = "compiled network";
  ResetCoordinator factory;
  factory.mode_ = ResetMode::FACTORY;
  factory.setup();
  assert(persistent_override && !factory.failed_ && factory.epoch_ == 1);
  assert(!wifi::component.has_sta() && saved_networks.empty());
  saved_networks[preference_key()] = "newly provisioned network";
  for (unsigned reboot = 0; reboot < 3; ++reboot) {
    wifi::component.compiled_sta = true;  // YAML compiled stations are recreated.
    ResetCoordinator restarted;
    restarted.setup();
    assert(persistent_override && !wifi::component.has_sta());
    assert(preference_key() == fallback_key);
    assert(saved_networks.at(preference_key()) == "newly provisioned network");
  }
  assert(wifi::component.clear_calls == 4);
  std::cout << "WiFi setup preserves custom AP names and reprovisioned credentials across three modeled reboots\n";
}
'''

for name in ("install", "troubleshooting", "usb-flashing"):
    docs = (ROOT / f"docs/{name}.md").read_text()
    assert "**espframe_**" in docs and "Older firmware uses **espframe**" in docs
    assert "custom setup SSID" in docs

with tempfile.TemporaryDirectory(prefix="espframe-wifi-policy-") as directory:
    cpp = Path(directory) / "wifi_setup_policy.cpp"
    executable = Path(directory) / "wifi_setup_policy"
    cpp.write_text(source)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", str(cpp), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
