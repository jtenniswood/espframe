#pragma once

#include <charconv>
#include <cstdint>
#include <string_view>

namespace espframe::native {

struct TailnetHost {
  std::string_view hostname;
  uint32_t ipv4{};
};

constexpr std::string_view without_dns_root(std::string_view name) {
  if (name.ends_with('.'))
    name.remove_suffix(1);
  return name;
}

constexpr char lower_dns_ascii(char character) {
  return character >= 'A' && character <= 'Z' ? character + ('a' - 'A') : character;
}

constexpr bool same_dns_name(std::string_view first, std::string_view second) {
  first = without_dns_root(first);
  second = without_dns_root(second);
  if (first.size() != second.size())
    return false;
  for (size_t i = 0; i < first.size(); ++i) {
    if (lower_dns_ascii(first[i]) != lower_dns_ascii(second[i]))
      return false;
  }
  return true;
}

// The experimental photo transport accepts one Tailscale peer, addressed by
// its IPv4 address or full MagicDNS name. Ports and paths remain part of the URL.
inline TailnetHost tailnet_host(std::string_view url) {
  const auto scheme = url.starts_with("https://") ? 8U : url.starts_with("http://") ? 7U : 0U;
  if (!scheme)
    return {};
  const auto authority = url.substr(scheme, url.find_first_of("/?#", scheme) - scheme);
  if (authority.find('@') != std::string_view::npos)
    return {};
  const auto hostname = authority.substr(0, authority.find(':'));
  const auto dns_name = without_dns_root(hostname);
  constexpr std::string_view suffix = ".ts.net";
  if (dns_name.size() > suffix.size()) {
    const auto ending = dns_name.substr(dns_name.size() - suffix.size());
    if (same_dns_name(ending, suffix))
      return {hostname, 0};
  }

  auto rest = hostname;
  uint32_t address = 0;
  for (unsigned octet = 0; octet < 4; ++octet) {
    const auto dot = rest.find('.');
    if ((dot == std::string_view::npos) != (octet == 3))
      return {};
    const auto part = rest.substr(0, dot);
    if (part.empty())
      return {};
    unsigned value{};
    const auto [end, error] = std::from_chars(part.begin(), part.end(), value);
    if (error != std::errc{} || end != part.end() || value > 255)
      return {};
    address = (address << 8) | value;
    if (octet != 3)
      rest.remove_prefix(dot + 1);
  }
  return (address & 0xffc00000U) == 0x64400000U ? TailnetHost{hostname, address} : TailnetHost{};
}

}  // namespace espframe::native
