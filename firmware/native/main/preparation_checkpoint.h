#pragma once

namespace espframe::native {

// The worker can pause at this boundary or stop if its lease was cancelled.
struct PreparationCheckpoint {
  void *context{};
  bool (*proceed)(void *){};

  bool operator()() const { return proceed == nullptr || proceed(context); }
};

}  // namespace espframe::native
