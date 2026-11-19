#include "immich_selection.h"

#include "cJSON.h"

#include <charconv>
#include <cstdio>
#include <cstring>
#include <memory>

namespace espframe::native {
namespace {

bool valid_id(std::string_view id) {
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

std::optional<std::chrono::sys_days> read_day(const char *date) {
  if (!date || std::strlen(date) < 10 || date[4] != '-' || date[7] != '-')
    return {};
  auto read_part = [date](int first, int last) {
    unsigned value{};
    const auto out = std::from_chars(date + first, date + last, value);
    return out.ptr == date + last && out.ec == std::errc{} ? value : 0;
  };
  const auto y = read_part(0, 4);
  const auto m = read_part(5, 7);
  const auto d = read_part(8, 10);
  const std::chrono::year_month_day day{std::chrono::year(static_cast<int>(y)), std::chrono::month(m),
                                       std::chrono::day(d)};
  if (!day.ok() || y < 2 || y > 9998)
    return {};
  return std::chrono::sys_days(day);
}

std::string iso_day(std::chrono::sys_days value) {
  const std::chrono::year_month_day day{value};
  char date[24]{};
  std::snprintf(date, sizeof(date), "%04u-%02u-%02u", static_cast<unsigned>(static_cast<int>(day.year())),
                static_cast<unsigned>(day.month()), static_cast<unsigned>(day.day()));
  return date;
}

const char *get_text(const cJSON *object, const char *key) {
  return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(object, key));
}

int get_dimension(const cJSON *object, const char *key) {
  const auto *value = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!cJSON_IsNumber(value) || value->valueint != value->valuedouble || value->valueint < 1)
    return 0;
  return value->valueint;
}

}  // namespace

std::string random_photo_request(size_t count, std::optional<std::chrono::sys_days> local_day) {
  std::string request = "{\"type\":\"IMAGE\",\"size\":" + std::to_string(count);
  if (local_day) {
    request += ",\"withExif\":true";
    request += ",\"takenAfter\":\"" + iso_day(*local_day - std::chrono::days(1)) + "T00:00:00.000Z\"";
    request += ",\"takenBefore\":\"" + iso_day(*local_day + std::chrono::days(1)) + "T23:59:59.999Z\"";
  }
  return request + "}";
}

std::vector<ImmichPhoto> parse_photo_search(std::string_view json, size_t limit) {
  std::vector<ImmichPhoto> photos;
  const char *end{};
  const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> result(
      cJSON_ParseWithLengthOpts(json.data(), json.size(), &end, false), cJSON_Delete);
  if (!cJSON_IsArray(result.get()))
    return photos;
  const auto extra = std::string_view(end, json.data() + json.size() - end);
  if (extra.find_first_not_of(" \t\r\n") != std::string_view::npos)
    return photos;

  photos.reserve(limit);
  const cJSON *record;
  cJSON_ArrayForEach(record, result.get()) {
    if (photos.size() == limit)
      break;
    const char *id = get_text(record, "id");
    if (!id || !valid_id(id))
      continue;
    ImmichPhoto photo;
    std::memcpy(photo.id.data(), id, photo.id.size());
    if (std::any_of(photos.begin(), photos.end(), [&](const ImmichPhoto &prev) { return prev.id == photo.id; }))
      continue;
    const auto *exif = cJSON_GetObjectItemCaseSensitive(record, "exifInfo");
    photo.capture_day = read_day(get_text(record, "localDateTime"));
    if (!photo.capture_day)
      photo.capture_day = read_day(get_text(exif, "dateTimeOriginal"));
    int width = get_dimension(record, "width");
    int height = get_dimension(record, "height");
    if (width == 0 || height == 0) {
      width = get_dimension(exif, "exifImageWidth");
      height = get_dimension(exif, "exifImageHeight");
      if (const char *orientation = get_text(exif, "orientation"); orientation != nullptr &&
          orientation[0] >= '5' && orientation[0] <= '8' && orientation[1] == '\0')
        std::swap(width, height);
    }
    photo.portrait = width > 0 && height > width;
    photos.push_back(photo);
  }
  return photos;
}

bool is_portrait_companion(const ImmichPhoto &primary, const ImmichPhoto &candidate) {
  return candidate.id != primary.id && candidate.portrait && primary.capture_day.has_value() &&
         candidate.capture_day == primary.capture_day;
}

}  // namespace espframe::native
