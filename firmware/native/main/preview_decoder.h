#pragma once

#include <cstddef>
#include <cstdint>

namespace espframe::native {

// Decode an Immich preview and rotate the fitted RGB565 image into panel order.
bool decode_preview(const uint8_t *encoded, size_t length, uint16_t *panel);

}  // namespace espframe::native
