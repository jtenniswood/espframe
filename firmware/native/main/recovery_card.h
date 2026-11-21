#pragma once

#include "frame_status.h"
#include "slide_geometry.h"
#include "wifi_recovery.h"

#include <cstdint>
#include <span>

namespace espframe::native {

struct StatusText {
  std::span<const uint8_t> masks;
  uint8_t language{};
};

void draw_recovery_card(uint16_t *frame, const uint8_t *atlas, StatusText text, const RecoveryNetwork &network,
                        ScreenRotation rotation);
void draw_source_status(uint16_t *frame, StatusText text, PhotoSourceStatus status, ScreenRotation rotation);
void draw_connection_notice(uint16_t *frame, StatusText text, ImmichResponse response, ScreenRotation rotation);

}  // namespace espframe::native
