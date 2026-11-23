#pragma once

#include "touch_report.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace espframe::native {

class TouchReader {
 public:
  void start();
  bool poll(TouchReport &report, ScreenRotation rotation) const;

 private:
  static void run_(void *context);
  QueueHandle_t reports_{};
};

}  // namespace espframe::native
