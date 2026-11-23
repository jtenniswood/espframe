#pragma once

#include "immich_filter_config.h"

#include <array>
#include <atomic>

namespace espframe::native {

enum class ImmichSearchApi { Unknown, Legacy, Structured };
inline std::atomic<ImmichSearchApi> active_immich_search_api{ImmichSearchApi::Unknown};

struct PhotoFilters {
  PhotoFilters() {
    criteria.album_matching = "Any selected album";
    criteria.person_matching = "Any selected person";
    criteria.tag_matching = "Any selected tag";
  }

  ImmichFilterConfig criteria;
  std::string album_order{"Random albums"};
  std::array<std::string, 6> labels;
};

}  // namespace espframe::native
