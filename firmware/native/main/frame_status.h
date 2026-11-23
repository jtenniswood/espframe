#pragma once

#include "daylight.h"
#include "photo_source.h"

#include <atomic>
#include <cstdint>

namespace espframe::native {

inline constexpr char frame_status_path[] = "/espframe/api/v1/status";

enum class ScreenState { Starting, Awake, Asleep };
enum class ImmichResponse { Pending, Responding, Rejected, Unreachable, RequestFailed };

constexpr ImmichResponse immich_response(int status) {
  if (status == 200)
    return ImmichResponse::Responding;
  if (status == 401 || status == 403)
    return ImmichResponse::Rejected;
  return status == 0 ? ImmichResponse::Unreachable : ImmichResponse::RequestFailed;
}

struct FrameStatus {
  ScreenState screen{ScreenState::Starting};
  PhotoSourceMode source{PhotoSourceMode::Library};
  PhotoSourceStatus notice{PhotoSourceStatus::None};
  ImmichResponse contact{ImmichResponse::Pending};
  bool configured{};
  bool showed_photo{};
};

// The display task publishes only at first presentation, backlight changes,
// or worker completions. Settings requests read these values without a lock.
class FrameStatusStore {
 public:
  void configure(bool configured, PhotoSourceMode source) {
    source_.store(source, std::memory_order_relaxed);
    configured_.store(configured, std::memory_order_release);
  }

  void set_screen(bool awake) {
    screen_.store(awake ? ScreenState::Awake : ScreenState::Asleep, std::memory_order_relaxed);
  }

  void presented_photo() { showed_photo_.store(true, std::memory_order_relaxed); }

  void begin_immich_connection() {
    notice_.store(PhotoSourceStatus::None, std::memory_order_relaxed);
    contact_.store(ImmichResponse::Pending, std::memory_order_relaxed);
  }

  void set_solar_times(std::optional<SolarTimes> times) {
    solar_times_.store(times ? (static_cast<uint32_t>(times->sunrise) << 16) | times->sunset : UINT32_MAX,
                       std::memory_order_relaxed);
  }

  std::optional<SolarTimes> solar_times() const {
    const auto packed = solar_times_.load(std::memory_order_relaxed);
    if (packed == UINT32_MAX)
      return {};
    return SolarTimes{static_cast<int>(packed >> 16), static_cast<int>(packed & UINT16_MAX)};
  }

  void finish_preparation(PhotoSourceStatus notice, ImmichResponse contact) {
    notice_.store(notice, std::memory_order_relaxed);
    if (contact != ImmichResponse::Pending)
      contact_.store(contact, std::memory_order_relaxed);
  }

  FrameStatus read() const {
    const bool configured = configured_.load(std::memory_order_acquire);
    return {screen_.load(std::memory_order_relaxed), source_.load(std::memory_order_relaxed),
            notice_.load(std::memory_order_relaxed), contact_.load(std::memory_order_relaxed),
            configured, showed_photo_.load(std::memory_order_relaxed)};
  }

 private:
  std::atomic<ScreenState> screen_{ScreenState::Starting};
  std::atomic<PhotoSourceMode> source_{PhotoSourceMode::Library};
  std::atomic<PhotoSourceStatus> notice_{PhotoSourceStatus::None};
  std::atomic<ImmichResponse> contact_{ImmichResponse::Pending};
  std::atomic<bool> configured_{};
  std::atomic<bool> showed_photo_{};
  std::atomic<uint32_t> solar_times_{UINT32_MAX};
};

inline FrameStatusStore frame_status;

}  // namespace espframe::native
