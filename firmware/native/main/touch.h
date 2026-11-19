#pragma once

#include "touch_report.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace espframe::native {

class TouchReader {
 public:
  void start(ScreenRotation rotation);
  bool poll(TouchReport &report) const;

 private:
  static void run_(void *context);
  QueueHandle_t reports_{};
  ScreenRotation rotation_{};
};

}  // namespace espframe::native
