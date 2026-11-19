#include "station_retry.h"

#include <cassert>

using namespace espframe::native;
using Action = StationRetry::Action;
using Event = StationRetry::Event;

void test_timeout_and_backoff() {
  StationRetry retry;
  assert(retry.step(0, false, Event::None) == Action::Connect);
  assert(retry.step(7'999'999, false, Event::None) == Action::Wait);
  assert(retry.step(8'000'000, false, Event::None) == Action::Disconnect);
  assert(retry.next_at_us() == 9'000'000);
  assert(retry.step(8'500'000, false, Event::Disconnected) == Action::Wait);
  assert(retry.next_at_us() == 9'000'000);
  assert(retry.step(9'000'000, false, Event::None) == Action::Connect);

  // Immediate driver failures back off too, up to the maximum.
  int64_t now = 9'000'100;
  for (const int64_t delay : {2'000'000, 4'000'000, 8'000'000, 15'000'000, 15'000'000}) {
    retry.failed(now);
    assert(retry.next_at_us() == now + delay);
    assert(retry.step(now + delay - 1, false, Event::None) == Action::Wait);
    now += delay;
    assert(retry.step(now, false, Event::None) == Action::Connect);
  }
  assert(retry.step(now + 1, true, Event::None) == Action::Wait);
  assert(retry.step(now + 2, false, Event::Disconnected) == Action::Connect);
  assert(retry.step(now + 3, false, Event::Disconnected) == Action::Wait);
  assert(retry.next_at_us() == now + 1'000'003);
}

void test_address_loss_and_renewal() {
  StationRetry retry;
  assert(retry.step(0, false, Event::None) == Action::Connect);
  assert(retry.step(1, true, Event::None) == Action::Wait);
  assert(retry.step(2, true, Event::AddressLost) == Action::Wait);  // A newer lease is already present.
  assert(retry.step(3, false, Event::AddressLost) == Action::Disconnect);
  assert(retry.step(4, false, Event::Disconnected) == Action::Wait);
  assert(retry.step(1'000'003, false, Event::None) == Action::Connect);
}

int main() {
  test_timeout_and_backoff();
  test_address_loss_and_renewal();
  uint32_t lease = next_station_lease(0, true);
  assert(has_station_address(lease));
  assert(next_station_lease(lease, true) == lease);
  const auto renewed = next_station_lease(lease, true, true);
  assert(has_station_address(renewed) && renewed != lease);
  lease = next_station_lease(renewed, false);
  assert(!has_station_address(lease) && next_station_lease(lease, false) == lease);
  assert(next_station_lease(lease, true) != renewed);
}
