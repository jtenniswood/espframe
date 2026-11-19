#pragma once

#include <cstdint>

namespace espframe::native {

struct TouchReport {
  int x{};
  int y{};
  uint8_t contacts{};
  bool valid{};
  uint32_t sequence{};
  int64_t sampled_us{};
};

}  // namespace espframe::native
