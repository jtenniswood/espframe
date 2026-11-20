#pragma once

#include "immich_filter_config.h"

namespace espframe::native {

struct PhotoFilters {
  PhotoFilters() {
    criteria.album_matching = "Any selected album";
    criteria.person_matching = "Any selected person";
    criteria.tag_matching = "Any selected tag";
  }

  ImmichFilterConfig criteria;
  std::string album_order{"Random albums"};
};

}  // namespace espframe::native
