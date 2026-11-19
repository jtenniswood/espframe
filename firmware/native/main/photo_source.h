#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace espframe::native {

enum class PhotoSourceMode { Library, Favorites, Album, Memories };
enum class MemoriesStatus : uint8_t { None, WaitingForTime, Empty, Unavailable };

inline constexpr std::array<std::string_view, 5> memory_windows{
    "Same Day", "Within 1 Day", "Within 2 Days", "Within 3 Days", "Within 7 Days"};
inline constexpr std::array<uint8_t, 5> memory_window_days{0, 1, 2, 3, 7};
inline constexpr std::array<std::string_view, 3> portrait_pairing_ranges{
    "Same Day", "Within 1 Day", "Within 2 Days"};

struct PhotoSource {
  PhotoSourceMode mode{PhotoSourceMode::Library};
  std::string album_id;
  uint8_t memory_window{2};
  bool memory_fallback{true};
};

inline const char *photo_source_name(PhotoSourceMode mode) {
  switch (mode) {
    case PhotoSourceMode::Library:
      return "all";
    case PhotoSourceMode::Favorites:
      return "favorites";
    case PhotoSourceMode::Album:
      return "album";
    case PhotoSourceMode::Memories:
      return "memories";
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
