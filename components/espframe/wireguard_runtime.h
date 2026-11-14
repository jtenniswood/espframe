#pragma once

#include <cstdio>
#include <string>

#include "date_utils.h"
#include "wireguard_helpers.h"

#ifdef USE_ESP32
#include "esphome/core/helpers.h"
#include "lwip/dns.h"
#include "lwip/ip4.h"
#include "lwip/netif.h"
#endif

// lwIP picks the first interface in netif_list whose subnet contains the
// destination. netif_add() puts every new interface at the head of that list,
// and the WireGuard component adds its interface after Wi-Fi is up. When the
// tunnel address and the Wi-Fi network overlap (for example a frame at home on
// the same LAN as Immich), the tunnel therefore wins and local traffic is sent
// into the VPN. Moving the tunnel interface to the tail makes Wi-Fi win whenever
// it already reaches the destination, and the tunnel still carries everything
// Wi-Fi does not.
//
// Moves the first node matching `is_tunnel` to the end of the singly linked
// list that starts at `head` and returns the (possibly new) head. `moved` tells
// the caller whether the order changed. Pure so it can be tested on the host.
template <typename Node, typename IsTunnel>
inline Node *espframe_move_to_tail(Node *head, IsTunnel is_tunnel, bool &moved) {
  moved = false;
  Node *previous = nullptr;
  Node *current = head;
  while (current != nullptr && !is_tunnel(current)) {
    previous = current;
    current = current->next;
  }
  if (current == nullptr || current->next == nullptr) return head;
  Node *tail = current->next;
  if (previous != nullptr) {
    previous->next = current->next;
  } else {
    head = current->next;
  }
  while (tail->next != nullptr) tail = tail->next;
  tail->next = current;
  current->next = nullptr;
  moved = true;
  return head;
}

// Host part of an http(s) URL such as "https://photos.example.com:2283".
inline std::string espframe_wireguard_url_host(const std::string &url) {
  const size_t scheme = url.find("://");
  return extract_url_host(scheme == std::string::npos ? url : url.substr(scheme + 3));
}

#ifdef USE_ESP32

inline bool espframe_is_wireguard_netif(const struct netif *netif) {
  return netif != nullptr && netif->name[0] == 'w' && netif->name[1] == 'g';
}

// Keeps the tunnel interface behind the others. The component removes and
// re-adds it on every reconnect, so this must be repeated. Returns true when the
// order changed.
inline bool espframe_wireguard_prefer_other_netifs() {
  esphome::LwIPLock lock;
  bool moved = false;
  netif_list = espframe_move_to_tail(
      netif_list, [](const struct netif *netif) { return espframe_is_wireguard_netif(netif); }, moved);
  return moved;
}

inline std::string espframe_describe_netif(const struct netif *netif) {
  char address[IP4ADDR_STRLEN_MAX];
  char netmask[IP4ADDR_STRLEN_MAX];
  ip4addr_ntoa_r(netif_ip4_addr(netif), address, sizeof(address));
  ip4addr_ntoa_r(netif_ip4_netmask(netif), netmask, sizeof(netmask));
  char text[80];
  snprintf(text, sizeof(text), "%c%c%u %s/%s", netif->name[0], netif->name[1], (unsigned) netif->num, address,
           netmask);
  return text;
}

// Interfaces with an address, in the order lwIP searches them.
inline std::string espframe_describe_netifs() {
  esphome::LwIPLock lock;
  std::string result;
  for (struct netif *netif = netif_list; netif != nullptr; netif = netif->next) {
    if (ip4_addr_isany_val(*netif_ip4_addr(netif))) continue;
    if (!result.empty()) result += ", ";
    result += espframe_describe_netif(netif);
    if (!netif_is_up(netif)) result += " (down)";
  }
  return result.empty() ? "none" : result;
}

namespace espframe_wireguard_dns {
inline ip_addr_t address;
inline bool resolved = false;
inline void on_found(const char *, const ip_addr_t *found, void *) {
  if (found != nullptr && IP_IS_V4(found)) {
    address = *found;
    resolved = true;
  }
}
}  // namespace espframe_wireguard_dns

struct EspframeImmichRoute {
  bool literal{false};
  bool resolved{false};
  uint32_t ip{0};
  std::string netif;
};

// Resolves the Immich host (through lwIP's DNS cache for a hostname, pending
// until the answer arrives) and asks lwIP which interface it would use.
inline EspframeImmichRoute espframe_lookup_immich_route(const std::string &host) {
  EspframeImmichRoute route;
  esphome::LwIPLock lock;
  ip_addr_t target;
  route.literal = espframe_parse_ipv4(host, route.ip);
  if (route.literal) {
    IP_ADDR4(&target, (route.ip >> 24) & 0xFF, (route.ip >> 16) & 0xFF, (route.ip >> 8) & 0xFF, route.ip & 0xFF);
  } else {
    static std::string resolving_host;
    if (resolving_host != host) {
      resolving_host = host;
      espframe_wireguard_dns::resolved = false;
    }
    if (!espframe_wireguard_dns::resolved) {
      ip_addr_t cached;
      if (dns_gethostbyname_addrtype(host.c_str(), &cached, espframe_wireguard_dns::on_found, nullptr,
                                     LWIP_DNS_ADDRTYPE_IPV4) != ERR_OK) {
        return route;
      }
      espframe_wireguard_dns::address = cached;
      espframe_wireguard_dns::resolved = true;
    }
    target = espframe_wireguard_dns::address;
    route.ip = lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&target)));
  }
  route.resolved = true;
  const struct netif *netif = ip4_route(ip_2_ip4(&target));
  route.netif = netif == nullptr ? "no route" : espframe_describe_netif(netif);
  return route;
}

// One log line that answers: is the Immich host a name or an address, what did
// it resolve to, which interface does lwIP pick for it, and does that address
// fall inside the tunnel's netmask and allowed IPs.
inline std::string espframe_describe_route_to(const std::string &host) {
  if (host.empty()) return "No Immich host saved";
  const EspframeImmichRoute route = espframe_lookup_immich_route(host);
  std::string text = "Immich host " + host + (route.literal ? " (IPv4 address)" : " (hostname)");
  if (!route.resolved) return text + ", not resolved yet";
  text += " -> " + espframe_format_ipv4(route.ip) + " via " + route.netif;
  if (espframe_wireguard_active()) {
    text += "; " + espframe_wireguard_coverage(espframe_wireguard_active_config(), route.ip);
  }
  return text;
}

#endif  // USE_ESP32
