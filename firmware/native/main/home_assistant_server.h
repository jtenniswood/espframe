#pragma once

#include "esphome_session.h"

namespace espframe::native {

class NativeNetwork;

bool start_home_assistant(const NativeNetwork &network, EspHomeIdentity identity, std::string_view key);

}  // namespace espframe::native
