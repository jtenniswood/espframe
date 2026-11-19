#pragma once

#include "driver/ppa.h"

#include <atomic>
#include <cstdint>

namespace espframe::native {

// One transaction owns the writable panel buffer and reads two prepared
// slides until the PPA completion callback releases them.
class FrameBlend {
 public:
  bool init();
  void release();
  bool start(const uint16_t *background, const uint16_t *foreground, uint16_t *output, uint8_t opacity);
  bool pending() const { return pending_; }
  bool poll_complete();

 private:
  static bool completed_(ppa_client_handle_t, ppa_event_data_t *, void *context);

  ppa_client_handle_t client_{};
  std::atomic<bool> complete_{};
  bool pending_{};
};

}  // namespace espframe::native
