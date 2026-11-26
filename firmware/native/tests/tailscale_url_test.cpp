#include "tailscale_url.h"

#include <cassert>

using espframe::native::tailnet_host;
using espframe::native::same_dns_name;

int main() {
  const auto address = tailnet_host("http://100.64.0.1:2283/api");
  assert(address.hostname == "100.64.0.1");
  assert(address.ipv4 == 0x64400001);
  assert(tailnet_host("https://100.127.255.255/api").ipv4 == 0x647fffff);
  assert(tailnet_host("https://Example.Tail.TS.NET./api").hostname == "Example.Tail.TS.NET.");
  assert(tailnet_host("https://photos.tail.ts.net:443/api").hostname == "photos.tail.ts.net");
  assert(tailnet_host("https://photos.tail.ts.net:443/api").ipv4 == 0);
  assert(same_dns_name("Photos.Tail.TS.NET.", "photos.tail.ts.net"));
  assert(!same_dns_name("photos.tail.ts.net", "photos.other-tail.ts.net"));
  assert(!same_dns_name("photos", "photos.tail.ts.net"));
  assert(same_dns_name("a-long-device-name-that-does-not-fit-in-the-upstream-peer-record.tail.ts.net.",
                       "a-long-device-name-that-does-not-fit-in-the-upstream-peer-record.tail.ts.net"));
  for (const auto *url : {"http://100.63.255.255", "http://100.128.0.1", "http://100.64.0.256",
                          "http://100.64..1", "http://100.64.0.1.1", "https://photos.ts.net.example/api",
                          "https://user@photos.ts.net/api", "http://192.168.1.14/api", "file://photos.tail.ts.net"})
    assert(tailnet_host(url).hostname.empty());
}
