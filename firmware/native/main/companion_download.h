#pragma once

#include "immich_http.h"
#include "photo_history.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace espframe::native {

// The photo worker owns the request and consumes the finished buffer. One
// background task handles the transfer while the first portrait decodes.
class CompanionDownload {
 public:
  explicit CompanionDownload(const std::atomic<bool> &slide_cancelled) : slide_cancelled_(slide_cancelled) {}

  void start(const AssetId &id, uint32_t connection, const std::string &base_url, const std::string &key);
  std::span<const uint8_t> take(const AssetId &id, uint32_t connection);
  void cancel() { cancelled_ = true; }

 private:
  static void run(void *context);

  const std::atomic<bool> &slide_cancelled_;
  std::atomic<bool> cancelled_{};
  ImmichHttp http_{cancelled_};
  TaskHandle_t task_{};
  SemaphoreHandle_t complete_{};
  uint8_t *buffer_{};
  AssetId id_{};
  uint32_t connection_{};
  std::string url_;
  const std::string *key_{};
  int64_t deadline_us_{};
  size_t length_{};
  bool pending_{};
};

}  // namespace espframe::native
