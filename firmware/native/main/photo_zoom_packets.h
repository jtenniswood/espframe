#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include "esp_heap_caps.h"

namespace espframe::native {

// Retain only the aligned JPEG payloads. The worker reuses one encoder
// buffer so preparation can begin before a full clip would fit.

class PhotoZoomPackets {
 public:
  static constexpr size_t frame_count = 11;
  static constexpr size_t alignment = 128;
  static constexpr size_t byte_limit = 2 * 1024 * 1024;
  static constexpr size_t packet_limit = 384 * 1024;

  static constexpr size_t allocation_size(size_t bytes) {
    return (bytes + alignment - 1) / alignment * alignment;
  }

  bool append(std::span<const uint8_t> encoded, size_t remaining_budget) {
    if (ready() || encoded.size() < 4 || encoded.size() > packet_limit ||
        encoded[0] != 0xff || encoded[1] != 0xd8 || encoded[encoded.size() - 2] != 0xff || encoded.back() != 0xd9)
      return false;
    const auto bytes = allocation_size(encoded.size());
    if (bytes > std::min(remaining_budget, byte_limit - used_))
      return false;
    auto &packet = packets_[count_];
    packet.data.reset(static_cast<uint8_t *>(heap_caps_aligned_alloc(alignment, bytes,
                                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
    if (!packet.data)
      return false;
    std::memcpy(packet.data.get(), encoded.data(), encoded.size());
    packet.size = encoded.size();
    used_ += bytes;
    ++count_;
    return true;
  }

  size_t prepared_frames() const { return count_; }
  bool ready() const { return count_ == frame_count; }
  size_t used_bytes() const { return used_; }
  std::span<const uint8_t> frame(size_t index) const {
    if (!ready() || index >= count_)
      return {};
    const auto &packet = packets_[index];
    return {packet.data.get(), packet.size};
  }

 private:
  struct Packet {
    std::unique_ptr<uint8_t, decltype(&heap_caps_free)> data{nullptr, heap_caps_free};
    size_t size{};
  };
  std::array<Packet, frame_count> packets_{};
  size_t used_{};
  size_t count_{};
};

}  // namespace espframe::native
