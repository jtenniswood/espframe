#pragma once

#include "photo_history.h"

#include "esp_heap_caps.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <span>
#include <utility>

namespace espframe::native {

// Worker-owned cache of previews that have already decoded successfully.
// Keeping them compressed extends the useful history without more panel buffers.
class RecentPreviews {
 public:
  static constexpr size_t capacity = 16;
  static constexpr size_t byte_budget = 2 * 1024 * 1024;

  std::span<const uint8_t> find(const AssetId &id) {
    for (auto &entry : this->entries_) {
      if (entry.bytes && entry.id == id) {
        entry.touched = ++this->clock_;
        return {entry.bytes.get(), entry.length};
      }
    }
    return {};
  }

  void remember(const AssetId &id, std::span<const uint8_t> data) {
    if (data.empty() || data.size() > byte_budget || !this->find(id).empty())
      return;
    constexpr size_t free_for_decode = 8 * 1024 * 1024;
    while (this->used_ + data.size() > byte_budget ||
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < free_for_decode + data.size()) {
      if (this->used_ == 0)
        return;
      this->remove_(this->oldest_());
    }
    const auto empty = std::find_if(this->entries_.begin(), this->entries_.end(), [](const Entry &entry) {
      return !entry.bytes;
    });
    auto &entry = empty != this->entries_.end() ? *empty : this->oldest_();
    this->remove_(entry);
    Buffer copy(static_cast<uint8_t *>(heap_caps_malloc(data.size(), MALLOC_CAP_SPIRAM)));
    if (!copy)
      return;
    std::memcpy(copy.get(), data.data(), data.size());
    this->used_ += data.size();
    entry = {id, std::move(copy), data.size(), ++this->clock_};
  }

  void discard(const AssetId &id) {
    for (auto &entry : this->entries_) {
      if (entry.bytes && entry.id == id) {
        this->remove_(entry);
        return;
      }
    }
  }

 private:
  struct Release {
    void operator()(uint8_t *buffer) const { heap_caps_free(buffer); }
  };
  using Buffer = std::unique_ptr<uint8_t, Release>;

  struct Entry {
    AssetId id{};
    Buffer bytes{};
    size_t length{};
    uint64_t touched{};
  };

  Entry &oldest_() {
    return *std::min_element(this->entries_.begin(), this->entries_.end(), [](const Entry &a, const Entry &b) {
      if (!a.bytes || !b.bytes)
        return a.bytes && !b.bytes;
      return a.touched < b.touched;
    });
  }

  void remove_(Entry &entry) {
    this->used_ -= entry.length;
    entry = {};
  }

  std::array<Entry, capacity> entries_{};
  size_t used_{};
  uint64_t clock_{};
};

}  // namespace espframe::native
