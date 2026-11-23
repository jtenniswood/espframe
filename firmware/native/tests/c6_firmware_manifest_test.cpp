#include "c6_firmware_manifest.h"

#include <cassert>
#include <fstream>
#include <iterator>

using namespace espframe::native;

int main(int argc, char **argv) {
  const HostedVersion host{3, 1, 0};
  assert(parse_hosted_version("3.1.0") == host);
  assert(!parse_hosted_version("3.1"));
  assert(!parse_hosted_version("3.1.0-rc.1"));
  assert(!parse_hosted_version("3.1."));
  assert(!parse_hosted_version("30000000000.1.0"));
  assert(!read_c6_release(R"({"versions":[]})", host));
  assert(!read_c6_release(R"({"versions":[]} trailing)", host));
  const auto candidate = read_c6_release(R"({"versions":[
    {"version":"3.2.0","url":"https://esphome.github.io/esp-hosted-firmware/v3.2.0/network_adapter_esp32c6.bin","sha256":"2222222222222222222222222222222222222222222222222222222222222222"},
    {"version":"3.1.0","url":"https://example.test/esp-hosted-firmware/v3.1.0/network_adapter_esp32c6.bin","sha256":"2222222222222222222222222222222222222222222222222222222222222222"},
    {"version":"3.1.0","url":"https://esphome.github.io/esp-hosted-firmware/v3.1.0/network_adapter_esp32c6.factory.bin","sha256":"2222222222222222222222222222222222222222222222222222222222222222"},
    {"version":"3.1.0","url":"https://esphome.github.io/esp-hosted-firmware/v3.1.0/network_adapter_esp32c6.bin","sha256":"222222222222222222222222222222222222222222222222222222222222222g"},
    {"version":"2.12.8","url":"https://esphome.github.io/esp-hosted-firmware/v2.12.8/network_adapter_esp32c6.bin","sha256":"2222222222222222222222222222222222222222222222222222222222222222"},
    {"version":"3.0.9","url":"https://esphome.github.io/esp-hosted-firmware/v3.0.9/network_adapter_esp32c6.bin","sha256":"80e85881783840ff7c533878b679b9a6d72c426d0696e2f15ee064ca8b6c12db"}
  ]})", host);
  assert(candidate && hosted_version_text(candidate->version) == "3.0.9");
  assert(candidate->sha256.front() == 0x80 && candidate->sha256.back() == 0xdb);
  if (argc > 1) {
    std::ifstream file(argv[1]);
    const std::string json(std::istreambuf_iterator<char>{file}, {});
    const auto live = read_c6_release(json, host);
    assert(live && live->version == host);
  }
}
