#include "tailscale_url.h"

#include <cassert>

using espframe::native::tailnet_host;

int main() {
  const auto address = tailnet_host("http://100.64.0.1:2283/api");
  assert(address.hostname == "100.64.0.1");
  assert(address.ipv4 == 0x64400001);
  assert(tailnet_host("https://100.127.255.255/api").ipv4 == 0x647fffff);
  assert(tailnet_host("https://Example.Tail.TS.NET./api").hostname == "Example.Tail.TS.NET.");
  assert(tailnet_host("https://photos.tail.ts.net:443/api").hostname == "photos.tail.ts.net");
  assert(tailnet_host("https://photos.tail.ts.net:443/api").ipv4 == 0);
  for (const auto *url : {"http://100.63.255.255", "http://100.128.0.1", "http://100.64.0.256",
                          "http://100.64..1", "http://100.64.0.1.1", "https://photos.ts.net.example/api",
                          "https://user@photos.ts.net/api", "http://192.168.1.14/api", "file://photos.tail.ts.net"})
    assert(tailnet_host(url).hostname.empty());
}
