#pragma once

#include "photo_history.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace espframe::native {

class UnavailablePreviews {
 public:
  void clear() { entries_.fill({}); }

  bool contains(const AssetId &id, int64_t now) const {
    return std::ranges::any_of(entries_, [&](const Entry &entry) {
      return now < entry.retry_after && entry.id == id;
    });
  }

  // A 200 reaches this method only when decoding failed. A 413 also
  // covers previews that exceed the frame's download budget.
  bool remember_failure(const AssetId &id, int status, int64_t now) {
    if (status != 200 && status != 404 && status != 410 && status != 413)
      return false;

    auto entry = std::ranges::find(entries_, id, &Entry::id);
    if (entry == entries_.end())
      entry = std::ranges::min_element(entries_, {}, &Entry::retry_after);
    *entry = {id, now + 30'000'000};
    return true;
  }

 private:
  struct Entry {
    AssetId id{};
    int64_t retry_after{};
  };

  std::array<Entry, 32> entries_{};
};

}  // namespace espframe::native
