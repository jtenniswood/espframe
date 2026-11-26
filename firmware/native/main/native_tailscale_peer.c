#include "microlink_internal.h"

// The upstream TCP helper primes WireGuard, but Espframe uses ESP-IDF's
// HTTP client to keep TLS, cancellation and persistent connections intact.
bool espframe_tailscale_prepare_peer(microlink_t *client, uint32_t peer) {
  if (ml_wg_mgr_peer_is_up(client, peer))
    return true;
  ml_wg_mgr_post_request(client, ML_PEER_REQ_HANDSHAKE, peer);
  return false;
}

void espframe_tailscale_reconnect_relay(microlink_t *client) {
  // The initial connection is requested after the complete netmap is parsed.
  if (xEventGroupGetBits(client->events) & ML_EVT_COORD_REGISTERED)
    xEventGroupSetBits(client->events, ML_EVT_DERP_RECONNECT);
}
