#include "c6_firmware_manifest.h"

#include "cJSON.h"

#include <charconv>
#include <memory>

namespace espframe::native {

std::optional<HostedVersion> parse_hosted_version(std::string_view text) {
  HostedVersion version{};
  if (text.empty() || text.size() > 32)
    return {};
  for (size_t i = 0; i < version.size(); ++i) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), version[i]);
    if (result.ec != std::errc{})
      return {};
    text.remove_prefix(result.ptr - text.data());
    if (i + 1 == version.size())
      return text.empty() ? std::optional{version} : std::nullopt;
    if (!text.starts_with('.'))
      return {};
    text.remove_prefix(1);
  }
  return {};
}

std::string hosted_version_text(const HostedVersion &version) {
  return std::to_string(version[0]) + "." + std::to_string(version[1]) + "." + std::to_string(version[2]);
}

std::optional<C6Release> read_c6_release(std::string_view json, const HostedVersion &host) {
  if (json.empty() || json.size() > c6_manifest_limit || json.find('\0') != json.npos)
    return {};
  const std::string terminated(json);
  const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
      cJSON_ParseWithOpts(terminated.c_str(), nullptr, true), cJSON_Delete);
  const auto *versions = cJSON_GetObjectItemCaseSensitive(root.get(), "versions");
  if (!cJSON_IsArray(versions))
    return {};
  std::optional<C6Release> best;
  const cJSON *entry;
  cJSON_ArrayForEach(entry, versions) {
    const auto *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entry, "version"));
    const auto version = name ? parse_hosted_version(name) : std::nullopt;
    if (!version || *version > host || (best && *version < best->version))
      continue;
    const auto *url = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entry, "url"));
    const auto *hash = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entry, "sha256"));
    auto expected_url = std::string(hosted_firmware_origin) + "v" + hosted_version_text(*version) + "/network_adapter_esp32c6.bin";
    if (!url || url != expected_url || !hash || std::string_view(hash).size() != 64)
      continue;
    C6Release candidate{*version, std::move(expected_url), {}};
    bool valid = true;
    for (size_t i = 0; i < candidate.sha256.size(); ++i) {
      const auto *start = hash + 2 * i;
      const auto result = std::from_chars(start, start + 2, candidate.sha256[i], 16);
      valid &= result.ec == std::errc{} && result.ptr == start + 2;
    }
    if (!valid)
      continue;
    if (best && best->version == candidate.version && best->sha256 != candidate.sha256)
      return {};
    best = std::move(candidate);
  }
  return best;
}

}  // namespace espframe::native
