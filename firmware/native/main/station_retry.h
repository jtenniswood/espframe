#pragma once

#include <algorithm>
#include <cstdint>

namespace espframe::native {

class StationRetry {
 public:
  enum class Event { None, Disconnected, AddressLost };
  enum class Action { Wait, Connect, Disconnect };

  Action step(int64_t now, bool has_address, Event event) {
    if (has_address) {
      connected_ = true;
      connecting_ = false;
      delay_us_ = initial_delay_us;
      return Action::Wait;
    }
    if (connected_) {
      connected_ = false;
      next_at_us_ = now;
    }
    if (event == Event::AddressLost) {
      failed(now);
      return Action::Disconnect;
    }
    if (connecting_) {
      if (event == Event::Disconnected) {
        failed(now);
      } else if (now >= next_at_us_) {
        failed(now);
        return Action::Disconnect;
      }
      return Action::Wait;
    }
    if (now < next_at_us_)
      return Action::Wait;
    connecting_ = true;
    next_at_us_ = now + connect_timeout_us;
    return Action::Connect;
  }

  void failed(int64_t now) {
    connecting_ = false;
    next_at_us_ = now + delay_us_;
    delay_us_ = std::min(delay_us_ * 2, max_delay_us);
  }

  int64_t next_at_us() const { return next_at_us_; }

 private:
  static constexpr int64_t initial_delay_us = 1'000'000;
  static constexpr int64_t max_delay_us = 15'000'000;
  static constexpr int64_t connect_timeout_us = 8'000'000;
  int64_t next_at_us_{};
  int64_t delay_us_{initial_delay_us};
  bool connected_{};
  bool connecting_{};
};

// Odd versions identify usable IP leases. A disconnect and a reconnect must
// change the version even when DHCP returns the same IP address again.
inline constexpr bool has_station_address(uint32_t lease_version) { return (lease_version & 1u) != 0; }

inline constexpr uint32_t next_station_lease(uint32_t current, bool online, bool address_changed = false) {
  if (has_station_address(current) != online)
    return current + 1;
  if (online && address_changed)
    return current + 2;
  return current;
}

}  // namespace espframe::native
