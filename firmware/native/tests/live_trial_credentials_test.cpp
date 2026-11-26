#include "live_trial_credentials.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <string_view>

using namespace espframe::native;

int main() {
  std::array<uint8_t, esphome_station_bytes> station{};
  constexpr std::string_view ssid = "Trial Wi-Fi";
  constexpr std::string_view password = "test-password";
  std::copy(ssid.begin(), ssid.end(), station.begin());
  std::copy(password.begin(), password.end(), station.begin() + 33);
  const auto wifi = read_stock_station(station);
  assert(wifi && wifi->ssid.data() == ssid && wifi->password.data() == password);
  assert(!read_stock_station(std::span(station).first(station.size() - 1)));

  station[33] = 0;
  assert(read_stock_station(station)->password[0] == '\0');
  station[0] = 0;
  assert(!read_stock_station(station));
  station.fill('x');
  assert(!read_stock_station(station));
  station[32] = 0;
  station[97] = 0;
  assert(read_stock_station(station));

  std::array<uint8_t, esphome_url_bytes> url{};
  constexpr std::string_view address = "https://photos.test";
  url[0] = address.size();
  std::copy(address.begin(), address.end(), url.begin() + 1);
  assert(read_stock_preference_text(url) == address);
  url[1] = 0;
  assert(read_stock_preference_text(url).empty());

  std::array<uint8_t, esphome_api_key_bytes> key{};
  key.fill('x');
  key[0] = 254;
  assert(read_stock_preference_text(key).size() == 254);
  key[0] = 255;
  assert(read_stock_preference_text(key).empty());
  assert(read_stock_preference_text({}).empty());

  Settings settings;
  assert(TrialScope{}.apply(settings) && !settings.capture_dates.enabled && !settings.portrait_pairs_only);
  TrialScope scope{.source = 6, .rotation = 2, .date_mode = 1, .pairing_range = 2, .date_enabled = 1, .pairs_only = 1,
                   .date_amount = 4, .date_from = {}, .date_to = {}};
  assert(scope.apply(settings) && settings.capture_dates.needs_time() && settings.portrait_pairs_only);
  assert(settings.requires_portrait_pair() && settings.portrait_pairing_days() == 2);
  scope.pairing_enabled = 0;
  assert(scope.apply(settings) && !settings.requires_portrait_pair() && settings.portrait_pairing_days() == 2);
  scope.pairing_range = portrait_pairing_ranges.size() - 1;
  assert(scope.apply(settings) && settings.portrait_pairing_days() == portrait_pairing_any_time);
  scope.pairing_range = portrait_pairing_ranges.size();
  assert(!scope.apply(settings));
  scope.pairing_range = 2;
  assert(settings.capture_dates.amount == 4 && settings.capture_dates.years);
  assert(settings.screen_rotation == ScreenRotation::UpsideDown);
  scope.source = 5;
  assert(!scope.apply(settings));
  scope.source = 6;
  scope.orientation = 1;
  assert(scope.apply(settings) && settings.photo_orientation == PhotoOrientation::Portrait);
  scope.pairing_enabled = 1;
  scope.orientation = 2;
  assert(scope.apply(settings) && settings.photo_orientation == PhotoOrientation::Landscape);
  assert(!settings.requires_portrait_pair() && settings.portrait_pairing && settings.portrait_pairs_only);
  scope.orientation = 3;
  assert(!scope.apply(settings));
  scope.orientation = 0;
  scope.date_from = "2024-03-01";
  scope.date_to = "2024-02-29";
  assert(!scope.apply(settings));
  scope.date_to = "2024-03-02";
  scope.date_amount = 4.5;
  assert(!scope.apply(settings));
}
