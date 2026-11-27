#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace espframe::native {

enum class FirmwareUpdateResult {
  Staged, Unavailable, RestartPending, Size, WrongBoard, Interrupted, Invalid, WriteFailed,
};

using FirmwareRead = int (*)(void *, uint8_t *, size_t);

struct FirmwareImage {
  uint32_t offset{};
  std::array<uint8_t, 32> elf_sha256{};
  bool operator==(const FirmwareImage &) const = default;
};

struct FirmwareBootStatus {
  std::optional<FirmwareImage> running;
  std::optional<FirmwareImage> rejected;
  bool boot_verified{};
};

bool can_update_firmware();
bool is_firmware_test_build();
FirmwareUpdateResult stage_firmware(size_t length, FirmwareRead read, void *context, FirmwareImage *staged = nullptr);
FirmwareBootStatus firmware_boot_status();
std::string_view last_reset_reason();
bool frame_restart_available();
bool run_coprocessor_update(bool (*install)(void *), void *context);
void request_frame_restart();
void start_firmware_boot_check();
void reject_pending_firmware_boot();
void confirm_firmware_boot();

}  // namespace espframe::native
