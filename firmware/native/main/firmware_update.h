#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace espframe::native {

enum class FirmwareUpdateResult {
  Staged, Unavailable, RestartPending, Size, WrongBoard, Interrupted, Invalid, WriteFailed,
};

using FirmwareRead = int (*)(void *, uint8_t *, size_t);

bool can_update_firmware();
FirmwareUpdateResult stage_firmware(size_t length, FirmwareRead read, void *context);
std::string_view last_reset_reason();
void request_frame_restart();
void start_firmware_boot_check();
void confirm_firmware_boot();

}  // namespace espframe::native
