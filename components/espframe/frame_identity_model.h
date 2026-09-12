#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

namespace esphome::espframe {
constexpr size_t FRAME_NAME_MAX_BYTES = 120;

inline bool normalize_frame_name(const std::string &input, std::string &output) {
  const size_t start = input.find_first_not_of(" \t\r\n\f\v");
  output = start == std::string::npos ? "" :
      input.substr(start, input.find_last_not_of(" \t\r\n\f\v") - start + 1);
  if (output.size() > FRAME_NAME_MAX_BYTES) return false;
  // Validate UTF-8, including overlong encodings, surrogates and C0/C1 controls.
  for (size_t i = 0; i < output.size();) {
    uint32_t code = static_cast<uint8_t>(output[i++]);
    unsigned extra = 0;
    uint32_t minimum = 0;
    if (code >= 0xc2 && code <= 0xdf) { code &= 0x1f; extra = 1; minimum = 0x80; }
    else if (code >= 0xe0 && code <= 0xef) { code &= 0x0f; extra = 2; minimum = 0x800; }
    else if (code >= 0xf0 && code <= 0xf4) { code &= 0x07; extra = 3; minimum = 0x10000; }
    else if (code >= 0x80) return false;
    if (i + extra > output.size()) return false;
    while (extra--) {
      const auto next = static_cast<uint8_t>(output[i++]);
      if ((next & 0xc0) != 0x80) return false;
      code = (code << 6) | (next & 0x3f);
    }
    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff) ||
        code < 32 || (code >= 127 && code <= 159)) return false;
  }
  return true;
}

inline std::string frame_hostname(const std::string &name, const std::string &suffix) {
  std::string slug;
  bool separator = false;
  for (unsigned char c : name) {
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      if (separator && !slug.empty()) slug += '-';
      slug += static_cast<char>(c);
      separator = false;
    } else separator = true;
  }
  if (slug.empty()) slug = "frame";
  // Stay within ESPHome's 24-character hostname limit, including the suffix.
  slug.resize(std::min<size_t>(19, slug.size()));
  while (!slug.empty() && slug.back() == '-') slug.pop_back();
  return slug + "-" + suffix;
}
}  // namespace esphome::espframe
