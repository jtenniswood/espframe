#pragma once

#include "photo_history.h"
#include "photo_source.h"

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

enum class ImmichSearchApi { Unknown, Legacy, Structured };

struct PhotoSearchRequest {
  std::string_view path;
  std::string body;
  bool paged{};
};

struct PhotoSearchResult {
  std::vector<ImmichPhoto> photos;
  uint32_t next_page{};
  bool valid{};
};

ImmichSearchApi parse_search_api(std::string_view json);
PhotoSearchRequest make_photo_search(size_t count, const PhotoSource &source, ImmichSearchApi api,
                                     std::optional<std::chrono::sys_days> local_day = {}, uint32_t page = 1);
PhotoSearchResult parse_photo_search(std::string_view json, size_t limit, bool paged, bool favorites_only);
bool is_portrait_companion(const ImmichPhoto &primary, const ImmichPhoto &candidate);

}  // namespace espframe::native
