#include "clock_settings.h"

namespace espframe::native {
namespace {

constexpr ClockZone zones[]{
#include "native_clock_zones.inc"
};

}  // namespace

std::span<const ClockZone> clock_zones() { return zones; }

const char *clock_timezone(std::string_view option) {
  for (const auto &zone : zones) {
    if (option == zone.option)
      return zone.posix;
  }
  return nullptr;
}

}  // namespace espframe::native
