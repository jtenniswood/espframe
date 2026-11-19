#pragma once

#include "slide_geometry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace espframe::native {

// UI task owns the index and slot states. The producer owns the pixels in an
// outstanding lease until it sends the completion back to the UI task.
class SlideCache {
 public:
  static constexpr size_t minimum_capacity = 6;
  static constexpr size_t capacity = 9;
  using Buffers = std::array<uint16_t *, capacity>;

  struct Lease {
    uint16_t *pixels;
    SlideIndex slide;
    size_t slot;
    uint32_t ticket;
  };

  explicit SlideCache(const Buffers &buffers) : buffers_(buffers) {}

  size_t size() const {
    size_t allocated = 0;
    for (const auto *buffer : buffers_)
      allocated += buffer != nullptr;
    return allocated;
  }

  const uint16_t *find(SlideIndex slide) const {
    for (size_t index = 0; index < capacity; ++index) {
      const auto &entry = this->entries_[index];
      if (entry.state == State::Ready && entry.slide == slide)
        return this->buffers_[index];
    }
    return nullptr;
  }

  std::optional<Lease> reserve(SlideIndex slide, SlideIndex keep_first, SlideIndex keep_last) {
    size_t available = capacity;
    for (size_t index = 0; index < capacity; ++index) {
      if (!this->buffers_[index])
        continue;
      const auto &entry = this->entries_[index];
      if (entry.state != State::Empty && entry.slide == slide)
        return std::nullopt;
      if (entry.state == State::Empty ||
          (available == capacity && entry.state == State::Ready &&
           (entry.slide < keep_first || entry.slide > keep_last)))
        available = index;
    }
    if (available == capacity)
      return std::nullopt;

    const auto ticket = ++this->ticket_;
    this->entries_[available] = {State::Preparing, slide, ticket};
    return Lease{this->buffers_[available], slide, available, ticket};
  }

  bool complete(const Lease &lease, bool ready) {
    if (lease.slot >= capacity)
      return false;
    auto &entry = this->entries_[lease.slot];
    if (entry.state != State::Preparing || entry.ticket != lease.ticket || entry.slide != lease.slide)
      return false;
    entry.state = ready ? State::Ready : State::Empty;
    return true;
  }

 private:
  enum class State { Empty, Preparing, Ready };
  struct Entry {
    State state{State::Empty};
    SlideIndex slide{};
    uint32_t ticket{};
  };

  Buffers buffers_;
  std::array<Entry, capacity> entries_{};
  uint32_t ticket_{};
};

}  // namespace espframe::native
