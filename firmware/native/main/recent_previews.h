#pragma once

#include "photo_framing.h"
#include "photo_history.h"
#include "photo_orientation.h"
#include "slide_geometry.h"

#include "esp_heap_caps.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <variant>

namespace espframe::native {

// Worker-owned cache of downloaded previews and prepared slides.
// Keeping them compressed extends the useful history without more panel buffers.
class RecentPreviews {
 public:
  struct PreparedKey {
    AssetId primary;
    AssetId companion;
    DisplayMode framing{DisplayMode::Fit};
    PhotoOrientation orientation{PhotoOrientation::Any};
    ScreenRotation rotation{ScreenRotation::Normal};
    bool operator==(const PreparedKey &) const = default;
  };
  using Key = std::variant<AssetId, PreparedKey>;
  static constexpr size_t capacity = 16;
  static constexpr size_t byte_budget = 2 * 1024 * 1024;
  static constexpr size_t prepared_byte_budget = 4 * 1024 * 1024;

  std::span<const uint8_t> find(const Key &key) {
    if (auto *entry = this->find_(key)) {
      entry->touched = ++this->clock_;
      return {entry->bytes.get(), entry->length};
    }
    return {};
  }

  void remember(const Key &key, std::span<const uint8_t> data) {
    this->remember_(key, data, true);
  }

  // A completed transfer whose decode was interrupted is useful next time,
  // but it should not displace an already playable photo from the cache.
  void remember_interrupted(const Key &key, std::span<const uint8_t> data) {
    this->remember_(key, data, false);
  }

  void remember_prepared(const PreparedKey &key, std::span<const uint8_t> data) {
    // A prepared pair replaces two upstream files. Give opted-in sessions
    // more room for history while the same decoder reserve still applies.
    this->byte_limit_ = prepared_byte_budget;
    this->remember(key, data);
  }

  void discard(const Key &key) {
    if (auto *entry = this->find_(key))
      this->remove_(*entry);
  }

  void clear() {
    for (auto &entry : entries_)
      remove_(entry);
    clock_ = 0;
    byte_limit_ = byte_budget;
  }

 private:
  void remember_(const Key &key, std::span<const uint8_t> data, bool may_evict) {
    if (data.empty() || data.size() > this->byte_limit_)
      return;
    if (auto *entry = this->find_(key)) {
      entry->touched = ++this->clock_;
      entry->interrupted = entry->interrupted && !may_evict;
      return;
    }
    constexpr size_t free_for_decode = 8 * 1024 * 1024;
    while (this->used_ + data.size() > this->byte_limit_ ||
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < free_for_decode + data.size()) {
      if (!may_evict || this->used_ == 0)
        return;
      this->remove_(this->oldest_());
    }
    const auto empty = std::find_if(this->entries_.begin(), this->entries_.end(), [](const Entry &entry) {
      return !entry.bytes;
    });
    if (!may_evict && empty == this->entries_.end())
      return;
    auto &entry = empty != this->entries_.end() ? *empty : this->oldest_();
    this->remove_(entry);
    Buffer copy(static_cast<uint8_t *>(heap_caps_malloc(data.size(), MALLOC_CAP_SPIRAM)));
    if (!copy)
      return;
    std::memcpy(copy.get(), data.data(), data.size());
    this->used_ += data.size();
    entry = {key, std::move(copy), data.size(), ++this->clock_, !may_evict};
  }

  struct Release {
    void operator()(uint8_t *buffer) const { heap_caps_free(buffer); }
  };
  using Buffer = std::unique_ptr<uint8_t, Release>;

  struct Entry {
    Key key{};
    Buffer bytes{};
    size_t length{};
    uint64_t touched{};
    bool interrupted{};
  };

  Entry *find_(const Key &key) {
    for (auto &entry : this->entries_) {
      if (entry.bytes && entry.key == key)
        return &entry;
    }
    return nullptr;
  }

  Entry &oldest_() {
    return *std::min_element(this->entries_.begin(), this->entries_.end(), [](const Entry &a, const Entry &b) {
      if (!a.bytes || !b.bytes)
        return a.bytes && !b.bytes;
      if (a.interrupted != b.interrupted)
        return a.interrupted;
      return a.touched < b.touched;
    });
  }

  void remove_(Entry &entry) {
    this->used_ -= entry.length;
    entry = {};
  }

  std::array<Entry, capacity> entries_{};
  size_t byte_limit_{byte_budget};
  size_t used_{};
  uint64_t clock_{};
};

}  // namespace espframe::native
