#pragma once

#include "frame_status.h"

#include <cstdint>

namespace espframe::native {

class InitialPhotoTimeout {
 public:
  void observe(int64_t now_us, bool prepared, ImmichResponse response) {
    if (prepared || response == ImmichResponse::Responding) {
      *this = {};
    } else if (response != ImmichResponse::Pending) {
      if (failure_ == ImmichResponse::Pending)
        failed_at_us_ = now_us;
      failure_ = response;
    }
  }

  ImmichResponse notice(int64_t now_us, uint32_t timeout_seconds) const {
    return now_us - failed_at_us_ >= timeout_seconds * 1'000'000LL ? failure_ : ImmichResponse::Pending;
  }

 private:
  int64_t failed_at_us_{};
  ImmichResponse failure_{ImmichResponse::Pending};
};

}  // namespace espframe::native
