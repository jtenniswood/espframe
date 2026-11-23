#pragma once

#include <string>

namespace espframe::native {

class NativeNetwork;

struct C6FirmwareStatus {
  std::string current;
  std::string latest;
  const char *state;
};

void start_c6_firmware_updates(NativeNetwork &network);
C6FirmwareStatus c6_firmware_status();
bool request_c6_firmware_update(bool install);

}  // namespace espframe::native
