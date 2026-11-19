#pragma once

#include "photo_source.h"
#include "slide_geometry.h"
#include "wifi_recovery.h"

#include <cstdint>

namespace espframe::native {

void draw_recovery_card(uint16_t *frame, const uint8_t *atlas, const RecoveryNetwork &network,
                        ScreenRotation rotation);
void draw_source_status(uint16_t *frame, const uint8_t *atlas, PhotoSourceStatus status, ScreenRotation rotation);
void draw_loading_status(uint16_t *frame, const uint8_t *atlas, int direction, ScreenRotation rotation);

}  // namespace espframe::native
