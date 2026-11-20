#pragma once

#include "settings.h"

namespace espframe::native {

class NativeNetwork;
bool start_web_settings(const Settings &settings, NativeNetwork &network);

}  // namespace espframe::native
