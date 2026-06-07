#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

inline uint32_t parse_duration_option_seconds(const std::string &option,
                                              uint32_t fallback_seconds,
                                              uint32_t min_seconds,
                                              uint32_t max_seconds) {
  char *end = nullptr;
  unsigned long value = std::strtoul(option.c_str(), &end, 10);
  if (end == option.c_str() || value == 0) value = fallback_seconds;
  if (option.find("minute") != std::string::npos) value *= 60;
  if (value < min_seconds) return min_seconds;
  if (value > max_seconds) return max_seconds;
  return static_cast<uint32_t>(value);
}
