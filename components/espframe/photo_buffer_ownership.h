#pragma once

#include <array>
#include <cstddef>

namespace esphome::espframe {

// A synchronous ownership snapshot. No buffer is evicted while its content is
// reserved by the slideshow, an outstanding action, or a display descriptor.
struct PortraitBufferOwners {
  bool quiescent;
  bool active_pair;
  bool preload_pair;
  bool detach_hidden_sources;
};

template<typename State>
PortraitBufferOwners portrait_buffer_owners(const State &state, bool pending_actions, bool queued_commands,
                                           bool portrait_widgets_hidden) {
  bool fetching = false;
  for (bool busy : state.slot_flags.fetch_in_flight) fetching |= busy;
  const auto &portrait = state.portrait;
  return {
    state.active_slot_displayed && !fetching && !pending_actions && !queued_commands &&
      state.noncritical_remote_updates_in_flight == 0 && !portrait.workflow_busy,
    (portrait.is_pair && !portrait.using_preload) || portrait.left_ready || portrait.right_ready ||
      portrait.left_requested || portrait.right_requested || portrait.companion_found,
    state.portrait_preload_slot >= 0 || state.portrait_preload_left_ready ||
      state.portrait_preload_right_ready || state.preload_noncritical_in_flight ||
      (portrait.is_pair && portrait.using_preload),
    portrait_widgets_hidden && !portrait.is_pair && state.active_slot_displayed,
  };
}

struct PhotoBufferReclaimResult {
  size_t retained_bytes = 0;
  size_t released_bytes = 0;
};

// Binding::prepare_release must reject live display references, detach sources
// that are safe to forget, and invalidate their cache before Image::release.
// The caller runs on ESPHome's main loop; there is no yield between these steps.
template<typename Image, typename Binding>
PhotoBufferReclaimResult reclaim_portrait_buffers(const PortraitBufferOwners &owners,
                                                  const std::array<Image *, 4> &images, Binding &binding) {
  PhotoBufferReclaimResult result;
  bool downloading = false;
  for (auto *image : images) downloading |= image->is_downloading();
  for (size_t i = 0; i < images.size(); ++i) {
    auto *image = images[i];
    const size_t bytes = image->get_buffer_capacity();
    result.retained_bytes += bytes;
    if (bytes == 0 || !owners.quiescent || downloading || (i < 2 ? owners.active_pair : owners.preload_pair))
      continue;
    if (!binding.prepare_release(image, owners.detach_hidden_sources)) continue;
    image->release();
    result.released_bytes += bytes;
    result.retained_bytes -= bytes;
  }
  return result;
}

}  // namespace esphome::espframe
