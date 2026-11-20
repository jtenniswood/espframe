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
inline constexpr uint16_t loading_page_background = 0x1105;
void draw_loading_page_text(uint16_t *frame, const uint8_t *atlas, int shift, ScreenRotation rotation);

}  // namespace espframe::native
