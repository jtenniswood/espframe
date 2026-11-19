#pragma once

#include "esphome_session.h"

namespace espframe::native {

// Serve one connected TCP socket. The caller owns its lifetime.
void serve_esphome_socket(int fd, const EspHomeIdentity &identity, std::span<const uint8_t, 32> key,
                          const EspHomeControls &controls, void *context = nullptr, bool (*current)(void *) = nullptr);

}  // namespace espframe::native
