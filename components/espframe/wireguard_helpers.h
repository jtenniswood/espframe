#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

// Validation and normalisation for the WireGuard settings saved from the web
// UI. The firmware applies them once at boot, so every value is checked here
// before it reaches the WireGuard component.

static constexpr size_t ESPFRAME_WIREGUARD_MAX_ALLOWED_IPS = 4;
static constexpr size_t ESPFRAME_WIREGUARD_KEY_LENGTH = 44;
static constexpr size_t ESPFRAME_WIREGUARD_ENDPOINT_MAX_LENGTH = 253;

inline std::string espframe_wireguard_trim(const std::string &value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) start++;
  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) end--;
  return value.substr(start, end - start);
}

// Parses a dotted-quad IPv4 address into host byte order.
inline bool espframe_parse_ipv4(const std::string &value, uint32_t &out) {
  uint32_t result = 0;
  size_t index = 0;
  for (int part = 0; part < 4; part++) {
    if (index >= value.size() || !std::isdigit(static_cast<unsigned char>(value[index]))) return false;
    uint32_t octet = 0;
    size_t digits = 0;
    while (index < value.size() && std::isdigit(static_cast<unsigned char>(value[index]))) {
      octet = octet * 10 + static_cast<uint32_t>(value[index] - '0');
      index++;
      if (++digits > 3 || octet > 255) return false;
    }
    result = (result << 8) | octet;
    if (part < 3) {
      if (index >= value.size() || value[index] != '.') return false;
      index++;
    }
  }
  if (index != value.size()) return false;
  out = result;
  return true;
}

inline std::string espframe_format_ipv4(uint32_t address) {
  return std::to_string((address >> 24) & 0xFF) + "." + std::to_string((address >> 16) & 0xFF) + "." +
         std::to_string((address >> 8) & 0xFF) + "." + std::to_string(address & 0xFF);
}

inline uint32_t espframe_ipv4_prefix_mask(unsigned int prefix) {
  return prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - prefix));
}

// A netmask is valid when it is a contiguous run of ones followed by zeros.
inline bool espframe_is_valid_ipv4_netmask(const std::string &value) {
  uint32_t mask = 0;
  if (!espframe_parse_ipv4(value, mask)) return false;
  const uint32_t inverted = ~mask;
  return (inverted & (inverted + 1)) == 0;
}

inline bool espframe_is_valid_wireguard_address(const std::string &value) {
  uint32_t address = 0;
  return espframe_parse_ipv4(value, address) && address != 0 && address != 0xFFFFFFFFu;
}

inline bool espframe_is_valid_wireguard_port(long port) { return port >= 1 && port <= 65535; }

// WireGuard keys are 32 bytes of base64. The final character of a canonical
// 44-character encoding only carries four bits, so it is restricted to the same
// set the ESPHome component accepts.
inline bool espframe_is_valid_wireguard_key(const std::string &value) {
  if (value.size() != ESPFRAME_WIREGUARD_KEY_LENGTH) return false;
  for (size_t i = 0; i < 42; i++) {
    const char c = value[i];
    const bool base64 = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        c == '+' || c == '/';
    if (!base64) return false;
  }
  static const std::string LAST = "AEIMQUYcgkosw480";
  return LAST.find(value[42]) != std::string::npos && value[43] == '=';
}

// The endpoint is a hostname or IPv4 address without scheme, port or path; the
// port has its own setting.
inline bool espframe_is_valid_wireguard_endpoint(const std::string &value) {
  if (value.empty() || value.size() > ESPFRAME_WIREGUARD_ENDPOINT_MAX_LENGTH) return false;
  if (value.front() == '.' || value.front() == '-' || value.back() == '.' || value.back() == '-') return false;
  for (char c : value) {
    const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                         c == '.' || c == '-';
    if (!allowed) return false;
  }
  return value.find("..") == std::string::npos;
}

using EspframeWireguardAllowedIp = std::pair<std::string, std::string>;

// Parses a comma, semicolon or space separated list of IPv4 networks such as
// "192.168.1.0/24, 10.0.0.1". A bare address means a single host. Host bits are
// cleared so the saved list is canonical. Returns false on any invalid entry or
// when more than ESPFRAME_WIREGUARD_MAX_ALLOWED_IPS networks are given.
inline bool espframe_parse_wireguard_allowed_ips(const std::string &value,
                                                 std::vector<EspframeWireguardAllowedIp> &out) {
  out.clear();
  std::string normalized = value;
  for (char &c : normalized) {
    if (c == ',' || c == ';') c = ' ';
  }
  size_t index = 0;
  while (index < normalized.size()) {
    while (index < normalized.size() && std::isspace(static_cast<unsigned char>(normalized[index]))) index++;
    if (index >= normalized.size()) break;
    size_t end = index;
    while (end < normalized.size() && !std::isspace(static_cast<unsigned char>(normalized[end]))) end++;
    const std::string token = normalized.substr(index, end - index);
    index = end;

    std::string address_text = token;
    unsigned int prefix = 32;
    const size_t slash = token.find('/');
    if (slash != std::string::npos) {
      address_text = token.substr(0, slash);
      const std::string prefix_text = token.substr(slash + 1);
      if (prefix_text.empty() || prefix_text.size() > 2) return false;
      for (char c : prefix_text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
      }
      prefix = static_cast<unsigned int>(std::atoi(prefix_text.c_str()));
      if (prefix > 32) return false;
    }
    uint32_t address = 0;
    if (!espframe_parse_ipv4(address_text, address)) return false;
    const uint32_t mask = espframe_ipv4_prefix_mask(prefix);
    if (out.size() >= ESPFRAME_WIREGUARD_MAX_ALLOWED_IPS) return false;
    out.emplace_back(espframe_format_ipv4(address & mask), espframe_format_ipv4(mask));
  }
  return !out.empty();
}

// Canonical "a.b.c.d/len" form of an allowed-IPs list, or "" when invalid.
inline std::string espframe_normalize_wireguard_allowed_ips(const std::string &value) {
  std::vector<EspframeWireguardAllowedIp> parsed;
  if (!espframe_parse_wireguard_allowed_ips(value, parsed)) return "";
  std::string result;
  for (const auto &entry : parsed) {
    uint32_t mask = 0;
    espframe_parse_ipv4(entry.second, mask);
    unsigned int prefix = 0;
    while (mask & 0x80000000u) {
      prefix++;
      mask <<= 1;
    }
    if (!result.empty()) result += ", ";
    result += entry.first + "/" + std::to_string(prefix);
  }
  return result;
}

struct EspframeWireguardConfig {
  std::string endpoint;
  uint16_t port{51820};
  std::string address;
  std::string netmask;
  std::vector<EspframeWireguardAllowedIp> allowed_ips;
  std::string peer_public_key;
  std::string private_key;
};

// Validates the saved settings as a set. On failure `error` names the first
// field that must be fixed, so the boot log can say why the tunnel stayed off.
inline bool espframe_build_wireguard_config(const std::string &endpoint, long port, const std::string &address,
                                            const std::string &netmask, const std::string &allowed_ips,
                                            const std::string &peer_public_key, const std::string &private_key,
                                            EspframeWireguardConfig &out, std::string &error) {
  const std::string trimmed_endpoint = espframe_wireguard_trim(endpoint);
  if (!espframe_is_valid_wireguard_endpoint(trimmed_endpoint)) {
    error = "endpoint";
    return false;
  }
  if (!espframe_is_valid_wireguard_port(port)) {
    error = "port";
    return false;
  }
  const std::string trimmed_address = espframe_wireguard_trim(address);
  if (!espframe_is_valid_wireguard_address(trimmed_address)) {
    error = "address";
    return false;
  }
  const std::string trimmed_netmask = espframe_wireguard_trim(netmask);
  if (!espframe_is_valid_ipv4_netmask(trimmed_netmask)) {
    error = "netmask";
    return false;
  }
  std::vector<EspframeWireguardAllowedIp> parsed_allowed_ips;
  if (!espframe_parse_wireguard_allowed_ips(allowed_ips, parsed_allowed_ips)) {
    error = "allowed_ips";
    return false;
  }
  const std::string trimmed_peer_key = espframe_wireguard_trim(peer_public_key);
  if (!espframe_is_valid_wireguard_key(trimmed_peer_key)) {
    error = "peer_public_key";
    return false;
  }
  const std::string trimmed_private_key = espframe_wireguard_trim(private_key);
  if (!espframe_is_valid_wireguard_key(trimmed_private_key)) {
    error = "private_key";
    return false;
  }
  out.endpoint = trimmed_endpoint;
  out.port = static_cast<uint16_t>(port);
  out.address = trimmed_address;
  out.netmask = trimmed_netmask;
  out.allowed_ips = std::move(parsed_allowed_ips);
  out.peer_public_key = trimmed_peer_key;
  out.private_key = trimmed_private_key;
  return true;
}

inline bool espframe_ipv4_in_network(uint32_t ip, const std::string &network, const std::string &netmask) {
  uint32_t network_address = 0;
  uint32_t mask = 0;
  if (!espframe_parse_ipv4(network, network_address) || !espframe_parse_ipv4(netmask, mask)) return false;
  return (ip & mask) == (network_address & mask);
}

// The settings applied at boot. The WireGuard component keeps raw pointers into
// these strings, so they live for the rest of the run and are written once.
inline EspframeWireguardConfig &espframe_wireguard_active_config() {
  static EspframeWireguardConfig config;
  return config;
}

inline bool &espframe_wireguard_active() {
  static bool active = false;
  return active;
}

// Whether the tunnel would carry traffic to `ip`: lwIP only sends it into the
// tunnel when it falls inside the tunnel interface's netmask, and the peer only
// accepts it when it falls inside the allowed IPs.
inline std::string espframe_wireguard_coverage(const EspframeWireguardConfig &config, uint32_t ip) {
  const bool in_netmask = espframe_ipv4_in_network(ip, config.address, config.netmask);
  bool in_allowed_ips = false;
  for (const auto &allowed : config.allowed_ips) {
    if (espframe_ipv4_in_network(ip, allowed.first, allowed.second)) in_allowed_ips = true;
  }
  return std::string("inside tunnel netmask: ") + (in_netmask ? "yes" : "no") +
         ", inside allowed IPs: " + (in_allowed_ips ? "yes" : "no");
}
