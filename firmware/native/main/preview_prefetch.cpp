#include "preview_prefetch.h"

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "live_trial.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace espframe::native {

bool PreviewPrefetch::claim_slot_() {
  if (pending_) {
    if (xSemaphoreTake(complete_, 0) != pdTRUE)
      return false;
    pending_ = false;
  }
  return true;
}

bool PreviewPrefetch::start_task_() {
  if (task_)
    return true;
  if (!complete_)
    complete_ = xSemaphoreCreateBinary();
  return complete_ && xTaskCreatePinnedToCore(run_, "preview-fetch", 6144, this, 2, &task_, xPortGetCoreID()) == pdPASS;
}

void PreviewPrefetch::start(const AssetId &id, uint32_t connection, const std::string &base_url, const std::string &key) {
  if (!connection || slide_cancelled_ ||
      (pending_ && !warming_ && id == id_ && connection == connection_ && !cancelled_) || !claim_slot_())
    return;
  if (!buffer_) {
    if (memory_retry_at_us_ && esp_timer_get_time() < memory_retry_at_us_)
      return;
    // Leave room for decoding even with the first pane of a pair allocated.
    // After pressure, require an extra buffer's worth of free memory so
    // taking the staging slot back does not immediately recreate the shortage.
    constexpr size_t decode_reserve = 6 * 1024 * 1024;
    const size_t free_needed = decode_reserve + capacity + (memory_retry_at_us_ ? capacity : 0);
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < free_needed)
      return;
    buffer_ = static_cast<uint8_t *>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM));
    if (!buffer_)
      return;
  }
  if (!start_task_()) {
    heap_caps_free(buffer_);
    buffer_ = nullptr;
    return;
  }
  id_ = id;
  connection_ = connection;
  url_ = base_url + "/assets/" + id.data() + "/thumbnail?size=preview";
  key_ = &key;
  deadline_us_ = esp_timer_get_time() + ImmichHttp::transfer_budget_us;
  warming_ = false;
  cancelled_ = false;
  pending_ = true;
  xTaskNotifyGive(task_);
}

void PreviewPrefetch::warm_prepared(WarmUrls urls, uint32_t connection, const std::string &token) {
  if (!connection || slide_cancelled_ || !claim_slot_() || !start_task_())
    return;
  warm_urls_ = std::move(urls);
  connection_ = connection;
  key_ = &token;
  deadline_us_ = esp_timer_get_time() + 500'000;
  warming_ = true;
  cancelled_ = false;
  pending_ = true;
  xTaskNotifyGive(task_);
}

std::optional<size_t> PreviewPrefetch::take(const AssetId &id, uint32_t connection, std::span<uint8_t> destination,
                                           int64_t deadline_us) {
  if (!pending_ || warming_ || cancelled_)
    return {};
  if (connection != connection_) {
    cancel();
    return {};
  }
  if (id != id_ || destination.empty()) {
    cancel_incomplete();
    return {};
  }
  const auto started = esp_timer_get_time();
  const auto wait_until = deadline_us ? std::min(deadline_us, deadline_us_) : deadline_us_;
  const auto failed = [&]() -> std::optional<size_t> {
    if (!deadline_us && started < deadline_us_ && esp_timer_get_time() >= deadline_us_)
      return 0;
    return {};
  };
  constexpr int64_t poll_interval_us = 10'000;
  for (;;) {
    if (slide_cancelled_) {
      cancel_incomplete();
      return {};
    }
    if (xSemaphoreTake(complete_, 0) == pdTRUE)
      break;
    const auto remaining = wait_until - esp_timer_get_time();
    if (remaining <= 0) {
      cancel();
      return failed();
    }
    const auto wait_ms = (std::min(remaining, poll_interval_us) + 999) / 1000;
    if (xSemaphoreTake(complete_, pdMS_TO_TICKS(wait_ms)) == pdTRUE)
      break;
  }
  if (slide_cancelled_) {
    xSemaphoreGive(complete_);
    return {};
  }
  pending_ = false;
  if ((deadline_us && esp_timer_get_time() >= deadline_us) || length_ > destination.size())
    return {};
  if (!length_)
    return failed();
  // Keep the staging buffer smaller than the foreground download limit.
  // Once copied, the slot can start filling again during this decode.
  std::memcpy(destination.data(), buffer_, length_);
  return length_;
}

void PreviewPrefetch::cancel_incomplete() {
  if (!pending_ || warming_ || cancelled_)
    return;
  if (xSemaphoreTake(complete_, 0) != pdTRUE) {
    cancel();
  } else {
    // The preparation task owns the slot again. Leave the result for a
    // later request for this asset without moving or copying its bytes.
    xSemaphoreGive(complete_);
  }
}

void PreviewPrefetch::release_on_memory_pressure() {
  if (buffer_) {
    cancel();
    if (pending_) {
      xSemaphoreTake(complete_, portMAX_DELAY);
      pending_ = false;
    }
    heap_caps_free(buffer_);
    buffer_ = nullptr;
  }
  constexpr int64_t memory_retry_delay_us = 30'000'000;
  memory_retry_at_us_ = esp_timer_get_time() + memory_retry_delay_us;
}

void PreviewPrefetch::run_(void *context) {
  auto &self = *static_cast<PreviewPrefetch *>(context);
  uint32_t active_connection{};
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (self.connection_ != active_connection) {
      self.http_.disconnect();
      self.prepared_http_.disconnect();
      active_connection = self.connection_;
    }
    if (self.warming_) {
      const auto authorization = "Bearer " + *self.key_;
      for (const auto &url : self.warm_urls_) {
        const auto started = esp_timer_get_time();
        if (url.empty() || self.cancelled_ || started >= self.deadline_us_)
          break;
        size_t received{};
        const auto status = self.prepared_http_.read(url, authorization.c_str(), "{}", {}, received,
                                                    self.deadline_us_);
        if (status != 202 && status != 204)
          break;
#if SWIPE_TRIAL_IMMICH
        record_live_trial_preparation_time(TrialPreparationStage::PreparedWarm,
                                           static_cast<uint32_t>(esp_timer_get_time() - started));
#endif
      }
      xSemaphoreGive(self.complete_);
      continue;
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
