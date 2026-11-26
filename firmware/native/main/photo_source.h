#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace espframe::native {

enum class PhotoSourceMode { Library, Favorites, Album, Memories, Custom };
enum class PhotoPreset : uint8_t { All, Favorites, Album, Person, Tag, Memories, Custom };
inline constexpr std::array<std::string_view, 7> photo_presets{
    "All Photos", "Favorites", "Album", "Person", "Tag", "Memories", "Custom"};
inline std::atomic<PhotoPreset> active_photo_preset{PhotoPreset::All};
inline std::atomic<uint32_t> photo_source_revision{};
inline std::atomic<uint32_t> photo_selection_revision{};

inline const char *photo_preset_name(PhotoPreset preset) {
  return photo_presets[static_cast<size_t>(preset)].data();
}

inline std::optional<PhotoPreset> parse_photo_preset(std::string_view name) {
  for (size_t index = 0; index < photo_presets.size(); ++index) {
    if (photo_presets[index] == name)
      return static_cast<PhotoPreset>(index);
  }
  return {};
}
enum class PhotoSourceStatus : uint8_t {
  None, WaitingForTime, MemoriesEmpty, MemoriesUnavailable, IncompleteFilters, UnsupportedFilters, FiltersUnavailable,
  NoPhotos, WaitingForPair
};

inline constexpr std::array<std::string_view, 5> memory_windows{
    "Same Day", "Within 1 Day", "Within 2 Days", "Within 3 Days", "Within 7 Days"};
inline constexpr std::array<uint8_t, 5> memory_window_days{0, 1, 2, 3, 7};
inline constexpr int portrait_pairing_any_time = -1;
inline constexpr std::array<std::string_view, 9> portrait_pairing_ranges{
    "Same Day", "Within 1 Day", "Within 2 Days", "Within 3 Days", "Within 5 Days",
    "Within 7 Days", "Within 14 Days", "Within 30 Days", "Any Time"};
inline constexpr std::array<int, portrait_pairing_ranges.size()> portrait_pairing_range_days{
    0, 1, 2, 3, 5, 7, 14, 30, portrait_pairing_any_time};

struct PhotoSource {
  PhotoSourceMode mode{PhotoSourceMode::Library};
  std::string album_id;
  uint8_t memory_window{2};
  bool memory_fallback{true};
  std::optional<PhotoPreset> legacy_preset{};

  PhotoPreset preset() const {
    switch (mode) {
      case PhotoSourceMode::Library: return PhotoPreset::All;
      case PhotoSourceMode::Favorites: return PhotoPreset::Favorites;
      case PhotoSourceMode::Album: return PhotoPreset::Album;
      case PhotoSourceMode::Memories: return PhotoPreset::Memories;
      case PhotoSourceMode::Custom: return legacy_preset.value_or(PhotoPreset::Custom);
    }
    return PhotoPreset::All;
  }
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
    case PhotoSourceMode::Custom:
      return "custom";
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
