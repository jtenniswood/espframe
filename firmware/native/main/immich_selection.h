#pragma once

#include "capture_date.h"
#include "photo_history.h"
#include "photo_source.h"

#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace espframe::native {

struct ImmichPhoto {
  AssetId id{};
  PhotoMetadata metadata{};
  bool portrait{};
  bool orientation_known{};
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
                                     std::optional<std::chrono::sys_days> local_day = {}, uint32_t page = 1,
                                     bool with_location = false, CaptureDateRange dates = {});
PhotoSearchResult parse_photo_search(std::string_view json, size_t limit, bool paged, bool favorites_only,
                                     CaptureDateRange dates = {});
std::optional<ImmichPhoto> parse_memory_asset(std::string_view json);
std::string memory_request_path(std::chrono::sys_days date, unsigned index);
bool is_portrait_companion(const ImmichPhoto &primary, const ImmichPhoto &candidate);
bool can_start_portrait_pair(const ImmichPhoto &photo);
std::optional<size_t> find_pair_seed(std::span<const ImmichPhoto> photos);

}  // namespace espframe::native
