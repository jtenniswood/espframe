#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace esphome::espframe {

// Immediate calls only: no callbacks, allocation, or persistent pointer registry.
template<typename... Scripts> void stop_scripts_in_order(Scripts *...scripts) {
  (scripts->stop(), ...);
}

struct RotationPlan {
  int degrees;
  bool portrait_pairing;
  bool valid;
};
inline RotationPlan rotation_plan(const std::string &option, const std::array<int, 4> &panel_rotations) {
  if (option == "0") return {panel_rotations[0], true, true};
  if (option == "90") return {panel_rotations[1], false, true};
  if (option == "180") return {panel_rotations[2], true, true};
  if (option == "270") return {panel_rotations[3], false, true};
  return {0, false, false};
}
template<typename Display, typename PairingSwitch>
void apply_rotation_plan(const RotationPlan &plan, Display *display, PairingSwitch *pairing) {
  if (!plan.valid) return;
  display->set_rotation(plan.degrees);
  if (plan.portrait_pairing) pairing->turn_on();
  else pairing->turn_off();
}

enum class SlotFetchDecision : uint8_t {
  ALLOW, WIFI_DISCONNECTED, MISSING_CONFIG, PAUSED, COOLDOWN,
  PORTRAIT_BUSY, ANOTHER_FETCH_BUSY, SLOT_BUSY, IMAGE_BUSY, INVALID_SLOT,
};

// Preserve the YAML guard precedence. Evaluate slot flags only after the early
// gates, so a paused/disconnected request never depends on a usable target.
template<typename State>
SlotFetchDecision slot_fetch_decision(const State &state, bool wifi_connected, bool configured,
                                     bool paused, bool cooldown, bool image_downloading) {
  if (!wifi_connected) return SlotFetchDecision::WIFI_DISCONNECTED;
  if (!configured) return SlotFetchDecision::MISSING_CONFIG;
  if (paused) return SlotFetchDecision::PAUSED;
  if (cooldown) return SlotFetchDecision::COOLDOWN;
  if (state.target_slot != state.active_slot && state.portrait.workflow_busy)
    return SlotFetchDecision::PORTRAIT_BUSY;
  if (state.target_slot < 0 || state.target_slot > 2) return SlotFetchDecision::INVALID_SLOT;
  bool any_fetch = false;
  for (bool busy : state.slot_flags.fetch_in_flight) any_fetch |= busy;
  const bool slot_busy = state.slot_flags.fetch_in_flight[state.target_slot];
  if (any_fetch && !slot_busy) return SlotFetchDecision::ANOTHER_FETCH_BUSY;
  if (slot_busy) return SlotFetchDecision::SLOT_BUSY;
  if (image_downloading) return SlotFetchDecision::IMAGE_BUSY;
  return SlotFetchDecision::ALLOW;
}
inline bool slot_fetch_needs_defer(SlotFetchDecision decision) {
  return decision == SlotFetchDecision::PORTRAIT_BUSY || decision == SlotFetchDecision::ANOTHER_FETCH_BUSY ||
         decision == SlotFetchDecision::IMAGE_BUSY;
}

}  // namespace esphome::espframe
