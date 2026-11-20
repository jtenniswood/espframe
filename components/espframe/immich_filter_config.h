#pragma once

#include <cstdint>
#include <string>

struct ImmichFilterConfig {
  bool albums_enabled = false;
  bool people_enabled = false;
  bool tags_enabled = false;
  bool favorites_enabled = false;
  bool rating_enabled = false;
  bool location_enabled = false;
  std::string album_ids;
  std::string person_ids;
  std::string tag_ids;
  std::string album_matching = "Any selected";
  std::string person_matching = "Any selected";
  std::string tag_matching = "Any selected";
  std::string inclusion_matching = "Match all enabled groups";
  std::string favorite_mode = "Any";
  uint8_t minimum_rating = 0;
  std::string taken_after;
  std::string taken_before;
  std::string city;
  std::string state;
  std::string country;
  std::string excluded_album_ids;
  std::string excluded_person_ids;
  std::string excluded_tag_ids;
};

struct ImmichFilterBranch {
  // Empty means there are no inclusion groups. "All" means every enabled
  // group is present; the other values identify the one sampled OR branch.
  std::string group;
  std::string album_ids;
  std::string person_ids;
  std::string tag_ids;
};
