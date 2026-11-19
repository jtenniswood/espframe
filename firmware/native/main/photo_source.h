#pragma once

#include <string>
#include <string_view>

namespace espframe::native {

enum class PhotoSourceMode { Library, Favorites, Album };

struct PhotoSource {
  PhotoSourceMode mode{PhotoSourceMode::Library};
  std::string album_id;
};

inline const char *photo_source_name(PhotoSourceMode mode) {
  switch (mode) {
    case PhotoSourceMode::Library:
      return "all";
    case PhotoSourceMode::Favorites:
      return "favorites";
    case PhotoSourceMode::Album:
      return "album";
  }
  return "all";
}

inline bool is_immich_id(std::string_view id) {
  if (id.size() != 36)
    return false;
  for (size_t i = 0; i < id.size(); ++i) {
    const char c = id[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (c != '-')
        return false;
    } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

}  // namespace espframe::native
