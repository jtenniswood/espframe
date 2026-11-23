#pragma once

#include "capture_date.h"
#include "photo_filters.h"
#include "photo_history.h"
#include "photo_orientation.h"
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

  bool may_match_orientation(PhotoOrientation orientation) const {
    return !orientation_known || matches_photo_orientation(orientation, portrait);
  }

  bool can_stage_unpaired(PhotoOrientation orientation, bool pairs_portraits) const {
    return orientation_known && may_match_orientation(orientation) && (!portrait || !pairs_portraits);
  }
};

inline constexpr size_t photo_search_batch = 16;

enum class PhotoFilterStatus { Ready, Incomplete, Unsupported };

struct PhotoSearchRequest {
  std::string_view path;
  std::string body;
  bool paged{};
  CaptureDateRange dates;
};

struct PhotoSearchResult {
  std::vector<ImmichPhoto> photos;
  uint32_t next_page{};
  bool valid{};

  bool has_next_page(uint32_t current_page) const { return valid && next_page > current_page; }
};

ImmichSearchApi parse_search_api(std::string_view json);
PhotoSearchRequest make_photo_search(size_t count, const PhotoSource &source, ImmichSearchApi api,
                                     std::optional<std::chrono::sys_days> local_day = {}, uint32_t page = 1,
                                     bool with_location = false, CaptureDateRange dates = {}, unsigned pairing_days = 0);
PhotoFilterStatus photo_filter_status(const PhotoFilters &filters, ImmichSearchApi api);
ImmichFilterBranch next_photo_filter_branch(const PhotoFilters &filters, ImmichSearchApi api,
                                           uint8_t &next_group, int &next_album);
bool photo_filter_uses_metadata(const ImmichFilterBranch &branch, ImmichSearchApi api);
PhotoSearchRequest make_filtered_photo_search(size_t count, const PhotoFilters &filters, const ImmichFilterBranch &branch,
                                              ImmichSearchApi api, std::optional<std::chrono::sys_days> local_day,
                                              uint32_t page, bool with_location, CaptureDateRange dates,
                                              unsigned pairing_days);
PhotoSearchResult parse_photo_search(std::string_view json, size_t limit, bool paged, bool favorites_only,
                                     CaptureDateRange dates = {}, bool exclude_favorites = false,
                                     PhotoOrientation orientation = PhotoOrientation::Any);
std::optional<ImmichPhoto> parse_photo_asset(std::string_view json);
std::string memory_request_path(std::chrono::sys_days date, unsigned index);
bool is_portrait_companion(const ImmichPhoto &primary, const ImmichPhoto &candidate, unsigned pairing_days = 0);
bool can_start_portrait_pair(const ImmichPhoto &photo);
bool can_play_with_required_pairs(const ImmichPhoto &photo, PhotoOrientation orientation);
std::optional<size_t> find_pair_or_landscape_seed(std::span<const ImmichPhoto> photos, unsigned pairing_days,
                                                  const AssetId &exclude, PhotoOrientation orientation);

}  // namespace espframe::native
