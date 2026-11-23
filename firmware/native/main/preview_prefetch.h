#pragma once

#include "immich_http.h"
#include "photo_history.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <optional>

namespace espframe::native {

// One encoded preview can download while the slide worker decodes another.
// The slide worker owns the slot except between starting a request and
// receiving completion. The HTTP handle stays on the download task.
class PreviewPrefetch {
 public:
  enum class Size { Preview, Thumbnail };
  static constexpr size_t capacity = 1024 * 1024;
  static constexpr size_t thumbnail_capacity = 128 * 1024;
  static constexpr int64_t thumbnail_budget_us = 650'000;

  explicit PreviewPrefetch(const std::atomic<bool> &slide_cancelled) : slide_cancelled_(slide_cancelled) {}
  bool start(const AssetId &id, uint32_t connection, const std::string &base_url, const std::string &key,
             Size size = Size::Preview);
  void warm_prepared(std::string url, uint32_t connection, const std::string &token);
  // Empty lets the caller download from Immich. Zero means a required
  // transfer exhausted its budget or a staged thumbnail already failed.
  std::optional<size_t> take(const AssetId &id, uint32_t connection, std::span<uint8_t> destination,
                             int64_t deadline_us = 0, Size size = Size::Preview);
  void cancel() { cancelled_ = true; }
  void cancel_incomplete();
  void release_on_memory_pressure();

 private:
  bool claim_slot_();
  bool start_task_();
  static void run_(void *context);

  const std::atomic<bool> &slide_cancelled_;
  std::atomic<bool> cancelled_{};
  ImmichHttp http_{cancelled_};
  ImmichHttp prepared_http_{cancelled_, "Authorization"};
  TaskHandle_t task_{};
  SemaphoreHandle_t complete_{};
  uint8_t *buffer_{};
  AssetId id_{};
  uint32_t connection_{};
  std::string url_;
  std::string key_;
  int64_t deadline_us_{};
  int64_t memory_retry_at_us_{};
  size_t length_{};
  Size size_{Size::Preview};
  bool pending_{};
  bool warming_{};
};

}  // namespace espframe::native
