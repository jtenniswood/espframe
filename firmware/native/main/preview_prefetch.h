#pragma once

#include "immich_http.h"
#include "photo_history.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace espframe::native {

// One encoded preview can download while the slide worker decodes another.
// The slide worker owns the slot except between starting a request and
// receiving completion. The HTTP handle stays on the download task.
class PreviewPrefetch {
 public:
  static constexpr size_t capacity = 1024 * 1024;

  explicit PreviewPrefetch(const std::atomic<bool> &slide_cancelled) : slide_cancelled_(slide_cancelled) {}
  void start(const AssetId &id, uint32_t connection, const std::string &base_url, const std::string &key);
  size_t take(const AssetId &id, uint32_t connection, std::span<uint8_t> destination, int64_t deadline_us = 0);
  void cancel() { cancelled_ = true; }
  void release_on_memory_pressure();

 private:
  static void run_(void *context);

  const std::atomic<bool> &slide_cancelled_;
  std::atomic<bool> cancelled_{};
  ImmichHttp http_{cancelled_};
  TaskHandle_t task_{};
  SemaphoreHandle_t complete_{};
  uint8_t *buffer_{};
  AssetId id_{};
  uint32_t connection_{};
  std::string url_;
  const std::string *key_{};  // The Immich connection settings live as long as the task.
  int64_t deadline_us_{};
  int64_t memory_retry_at_us_{};
  size_t length_{};
  bool pending_{};
};

}  // namespace espframe::native
