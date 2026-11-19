#pragma once

#include <string_view>

namespace espframe::native {

inline constexpr std::string_view trial_replay_origin = SWIPE_TRIAL_REPLAY_ORIGIN;
inline constexpr bool direct_trial_replay = !trial_replay_origin.empty() && !SWIPE_TRIAL_REPLAY_PREPARED;
inline constexpr char replay_immich_key[] = "espframe-synthetic-preview";
inline constexpr char replay_slide_token[] = "espframe-synthetic-slide-replay-only";

}  // namespace espframe::native
