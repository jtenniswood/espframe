#pragma once

#include "slide_geometry.h"

#include <optional>
#include <utility>

namespace espframe::native {

enum class TrialDragOutcome { Forward, Backward, Returned, Cancelled, Interrupted };

class TrialSwipeLanding {
 public:
  void released(SlideIndex origin) { origin_ = origin; }

  bool interrupt() { return std::exchange(origin_, std::nullopt).has_value(); }

  std::optional<TrialDragOutcome> presented(SlideIndex slide) {
    const auto origin = std::exchange(origin_, std::nullopt);
    if (!origin)
      return {};
    if (slide == *origin)
      return TrialDragOutcome::Returned;
    return slide > *origin ? TrialDragOutcome::Forward : TrialDragOutcome::Backward;
  }

 private:
  std::optional<SlideIndex> origin_;
};

}  // namespace espframe::native
