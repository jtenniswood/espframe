#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace espframe::native {

// A complete clip is published only after all frames were encoded. The
// allocation and ownership are held by the worker for the whole build.
class PhotoZoomPackets {
 public:
  static constexpr size_t frame_count = 11;
  static constexpr size_t byte_limit = 2 * 1024 * 1024;
  static constexpr size_t alignment = 128;
  static constexpr size_t packet_limit = 384 * 1024;

  explicit PhotoZoomPackets(std::span<uint8_t> storage) {
    if (reinterpret_cast<uintptr_t>(storage.data()) % alignment == 0)
      storage_ = storage.first(std::min(storage.size(), byte_limit));
  }

  std::span<uint8_t> destination() {
    if (ready() || offset_ >= storage_.size())
      return {};
    const auto available = std::min(packet_limit, storage_.size() - offset_);
    return storage_.subspan(offset_, available / alignment * alignment);
  }

  bool finish_frame(size_t size) {
    const auto output = destination();
    if (size < 4 || size > output.size() || output[0] != 0xff || output[1] != 0xd8 ||
        output[size - 2] != 0xff || output[size - 1] != 0xd9)
      return false;
    packets_[count_++] = {offset_, size};
    offset_ += ((size + alignment - 1) / alignment) * alignment;
    return true;
  }

  bool ready() const { return count_ == frame_count; }

  std::span<const uint8_t> frame(size_t index) const {
    if (!ready() || index >= frame_count)
      return {};
    const auto &packet = packets_[index];
    return storage_.subspan(packet.offset, packet.size);
  }

 private:
  struct Packet {
    size_t offset;
    size_t size;
  };

  std::span<uint8_t> storage_;
  std::array<Packet, frame_count> packets_{};
  size_t offset_{};
  size_t count_{};
};

}  // namespace espframe::native
