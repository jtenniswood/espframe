#pragma once

#include <array>
#include <charconv>
#include <string_view>
#include <system_error>

namespace esphome::espframe {

struct FirmwareVersion {
  std::array<unsigned, 3> numbers{};
  std::string_view prerelease;
};

inline bool firmware_version_digit(char c) { return c >= '0' && c <= '9'; }

inline bool firmware_version_numeric(std::string_view value) {
  for (char c : value) {
    if (!firmware_version_digit(c)) return false;
  }
  return !value.empty();
}

inline bool parse_firmware_version(std::string_view value, FirmwareVersion &version) {
  if (!value.empty() && value.front() == 'v') value.remove_prefix(1);
  const auto suffix = value.find('-');
  auto core = value.substr(0, suffix);
  for (size_t i = 0; i < version.numbers.size(); ++i) {
    const auto dot = core.find('.');
    if ((i + 1 < version.numbers.size()) != (dot != std::string_view::npos)) return false;
    const auto part = core.substr(0, dot);
    if (!firmware_version_numeric(part) || (part.size() > 1 && part.front() == '0')) return false;
    const auto result = std::from_chars(part.data(), part.data() + part.size(), version.numbers[i]);
    if (result.ec != std::errc{}) return false;
    if (dot != std::string_view::npos) core.remove_prefix(dot + 1);
  }

  version.prerelease = {};
  if (suffix == std::string_view::npos) return true;
  auto remaining = value.substr(suffix + 1);
  version.prerelease = remaining;
  for (;;) {
    const auto dot = remaining.find('.');
    const auto identifier = remaining.substr(0, dot);
    if (identifier.empty() ||
        (identifier.size() > 1 && identifier.front() == '0' && firmware_version_numeric(identifier))) return false;
    for (char c : identifier) {
      if (!firmware_version_digit(c) && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && c != '-') return false;
    }
    if (dot == std::string_view::npos) return true;
    remaining.remove_prefix(dot + 1);
  }
}

inline bool firmware_prerelease_is_newer(std::string_view latest, std::string_view current) {
  if (latest.empty() || current.empty()) return latest.empty() && !current.empty();
  while (!latest.empty() && !current.empty()) {
    const auto latest_dot = latest.find('.');
    const auto current_dot = current.find('.');
    const auto a = latest.substr(0, latest_dot);
    const auto b = current.substr(0, current_dot);
    if (a != b) {
      const bool a_numeric = firmware_version_numeric(a);
      const bool b_numeric = firmware_version_numeric(b);
      if (a_numeric != b_numeric) return !a_numeric;
      if (a_numeric && a.size() != b.size()) return a.size() > b.size();
      return a > b;
    }
    latest.remove_prefix(latest_dot == std::string_view::npos ? latest.size() : latest_dot + 1);
    current.remove_prefix(current_dot == std::string_view::npos ? current.size() : current_dot + 1);
  }
  return !latest.empty();
}

inline bool firmware_version_is_newer(std::string_view latest, std::string_view current) {
  FirmwareVersion candidate, installed;
  if (!parse_firmware_version(latest, candidate) || !parse_firmware_version(current, installed)) return false;
  if (candidate.numbers != installed.numbers) return candidate.numbers > installed.numbers;
  return firmware_prerelease_is_newer(candidate.prerelease, installed.prerelease);
}

}  // namespace esphome::espframe
