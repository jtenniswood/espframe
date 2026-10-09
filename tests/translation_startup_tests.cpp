#include <cassert>
#include <functional>
#include <map>
#include <string>
#include "components/espframe/i18n.h"

struct Application {
  bool complete = false;
  bool is_setup_complete() const { return complete; }
} App;

struct Scheduler {
  std::map<std::string, std::function<void()>> intervals, deferred;
  void cancel_interval(const std::string &name) { intervals.erase(name); }
  void set_interval(const std::string &name, unsigned, std::function<void()> callback) {
    intervals[name] = std::move(callback);
  }
  void defer(const std::string &name, std::function<void()> callback) {
    deferred[name] = std::move(callback);
  }
  void tick() {
    // Call a copy: callbacks may cancel their own interval.
    auto callbacks = intervals;
    for (auto &entry : callbacks) entry.second();
  }
  void flush() {
    auto callbacks = std::move(deferred);
    deferred.clear();
    for (auto &entry : callbacks) entry.second();
  }
} espframe_core;

struct MetadataRefresh {
  unsigned calls = 0;
  void execute() { ++calls; }
} update_photo_metadata_display;

struct lv_obj_t { std::string text; };
static unsigned paints = 0;
void lv_label_set_text(lv_obj_t *label, const char *text) {
  assert(App.is_setup_complete());
  label->text = text;
  ++paints;
}
const char *lv_label_get_text(lv_obj_t *label) { return label->text.c_str(); }

namespace wifi {
struct AccessPoint { std::string get_ssid() const { return "Frame"; } };
struct WiFiComponent { AccessPoint get_ap() const { return {}; } } component;
auto *global_wifi_component = &component;
}

#define id(name) name
#include "translation_startup_fixture.inc"

int main() {
  loading_status_label->text = "Connecting to WiFi";
  connection_failed_title->text = "Invalid API Key";
  connection_failed_subtitle->text = "Check your Immich API key in\nthe espframe settings.";

  on_language_value("de");
  assert(espframe_core.intervals.size() == 1);
  for (unsigned i = 0; i < 5; ++i) espframe_core.tick();
  assert(paints == 0 && update_photo_metadata_display.calls == 0);

  // A second restored value must replace the pending refresh.
  on_language_value("en");
  assert(espframe_core.intervals.size() == 1);
  App.complete = true;
  espframe_core.tick();
  assert(espframe_core.intervals.empty());
  assert(i18n_wifi_setup->text == "WiFi Setup");
  assert(connection_failed_title->text == "Invalid API Key");
  assert(update_photo_metadata_display.calls == 1);
  espframe_core.tick();
  assert(update_photo_metadata_display.calls == 1);

  // Live changes coalesce and preserve the displayed error in either locale.
  on_language_value("de");
  on_language_value("en");
  on_language_value("de");
  assert(espframe_core.deferred.size() == 1);
  espframe_core.flush();
  assert(i18n_wifi_setup->text == "WLAN einrichten");
  assert(connection_failed_title->text == "Ungültiger API-Schlüssel");
  assert(loading_status_label->text == "Verbindung mit WLAN");
  assert(update_photo_metadata_display.calls == 2);
  on_language_value("en");
  espframe_core.flush();
  assert(connection_failed_title->text == "Invalid API Key");
  assert(update_photo_metadata_display.calls == 3);

  // Preserve the active error and status when switching between every locale.
  for (const char *code : espframe_i18n_catalogue::LANGUAGES) {
    on_language_value(code);
    const auto calls = update_photo_metadata_display.calls;
    espframe_core.flush();
    assert(connection_failed_title->text == espframe_i18n_key("invalid_api_key"));
    assert(connection_failed_subtitle->text == espframe_i18n_key("check_api_key"));
    assert(loading_status_label->text == espframe_i18n_key("connecting_to_wifi"));
    assert(i18n_wifi_setup->text == espframe_i18n_key("wifi_setup"));
    assert(update_photo_metadata_display.calls == calls + 1);
  }
}
