#pragma once

#include <string_view>

namespace espframe::native {

inline constexpr std::string_view trial_replay_origin = SWIPE_TRIAL_REPLAY_ORIGIN;
inline constexpr bool synthetic_trial_replay = !trial_replay_origin.empty();
inline constexpr bool direct_trial_replay = synthetic_trial_replay && !SWIPE_TRIAL_REPLAY_PREPARED;
inline constexpr bool trial_replay_touch = direct_trial_replay && SWIPE_TRIAL_REPLAY_TOUCH;
inline constexpr bool trial_replay_pair_fill = synthetic_trial_replay && SWIPE_TRIAL_REPLAY_PAIR_FILL;
inline constexpr bool trial_replay_staged_detail = direct_trial_replay && SWIPE_TRIAL_REPLAY_STAGED_DETAIL;
inline constexpr bool prefetch_portrait_companion =
    !SWIPE_TRIAL_REPLAY_DISABLE_LOOKAHEAD && !SWIPE_TRIAL_REPLAY_DISABLE_COMPANION_PREFETCH;
inline constexpr char replay_immich_key[] = "espframe-synthetic-preview";
inline constexpr char replay_slide_token[] = "espframe-synthetic-slide-replay-only";

}  // namespace espframe::native
