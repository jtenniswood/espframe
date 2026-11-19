#pragma once

#include "slide_geometry.h"
#include "wifi_recovery.h"

#include <cstdint>

namespace espframe::native {

void draw_recovery_card(uint16_t *frame, const uint8_t *atlas, const RecoveryNetwork &network,
                        ScreenRotation rotation);

}  // namespace espframe::native
