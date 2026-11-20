#include "preview_prefetch.h"

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "live_trial.h"

#include <algorithm>
#include <cstring>

namespace espframe::native {

void PreviewPrefetch::start(const AssetId &id, uint32_t connection, const std::string &base_url, const std::string &key) {
  if (disabled_ || !connection || slide_cancelled_)
    return;
  if (pending_) {
    if (id == id_ && connection == connection_ && !cancelled_)
      return;
    if (xSemaphoreTake(complete_, 0) != pdTRUE)
      return;
    pending_ = false;
  }
  // Startup already reserves 10 MiB for the full preview buffer and
  // decoding. Leave 6 MiB here even if a pair's first pane is decoded.
  constexpr size_t decode_reserve = 6 * 1024 * 1024;
  if (!buffer_ && heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >= decode_reserve + capacity)
    buffer_ = static_cast<uint8_t *>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM));
  if (!buffer_)
    return;
  if (!task_) {
    if (!complete_)
      complete_ = xSemaphoreCreateBinary();
    if (!complete_ || xTaskCreatePinnedToCore(run_, "preview-fetch", 6144, this, 2, &task_, xPortGetCoreID()) != pdPASS) {
      heap_caps_free(buffer_);
      buffer_ = nullptr;
      return;
    }
  }
  id_ = id;
  connection_ = connection;
  url_ = base_url + "/assets/" + id.data() + "/thumbnail?size=preview";
  key_ = &key;
  deadline_us_ = esp_timer_get_time() + 2'000'000;
  cancelled_ = false;
  pending_ = true;
  xTaskNotifyGive(task_);
}

size_t PreviewPrefetch::take(const AssetId &id, uint32_t connection, std::span<uint8_t> destination,
                             int64_t deadline_us) {
  if (!pending_ || cancelled_)
    return 0;
  if (id != id_ || connection != connection_ || destination.empty()) {
    cancel();
    return 0;
  }
  constexpr int64_t poll_interval_us = 10'000;
  for (;;) {
    const auto remaining = deadline_us ? deadline_us - esp_timer_get_time() : poll_interval_us;
    if (slide_cancelled_ || remaining <= 0) {
      cancel();
      return 0;
    }
    const auto wait_ms = (std::min(remaining, poll_interval_us) + 999) / 1000;
    if (xSemaphoreTake(complete_, pdMS_TO_TICKS(wait_ms)) == pdTRUE)
      break;
  }
  pending_ = false;
  if (slide_cancelled_ || (deadline_us && esp_timer_get_time() >= deadline_us) ||
      !length_ || length_ > destination.size())
    return 0;
  // Keep the staging buffer smaller than the foreground download limit.
  // Once copied, the slot can start filling again during this decode.
  std::memcpy(destination.data(), buffer_, length_);
  return length_;
}

void PreviewPrefetch::release_on_memory_pressure() {
  if (!buffer_)
    return;
  disabled_ = true;
  cancel();
  if (pending_) {
    xSemaphoreTake(complete_, portMAX_DELAY);
    pending_ = false;
  }
  heap_caps_free(buffer_);
  buffer_ = nullptr;
}

void PreviewPrefetch::run_(void *context) {
  auto &self = *static_cast<PreviewPrefetch *>(context);
  uint32_t active_connection{};
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (self.connection_ != active_connection) {
      self.http_.disconnect();
      active_connection = self.connection_;
    }
#if SWIPE_TRIAL_IMMICH
    const auto started = esp_timer_get_time();
#endif
    const int status = self.http_.read(self.url_, self.key_->c_str(), {}, {self.buffer_, capacity}, self.length_,
                                       self.deadline_us_);
    if (status != 200 || self.cancelled_)
      self.length_ = 0;
#if SWIPE_TRIAL_IMMICH
    record_live_trial_preview(status);
    record_live_trial_preparation_time(TrialPreparationStage::PrefetchTransfer,
                                       static_cast<uint32_t>(esp_timer_get_time() - started), self.length_);
#endif
    xSemaphoreGive(self.complete_);
  }
}

}  // namespace espframe::native
