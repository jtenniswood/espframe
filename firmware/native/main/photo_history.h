#pragma once

#include "photo_metadata.h"
#include "photo_orientation.h"
#include "slide_geometry.h"

#include <algorithm>
#include <array>

namespace espframe::native {

using AssetId = std::array<char, 37>;

struct PhotoSlide {
  AssetId primary{};
  AssetId companion{};
  PhotoMetadata metadata{};
  PhotoOrientation thumbnail_orientation{PhotoOrientation::Any};
};

// Keep recent Immich IDs, while the slide cache separately owns decoded pixels.
// Slide zero is the startup scene and is never written to this history.
class PhotoHistory {
 public:
  static constexpr SlideIndex capacity = 128;

  SlideIndex first() const { return this->first_; }
  SlideIndex next() const { return this->next_; }

  void start_at(SlideIndex slide) { this->first_ = this->next_ = slide; }

  PhotoSlide *find(SlideIndex slide) {
    if (slide < this->first_ || slide >= this->next_)
      return nullptr;
    return &this->entries_[slide % capacity];
  }

  void append(const PhotoSlide &photo) {
    this->entries_[this->next_ % capacity] = photo;
    ++this->next_;
    this->first_ = std::max(this->first_, this->next_ - capacity);
  }

  bool recently_seen(const AssetId &id, SlideIndex count) const {
    const auto begin = std::max(this->first_, this->next_ - count);
    for (auto slide = begin; slide < this->next_; ++slide) {
      const auto &entry = this->entries_[slide % capacity];
      if (entry.primary == id || entry.companion == id)
        return true;
    }
    return false;
  }

 private:
  std::array<PhotoSlide, capacity> entries_{};
  SlideIndex first_{1};
  SlideIndex next_{1};
};

}  // namespace espframe::native
