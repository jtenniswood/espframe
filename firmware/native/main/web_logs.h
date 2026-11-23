#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace espframe::native {

struct WebLogLine {
  uint32_t id{};
  unsigned level{};
  std::array<char, 192> message{};
};

struct WebLogPage {
  uint32_t boot{};
  size_t count{};
  bool more{};
};

// The native web admin displays application logs. SDK and third-party logs
// still go to the console, where debug output can include connection details.
class WebLogBuffer {
 public:
  static constexpr size_t capacity = 64;

  void append(std::string_view line) {
    const auto plain = strip_color_prefix(line);
    if (plain.size() < 3 || plain[1] != ' ')
      return;
    const auto level = levels.find(plain.front());
    if (level == std::string_view::npos)
      return;
    auto tag = plain.substr(2);
    if (tag.starts_with('(')) {
      const auto close = tag.find(") ");
      if (close == std::string_view::npos)
        return;
      tag.remove_prefix(close + 2);
    }
    const auto separator = tag.find(": ");
    if (separator == std::string_view::npos ||
        std::ranges::find(public_tags, tag.substr(0, separator)) == public_tags.end())
      return;

    line = line.substr(0, line.find_first_of("\r\n"));
    auto &entry = entries_[sequence_ % capacity];
    entry.id = ++sequence_;
    entry.level = static_cast<unsigned>(level + (level < 3 ? 1 : 2));
    const size_t length = std::min(line.size(), entry.message.size() - 1);
    std::copy_n(line.begin(), length, entry.message.begin());
    entry.message[length] = '\0';
  }

  WebLogPage read(uint32_t boot, uint32_t after, std::span<WebLogLine> output) const {
    const auto oldest = sequence_ > capacity ? sequence_ - capacity : 0;
    const auto start = std::clamp(after, static_cast<uint32_t>(oldest), sequence_);
    const auto available = sequence_ - start;
    const auto count = std::min<size_t>(available, output.size());
    for (size_t i = 0; i < count; ++i)
      output[i] = entries_[(start + i) % capacity];
    return {boot, count, count < available};
  }

 private:
  static std::string_view strip_color_prefix(std::string_view line) {
    if (line.starts_with("\x1b[")) {
      const auto end = line.find('m');
      if (end != std::string_view::npos)
        line.remove_prefix(end + 1);
    }
    return line;
  }

  // Match the browser's existing severity classes. Slot four is ESPHome's Config level.
  static constexpr std::string_view levels = "EWIDV";
  static constexpr std::array<std::string_view, 9> public_tags{
      "swipe-probe", "network", "immich", "photo-display", "frame-blitter",
      "web-settings", "firmware-update", "usb-setup", "native-release"};
  std::array<WebLogLine, capacity> entries_{};
  uint32_t sequence_{};
};

void start_web_logs();
WebLogPage read_web_logs(uint32_t after, std::span<WebLogLine> output);

}  // namespace espframe::native
