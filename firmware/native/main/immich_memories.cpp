#include "immich_memories.h"

#include <algorithm>

namespace espframe::native {
namespace {

void skip_space(std::string_view &input) {
  const auto first = input.find_first_not_of(" \t\r\n");
  input.remove_prefix(first == input.npos ? input.size() : first);
}

bool take(std::string_view &input, char token) {
  skip_space(input);
  if (!input.starts_with(token))
    return false;
  input.remove_prefix(1);
  return true;
}

std::string_view value(std::string_view &input) {
  skip_space(input);
  std::array<char, 32> closures{};
  size_t depth = 0;
  bool in_string = false;
  size_t end = 0;
  for (; end < input.size(); ++end) {
    const char c = input[end];
    if (in_string) {
      if (c == '\\') {
        if (++end == input.size())
          return {};
      } else if (c == '"') {
        in_string = false;
        if (!depth) {
          ++end;
          break;
        }
      } else if (static_cast<unsigned char>(c) < 0x20) {
        return {};
      }
    } else if (c == '"') {
      if (end && !depth)
        return {};
      in_string = true;
    } else if (c == '[' || c == '{') {
      if ((!depth && end) || depth == closures.size())
        return {};
      closures[depth++] = c == '[' ? ']' : '}';
    } else if (c == ']' || c == '}') {
      if (!depth)
        break;
      if (c != closures[--depth])
        return {};
      if (!depth) {
        ++end;
        break;
      }
    } else if (!depth && (c == ',' || c == ':' || std::string_view(" \t\r\n").find(c) != input.npos)) {
      break;
    }
  }
  if (depth || in_string)
    return {};
  const auto result = input.substr(0, end);
  input.remove_prefix(end);
  return result;
}

template <typename Visit>
bool visit_array(std::string_view input, Visit visit) {
  if (!take(input, '['))
    return false;
  if (!take(input, ']')) {
    do {
      const auto entry = value(input);
      if (entry.empty() || !visit(entry))
        return false;
    } while (take(input, ','));
    if (!take(input, ']'))
      return false;
  }
  skip_space(input);
  return input.empty();
}

bool visit_memory(std::string_view group, std::vector<ImmichPhoto> &photos, size_t limit, PhotoOrientation orientation) {
  if (!take(group, '{'))
    return false;
  std::string_view assets;
  bool on_this_day = false;
  if (!take(group, '}')) {
    do {
      const auto key = value(group);
      if (!key.starts_with('"') || !take(group, ':'))
        return false;
      const auto field = value(group);
      if (field.empty())
        return false;
      if (key == "\"assets\"")
        assets = field;
      else if (key == "\"type\"")
        on_this_day = field == "\"on_this_day\"";
    } while (take(group, ','));
    if (!take(group, '}'))
      return false;
  }
  if (!group.empty())
    return false;
  if (!on_this_day)
    return true;
  return visit_array(assets, [&](std::string_view asset) {
    constexpr size_t max_asset_bytes = 16 * 1024;
    if (photos.size() < limit && asset.size() <= max_asset_bytes) {
      const auto photo = parse_photo_asset(asset);
      if (photo && photo->may_match_orientation(orientation) &&
          std::none_of(photos.begin(), photos.end(), [&](const ImmichPhoto &other) { return other.id == photo->id; }))
        photos.push_back(*photo);
    }
    return true;
  });
}

}  // namespace

std::optional<std::vector<ImmichPhoto>> parse_memories(std::string_view json, size_t limit, PhotoOrientation orientation) {
  std::vector<ImmichPhoto> photos;
  photos.reserve(limit);
  if (!visit_array(json, [&](std::string_view group) { return visit_memory(group, photos, limit, orientation); }))
    return {};
  return photos;
}

}  // namespace espframe::native
