#pragma once

#include "playback_trace.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include <atomic>
#include <variant>

namespace espframe::native {

class TraceLogger {
 public:
  void start(unsigned nominal_period_us);
  void frame(int64_t latched_us, MotionStamp stamp);
  void gesture(int64_t latched_us, const FirstMotionTrace &motion);
  void refill(int64_t finished_us, int64_t started_us, SlideIndex slide, RefillOutcome outcome);

 private:
  struct Frame {
    int64_t latched_us;
    MotionStamp stamp;
  };
  struct Gesture {
    int64_t latched_us;
    FirstMotionTrace motion;
  };
  struct Refill {
    int64_t finished_us;
    int64_t started_us;
    SlideIndex slide;
    RefillOutcome outcome;
  };
  using Event = std::variant<Frame, Gesture, Refill>;
  struct Record {
    uint32_t sequence;
    Event event;
  };

  void emit_(const Event &event);
  static void run_(void *context);

  QueueHandle_t queue_{};
  std::atomic<uint32_t> dropped_{};
  uint32_t sequence_{};
};

}  // namespace espframe::native
