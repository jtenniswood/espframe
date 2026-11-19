#pragma once

#include <cstddef>
#include <cstdint>

namespace espframe::native {

enum class FirmwareUpdateResult {
  Staged, Unavailable, RestartPending, Size, WrongBoard, Interrupted, Invalid, WriteFailed,
};

using FirmwareRead = int (*)(void *, uint8_t *, size_t);

bool can_update_firmware();
FirmwareUpdateResult stage_firmware(size_t length, FirmwareRead read, void *context);
void start_firmware_boot_check();
void confirm_firmware_boot();

}  // namespace espframe::native
