#pragma once

#include <string_view>

namespace espframe::native {

inline constexpr std::string_view trial_replay_origin = SWIPE_TRIAL_REPLAY_ORIGIN;
inline constexpr char replay_immich_key[] = "espframe-synthetic-preview";
inline constexpr char replay_authorization[] = "Bearer espframe-synthetic-slide-replay-only";

}  // namespace espframe::native
