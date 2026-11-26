#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace espframe::native {

inline constexpr char recovery_host[] = "192.168.4.1";
inline constexpr char recovery_url[] = "http://192.168.4.1";
inline constexpr char recovery_ssid_characters[] = "espfram_0123456789ABCDEF";
inline constexpr char setup_address_characters[] = "htp:/0123456789.";

enum class SetupPage : uint8_t { None, Wifi, Immich };

constexpr SetupPage setup_page(bool first_run_hotspot, bool immich_configured, uint32_t ipv4) {
  if (!immich_configured && ipv4)
    return SetupPage::Immich;
  return first_run_hotspot && !ipv4 ? SetupPage::Wifi : SetupPage::None;
}

struct RecoveryNetwork {
  std::array<char, 16> ssid{};
  std::array<char, 13> password{};
};

class RecoveryWindow {
 public:
  static constexpr int64_t offline_delay_us = 90'000'000;
  static constexpr int64_t connected_grace_us = 15'000'000;

  void update(int64_t now, bool connected) {
    if (!changed_at_ || connected != connected_) {
      changed_at_ = now;
      connected_ = connected;
    }
  }

  std::optional<int64_t> change_at(bool configured, bool hotspot) const {
    if (!changed_at_ || connected_ != hotspot)
      return {};
    if (connected_)
      return *changed_at_ + connected_grace_us;
    return *changed_at_ + (configured ? offline_delay_us : 0);
  }

 private:
  std::optional<int64_t> changed_at_;
  bool connected_{};
};

}  // namespace espframe::native
