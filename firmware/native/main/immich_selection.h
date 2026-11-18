#pragma once

#include "photo_history.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace espframe::native {

struct ImmichPhoto {
  AssetId id{};
  std::optional<std::chrono::sys_days> capture_day;
  bool portrait{};
};

inline constexpr size_t photo_search_batch = 16;

// Pair searches use a widened UTC window and verify the local calendar day
// from each result. Immich's server-side date filter uses fileCreatedAt.
std::string random_photo_request(size_t count, std::optional<std::chrono::sys_days> local_day = {});
std::vector<ImmichPhoto> parse_photo_search(std::string_view json, size_t limit);
bool is_portrait_companion(const ImmichPhoto &primary, const ImmichPhoto &candidate);

}  // namespace espframe::native
