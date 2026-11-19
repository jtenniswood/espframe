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
    bool replacement{};
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

  bool needs_refresh(SlideIndex slide) const {
    for (const auto &entry : this->entries_) {
      if (entry.state == State::Ready && entry.slide == slide)
        return entry.needs_refresh;
    }
    return false;
  }

  void mark_ready_for_refresh() {
    for (auto &entry : this->entries_) {
      if (entry.state == State::Ready)
        entry.needs_refresh = true;
    }
  }

  void stop_refresh(SlideIndex slide) {
    for (auto &entry : this->entries_) {
      if (entry.state == State::Ready && entry.slide == slide)
        entry.needs_refresh = false;
    }
  }

  std::optional<Lease> reserve(SlideIndex slide, SlideIndex keep_first, SlideIndex keep_last, bool replacement = false) {
    if (replacement && !find(slide))
      return std::nullopt;
    size_t empty = capacity;
    size_t evict = capacity;
    SlideIndex evict_distance{};
    for (size_t index = 0; index < capacity; ++index) {
      if (!this->buffers_[index])
        continue;
      const auto &entry = this->entries_[index];
      const bool replaces_ready = replacement && entry.state == State::Ready && entry.slide == slide;
      if (entry.state != State::Empty && entry.slide == slide && !replaces_ready)
        return std::nullopt;
      if (entry.state == State::Empty) {
        empty = index;
      } else if (entry.state == State::Ready && !replaces_ready &&
                 (entry.slide < keep_first || entry.slide > keep_last)) {
        const auto distance = entry.slide < keep_first ? keep_first - entry.slide : entry.slide - keep_last;
        if (evict == capacity || (replacement && distance > evict_distance)) {
          evict = index;
          evict_distance = distance;
        }
      }
    }
    const auto available = empty != capacity ? empty : evict;
    if (available == capacity)
      return std::nullopt;

    const auto ticket = ++this->ticket_;
    this->entries_[available] = {State::Preparing, slide, ticket};
    return Lease{this->buffers_[available], slide, available, ticket, replacement};
  }

  bool complete(const Lease &lease, bool ready) {
    if (lease.slot >= capacity)
      return false;
    auto &entry = this->entries_[lease.slot];
    if (entry.state != State::Preparing || entry.ticket != lease.ticket || entry.slide != lease.slide)
      return false;
    if (ready && lease.replacement) {
      for (auto &previous : this->entries_) {
        if (previous.state == State::Ready && previous.slide == lease.slide)
          previous.state = State::Empty;
      }
    }
    entry.state = ready ? State::Ready : State::Empty;
    return true;
  }

  void discard_other_ready_slides(SlideIndex keep) {
    for (auto &entry : this->entries_) {
      if (entry.state == State::Ready) {
        if (entry.slide != keep)
          entry.state = State::Empty;
        entry.needs_refresh = false;
      }
    }
  }

 private:
  enum class State { Empty, Preparing, Ready };
  struct Entry {
    State state{State::Empty};
    SlideIndex slide{};
    uint32_t ticket{};
    bool needs_refresh{};
  };

  Buffers buffers_;
  std::array<Entry, capacity> entries_{};
  uint32_t ticket_{};
};

}  // namespace espframe::native
