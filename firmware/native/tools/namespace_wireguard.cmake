# ESPHome WireGuard 0.4.8 and MicroLink each provide a modified wireguard-lwip.
# Keep the ESPHome public esp_wireguard_* API and namespace its private symbols
# so neither library resolves calls against the other's structs or crypto.
set(wireguard_private_symbols
    blake2s blake2s_final blake2s_init blake2s_update
    chacha20 chacha20_init chacha20poly1305_decrypt chacha20poly1305_encrypt
    crypto_equal crypto_zero get_peer_keypair_for_idx hchacha20
    keypair_destroy keypair_update peer_alloc peer_lookup_by_handshake
    peer_lookup_by_peer_index peer_lookup_by_pubkey peer_lookup_by_receiver
    poly1305_finish poly1305_init poly1305_update
    wireguard_base64_decode wireguard_base64_encode wireguard_check_mac1
    wireguard_check_mac2 wireguard_check_replay wireguard_create_cookie_reply
    wireguard_create_handshake_initiation wireguard_create_handshake_response
    wireguard_decrypt_packet wireguard_device_init wireguard_encrypt_packet
    wireguard_expired wireguard_get_message_type wireguard_init wireguard_is_under_load
    wireguard_peer_index wireguard_peer_init wireguard_platform_init
    wireguard_process_cookie_message wireguard_process_handshake_response
    wireguard_process_initiation_message wireguard_random_bytes wireguard_start_session
    wireguard_sys_now wireguard_tai64n_now wireguardif_add_allowed_ip
    wireguardif_add_peer wireguardif_connect wireguardif_disconnect wireguardif_fini
    wireguardif_init wireguardif_latest_handshake wireguardif_network_rx
    wireguardif_peer_init wireguardif_peer_is_up wireguardif_remove_peer
    wireguardif_shutdown wireguardif_update_endpoint xchacha20poly1305_decrypt
    xchacha20poly1305_encrypt)
foreach(symbol IN LISTS wireguard_private_symbols)
    target_compile_definitions(${wireguard_lib} PRIVATE "${symbol}=espframe_wg_${symbol}")
endforeach()
