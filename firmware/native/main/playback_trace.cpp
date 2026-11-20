#include "trace_logger.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/task.h"

#include <cinttypes>
#include <type_traits>

namespace espframe::native {
namespace {

constexpr const char *tag = "swipe-trace";
constexpr unsigned queue_depth = 96;

}  // namespace

void TraceLogger::start(unsigned nominal_period_us) {
  static_assert(std::is_trivially_copyable_v<Record>);
  queue_ = xQueueCreate(queue_depth, sizeof(Record));
  if (!queue_ || xTaskCreatePinnedToCore(run_, "swipe-trace", 3072, this, 1, nullptr, 0) != pdPASS) {
    if (queue_)
      vQueueDelete(queue_);
    queue_ = nullptr;
    ESP_LOGW(tag, "Performance trace unavailable");
    return;
  }
  ESP_LOGI(tag, "start 1 %s %u %zu %zu", NATIVE_BOARD, nominal_period_us,
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

void TraceLogger::emit_(const Event &event) {
  if (queue_) {
    const Record record{++sequence_, event};
    if (xQueueSend(queue_, &record, 0) != pdPASS)
      dropped_.fetch_add(1, std::memory_order_relaxed);
  }
}

void TraceLogger::frame(int64_t latched_us, MotionStamp stamp) {
  if (stamp.phase != MotionPhase::Still)
    emit_(Frame{latched_us, stamp});
}

void TraceLogger::gesture(int64_t latched_us, const FirstMotionTrace &motion) {
  emit_(Gesture{latched_us, motion});
}

void TraceLogger::refill(int64_t finished_us, int64_t started_us, SlideIndex slide, RefillOutcome outcome) {
  emit_(Refill{finished_us, started_us, slide, outcome});
}

void TraceLogger::run_(void *context) {
  auto &logger = *static_cast<TraceLogger *>(context);
  Record record{0, Frame{}};
  while (true) {
    if (xQueueReceive(logger.queue_, &record, portMAX_DELAY) != pdPASS)
      continue;
    const uint32_t dropped = logger.dropped_.exchange(0, std::memory_order_relaxed);
    if (dropped != 0)
      ESP_LOGW(tag, "lost %" PRIu32, dropped);
    if (const auto *frame = std::get_if<Frame>(&record.event)) {
      ESP_LOGI(tag, "f %" PRIu32 " %" PRIu32 " %c %" PRId64 " %d", record.sequence,
               frame->stamp.epoch, static_cast<char>(frame->stamp.phase), frame->latched_us, frame->stamp.preparing);
    } else if (const auto *gesture = std::get_if<Gesture>(&record.event)) {
      const auto &motion = gesture->motion;
      ESP_LOGI(tag, "g %" PRIu32 " %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %c %" PRId64 " %d",
               record.sequence, gesture->latched_us, motion.sampled_us, motion.contact_us, motion.composed_us,
               static_cast<char>(motion.cache.result), motion.cache.missing_slide, motion.preparing);
    } else {
      const auto &refill = std::get<Refill>(record.event);
      ESP_LOGI(tag, "r %" PRIu32 " %" PRId64 " %" PRId64 " %" PRId64 " %c", record.sequence,
               refill.finished_us, refill.started_us, refill.slide, static_cast<char>(refill.outcome));
    }
  }
}

}  // namespace espframe::native
