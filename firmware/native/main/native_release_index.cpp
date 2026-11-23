#include "native_release_index.h"

#include "cJSON.h"

#include <algorithm>
#include <charconv>
#include <memory>

namespace espframe::native {
namespace {

constexpr uint32_t max_image_size = 4 * 1024 * 1024;

std::optional<FirmwareDigest> read_digest(const cJSON *field) {
  const auto *str = cJSON_GetStringValue(field);
  if (!str || std::string_view(str).size() != 64)
    return {};
  FirmwareDigest digest{};
  for (size_t i = 0; i < digest.size(); ++i) {
    const char *begin = str + 2 * i;
    unsigned byte{};
    const auto result = std::from_chars(begin, begin + 2, byte, 16);
    if (result.ec != std::errc{} || result.ptr != begin + 2)
      return {};
    digest[i] = byte;
  }
  return digest;
}

}  // namespace

std::optional<PreviewVersion> parse_preview_version(std::string_view version) {
  if (!version.starts_with('v') || version.size() > 31)
    return {};
  version.remove_prefix(1);
  constexpr std::array<std::string_view, 4> separators{".", ".", "-preview.", ""};
  PreviewVersion numbers{};
  for (size_t i = 0; i < separators.size(); ++i) {
    const auto separator = separators[i];
    const auto end = separator.empty() ? version.size() : version.find(separator);
    if (end == std::string_view::npos || end == 0 || (end > 1 && version.starts_with('0')))
      return {};
    const auto parsed = std::from_chars(version.data(), version.data() + end, numbers[i]);
    if (parsed.ec != std::errc{} || parsed.ptr != version.data() + end)
      return {};
    version.remove_prefix(end + separator.size());
  }
  return numbers;
}

std::optional<std::vector<NativeRelease>> read_release_index(std::string_view json, std::string_view board) {
  if (json.empty() || json.size() > max_release_index_bytes || json.find('\0') != std::string_view::npos ||
      json.find("\\u0000") != std::string_view::npos)
    return {};
  const char *end{};
  const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
      cJSON_ParseWithLengthOpts(json.data(), json.size(), &end, false), cJSON_Delete);
  if (!cJSON_IsObject(root.get()) || std::string_view(end, json.data() + json.size() - end).find_first_not_of(" \r\n\t") !=
                                         std::string_view::npos)
    return {};
  const auto *schema = cJSON_GetObjectItemCaseSensitive(root.get(), "schema");
  const auto *device = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root.get(), "board"));
  const auto *versions = cJSON_GetObjectItemCaseSensitive(root.get(), "versions");
  if (!cJSON_IsNumber(schema) || schema->valuedouble != 1 || !device || device != board ||
      !cJSON_IsArray(versions) || cJSON_GetArraySize(versions) > 5)
    return {};
  std::vector<NativeRelease> releases;
  std::optional<PreviewVersion> previous;
  const cJSON *entry;
  cJSON_ArrayForEach(entry, versions) {
    const auto *version = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entry, "version"));
    const auto parsed = version ? parse_preview_version(version) : std::nullopt;
    const auto *size = cJSON_GetObjectItemCaseSensitive(entry, "size");
    const auto sha256 = read_digest(cJSON_GetObjectItemCaseSensitive(entry, "sha256"));
    const auto elf_sha256 = read_digest(cJSON_GetObjectItemCaseSensitive(entry, "elf_sha256"));
    if (!parsed || (previous && *parsed >= *previous) || !cJSON_IsNumber(size) ||
        size->valuedouble < 288 || size->valuedouble > max_image_size ||
        static_cast<uint32_t>(size->valuedouble) != size->valuedouble || !sha256 || !elf_sha256)
      return {};
    releases.push_back({version, static_cast<uint32_t>(size->valuedouble), *sha256, *elf_sha256});
    previous = parsed;
  }
  return releases;
}

}  // namespace espframe::native
