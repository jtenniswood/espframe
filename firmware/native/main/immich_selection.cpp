#include "immich_selection.h"

#include "cJSON.h"

#include <charconv>
#include <cstdio>
#include <cstring>
#include <memory>

namespace espframe::native {
namespace {

using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

Json parse_json(std::string_view json) {
  const char *end{};
  Json result(cJSON_ParseWithLengthOpts(json.data(), json.size(), &end, false), cJSON_Delete);
  if (result && std::string_view(end, json.data() + json.size() - end).find_first_not_of(" \t\r\n") !=
                    std::string_view::npos)
    result.reset();
  return result;
}

std::optional<std::chrono::sys_days> read_day(const char *date) {
  return date ? parse_calendar_day(std::string_view(date).substr(0, 10)) : std::nullopt;
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

template <size_t Size>
void read_place(const cJSON *exif, const char *key, std::array<char, Size> &place) {
  if (const auto *value = get_text(exif, key)) {
    const std::string_view input(value, strnlen(value, Size));
    const auto first = input.find_first_not_of(" \t\r\n");
    if (input.size() < Size && first != input.npos) {
      const auto length = input.find_last_not_of(" \t\r\n") - first + 1;
      std::memcpy(place.data(), input.data() + first, length);
    }
  }
}

int get_integer(const cJSON *object, const char *key, int fallback) {
  const auto *value = cJSON_GetObjectItemCaseSensitive(object, key);
  return cJSON_IsNumber(value) && value->valueint == value->valuedouble ? value->valueint : fallback;
}

uint32_t next_metadata_page(const cJSON *assets) {
  const auto *field = cJSON_GetObjectItemCaseSensitive(assets, "nextPage");
  if (const auto *value = cJSON_GetStringValue(field)) {
    uint32_t page{};
    const auto *end = value + std::strlen(value);
    const auto parsed = std::from_chars(value, end, page);
    return parsed.ec == std::errc{} && parsed.ptr == end ? page : 0;
  }
  const int page = get_integer(assets, "nextPage", 0);
  return page > 0 ? static_cast<uint32_t>(page) : 0;
}

std::optional<ImmichPhoto> read_asset(const cJSON *record) {
  const char *id = get_text(record, "id");
  if (!id || !is_immich_id(id))
    return {};
  ImmichPhoto photo;
  std::memcpy(photo.id.data(), id, photo.id.size());
  const auto *exif = cJSON_GetObjectItemCaseSensitive(record, "exifInfo");
  photo.metadata.capture_day = read_day(get_text(record, "localDateTime"));
  if (!photo.metadata.capture_day)
    photo.metadata.capture_day = read_day(get_text(exif, "dateTimeOriginal"));
  read_place(exif, "city", photo.metadata.city);
  read_place(exif, "country", photo.metadata.country);
  int width = get_integer(record, "width", 0);
  int height = get_integer(record, "height", 0);
  if (width <= 0 || height <= 0) {
    width = get_integer(exif, "exifImageWidth", 0);
    height = get_integer(exif, "exifImageHeight", 0);
    if (const char *orientation = get_text(exif, "orientation"); orientation != nullptr &&
        orientation[0] >= '5' && orientation[0] <= '8' && orientation[1] == '\0')
      std::swap(width, height);
  }
  photo.portrait = width > 0 && height > width;
  return photo;
}

}  // namespace

ImmichSearchApi parse_search_api(std::string_view json) {
  const auto version = parse_json(json);
  const int major = get_integer(version.get(), "major", -1);
  const int minor = get_integer(version.get(), "minor", -1);
  if (major < 2 || minor < 0)
    return ImmichSearchApi::Unknown;
  return major > 3 || (major == 3 && minor >= 2) ? ImmichSearchApi::Structured : ImmichSearchApi::Legacy;
}

PhotoSearchRequest make_photo_search(size_t count, const PhotoSource &source, ImmichSearchApi api,
                                     std::optional<std::chrono::sys_days> local_day, uint32_t page,
                                     bool with_location, CaptureDateRange dates) {
  if (source.mode == PhotoSourceMode::Memories)
    return {};
  const bool album = source.mode == PhotoSourceMode::Album;
  const bool favorites = source.mode == PhotoSourceMode::Favorites;
  if ((album || favorites) && api == ImmichSearchApi::Unknown)
    return {};
  if (album && !is_immich_id(source.album_id))
    return {};
  if (local_day) {
    if (!dates.includes(local_day))
      return {};
    dates = {local_day, local_day};
  }

  const bool structured = api == ImmichSearchApi::Structured;
  const bool paged = album && !structured;
  PhotoSearchRequest request{paged ? "/search/metadata" : "/search/random", "{", paged};
  if (!structured)
    request.body += "\"type\":\"IMAGE\",";
  request.body += "\"size\":" + std::to_string(count);
  if (paged)
    request.body += ",\"page\":" + std::to_string(page);
  if (local_day || with_location)
    request.body += ",\"withExif\":true";
  if (structured) {
    request.body += ",\"filter\":{\"type\":{\"eq\":\"IMAGE\"}";
  }
  if (album) {
    request.body += ",\"albumIds\":";
    if (structured)
      request.body += "{\"any\":";
    request.body += "[\"" + source.album_id + "\"]";
    if (structured)
      request.body += "}";
  }
  if (favorites)
    request.body += structured ? ",\"isFavorite\":{\"eq\":true}" : ",\"isFavorite\":true";
  if (dates.active()) {
    // Immich searches UTC instants. Include adjacent days for timezone
    // offsets, then check the recorded local capture day after parsing.
    if (structured)
      request.body += ",\"takenAt\":{";
    if (dates.from) {
      request.body += structured ? "\"gte\":\"" : ",\"takenAfter\":\"";
      request.body += iso_day(*dates.from - std::chrono::days(1)) + "T00:00:00.000Z\"";
    }
    if (dates.through) {
      request.body += structured ? (dates.from ? ",\"lte\":\"" : "\"lte\":\"") : ",\"takenBefore\":\"";
      request.body += iso_day(*dates.through + std::chrono::days(1)) + "T23:59:59.999Z\"";
    }
    if (structured)
      request.body += "}";
  }
  if (structured)
    request.body += "}";
  request.body += "}";
  return request;
}

PhotoSearchResult parse_photo_search(std::string_view json, size_t limit, bool paged, bool favorites_only,
                                     CaptureDateRange dates) {
  PhotoSearchResult result;
  const auto document = parse_json(json);
  const auto *assets = paged ? cJSON_GetObjectItemCaseSensitive(document.get(), "assets") : nullptr;
  const auto *items = paged ? cJSON_GetObjectItemCaseSensitive(assets, "items") : document.get();
  if (!cJSON_IsArray(items))
    return result;

  result.valid = true;
  result.next_page = paged ? next_metadata_page(assets) : 0;
  auto &photos = result.photos;
  photos.reserve(limit);
  const cJSON *record;
  cJSON_ArrayForEach(record, items) {
    if (photos.size() == limit)
      break;
    if (favorites_only && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(record, "isFavorite")))
      continue;
    const auto photo = read_asset(record);
    if (photo && dates.includes(photo->metadata.capture_day) &&
        std::none_of(photos.begin(), photos.end(), [&](const ImmichPhoto &prev) { return prev.id == photo->id; }))
      photos.push_back(*photo);
  }
  return result;
}

std::optional<ImmichPhoto> parse_memory_asset(std::string_view json) {
  const auto record = parse_json(json);
  const auto *type = get_text(record.get(), "type");
  if (!type || std::string_view(type) != "IMAGE")
    return {};
  return read_asset(record.get());
}

std::string memory_request_path(std::chrono::sys_days date, unsigned index) {
  const int step = (index + 1) / 2;
  const auto offset = std::chrono::days(index % 2 ? -step : step);
  return "/memories?type=on_this_day&for=" + iso_day(date + offset);
}

bool is_portrait_companion(const ImmichPhoto &primary, const ImmichPhoto &candidate) {
  return candidate.id != primary.id && candidate.portrait && primary.metadata.capture_day.has_value() &&
         candidate.metadata.capture_day == primary.metadata.capture_day;
}

}  // namespace espframe::native
