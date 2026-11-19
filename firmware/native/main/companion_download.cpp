#include "companion_download.h"

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "live_trial.h"

namespace espframe::native {
namespace {

constexpr size_t capacity = 2 * 1024 * 1024;
constexpr size_t free_for_decode = 8 * 1024 * 1024;

}  // namespace

void CompanionDownload::start(const AssetId &id, uint32_t connection, const std::string &base_url,
                              const std::string &key) {
  if (!connection || slide_cancelled_)
    return;
  if (pending_) {
    if (id == id_ && connection == connection_ && !cancelled_)
      return;
    if (xSemaphoreTake(complete_, 0) != pdTRUE)
      return;
    pending_ = false;
  }
  if (!buffer_ && heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >= free_for_decode + capacity)
    buffer_ = static_cast<uint8_t *>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM));
  if (!buffer_)
    return;
  if (!task_) {
    if (!complete_)
      complete_ = xSemaphoreCreateBinary();
    if (!complete_ || xTaskCreatePinnedToCore(run, "pair-download", 4096, this, 2, &task_, xPortGetCoreID()) != pdPASS) {
      heap_caps_free(buffer_);
      buffer_ = nullptr;
      return;
    }
  }

  id_ = id;
  connection_ = connection;
  url_ = base_url + "/assets/" + id.data() + "/thumbnail?size=preview";
  key_ = &key;
  deadline_us_ = esp_timer_get_time() + 3'500'000;
  cancelled_ = false;
  pending_ = true;
  xTaskNotifyGive(task_);
}

std::span<const uint8_t> CompanionDownload::take(const AssetId &id, uint32_t connection) {
  if (!pending_ || id != id_ || connection != connection_)
    return {};
  const auto started = esp_timer_get_time();
  while (xSemaphoreTake(complete_, pdMS_TO_TICKS(10)) != pdTRUE) {
    if (slide_cancelled_) {
      cancel();
      return {};
    }
  }
  pending_ = false;
  if (slide_cancelled_ || !length_)
    return {};
  if constexpr (SWIPE_TRIAL_IMMICH)
    record_live_trial_preparation_time(TrialPreparationStage::CompanionWait,
                                       static_cast<uint32_t>(esp_timer_get_time() - started));
  return {buffer_, length_};
}

void CompanionDownload::run(void *context) {
  auto &self = *static_cast<CompanionDownload *>(context);
  uint32_t active_connection = 0;
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (self.connection_ != active_connection) {
      self.http_.disconnect();
      active_connection = self.connection_;
    }
    const auto started = esp_timer_get_time();
    const int status = self.http_.read(self.url_, self.key_->c_str(), {}, {self.buffer_, capacity}, self.length_,
                                       self.deadline_us_);
    if (status != 200 || self.cancelled_)
      self.length_ = 0;
    if constexpr (SWIPE_TRIAL_IMMICH) {
      record_live_trial_preview(status);
      record_live_trial_preparation_time(TrialPreparationStage::CompanionTransfer,
                                         static_cast<uint32_t>(esp_timer_get_time() - started), self.length_);
    }
    xSemaphoreGive(self.complete_);
  }
}

}  // namespace espframe::native
