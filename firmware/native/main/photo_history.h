#pragma once

#include "slide_geometry.h"

#include <algorithm>
#include <array>

namespace espframe::native {

using AssetId = std::array<char, 37>;

// Keep recent Immich IDs, while the slide cache separately owns decoded pixels.
// Slide zero is the startup scene and is never written to this history.
class PhotoHistory {
 public:
  static constexpr SlideIndex capacity = 128;

  SlideIndex first() const { return this->first_; }
  SlideIndex next() const { return this->next_; }

  AssetId *find(SlideIndex slide) {
    if (slide < this->first_ || slide >= this->next_)
      return nullptr;
    return &this->entries_[slide % capacity];
  }

  void append(const AssetId &id) {
    this->entries_[this->next_ % capacity] = id;
    ++this->next_;
    this->first_ = std::max(this->first_, this->next_ - capacity);
  }

  bool recently_seen(const AssetId &id, SlideIndex count) const {
    const auto begin = std::max(this->first_, this->next_ - count);
    for (auto slide = begin; slide < this->next_; ++slide) {
      if (this->entries_[slide % capacity] == id)
        return true;
    }
    return false;
  }

 private:
  std::array<AssetId, capacity> entries_{};
  SlideIndex first_{1};
  SlideIndex next_{1};
};

}  // namespace espframe::native
