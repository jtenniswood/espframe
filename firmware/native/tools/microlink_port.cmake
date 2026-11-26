# Apply Espframe adaptations to configure-time copies of the pinned sources.
idf_component_get_property(microlink_lib microlink COMPONENT_LIB)
idf_component_get_property(microlink_dir microlink COMPONENT_DIR)

function(microlink_replace source expected replacement)
    string(FIND "${${source}}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Review Espframe's MicroLink port against the updated upstream source: ${expected}")
    endif()
    string(REPLACE "${expected}" "${replacement}" updated "${${source}}")
    set(${source} "${updated}" PARENT_SCOPE)
endfunction()

function(microlink_read filename output)
    set(upstream "${microlink_dir}/src/${filename}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${upstream}")
    file(READ "${upstream}" upstream_source)
    set(${output} "${upstream_source}" PARENT_SCOPE)
endfunction()

function(microlink_use filename source)
    set(ported "${CMAKE_CURRENT_BINARY_DIR}/${filename}")
    file(CONFIGURE OUTPUT "${ported}" CONTENT "@${source}@" @ONLY)
    get_target_property(sources ${microlink_lib} SOURCES)
    list(FILTER sources EXCLUDE REGEX "(^|/)${filename}$")
    set_property(TARGET ${microlink_lib} PROPERTY SOURCES "${sources}")
    target_sources(${microlink_lib} PRIVATE "${ported}")
endfunction()

# A unique netif number keeps interface-bound ESP-IDF sockets off the LAN.
microlink_read(ml_wg_mgr.c wireguard)
set(netif_insert "    ctx->netif->next = netif_list;")
microlink_replace(wireguard "${netif_insert}" "    ctx->netif->num = 253;\n${netif_insert}")
microlink_replace(wireguard "    strncpy(p->hostname, update->hostname, sizeof(p->hostname) - 1);"
    "    strlcpy(p->hostname, update->hostname, sizeof(p->hostname));")
set(add_peer "static int add_peer(microlink_t *ml, const ml_peer_update_t *update) {")
microlink_replace(wireguard "${add_peer}"
    "extern uint32_t espframe_tailscale_priority_peer(void);\n${add_peer}\n    const uint32_t priority_peer = espframe_tailscale_priority_peer();")
microlink_replace(wireguard "ml->config.priority_peer_ip" "priority_peer")
if(NATIVE_TAILSCALE_RELAY_TEST)
    if(CONFIG_ML_ZERO_COPY_WG)
        message(FATAL_ERROR "Relay testing requires the queue-based WireGuard receive path")
    endif()
    set(udp_callback "    wireguardif_set_udp_output_pbuf(netif, wg_udp_output_pbuf_cb, ml);")
    microlink_replace(wireguard "${udp_callback}" "${udp_callback}\n    wireguardif_force_derp_output(netif, true);")
    microlink_replace(wireguard "wireguardif_force_derp_output(netif, at_ready);" "wireguardif_force_derp_output(netif, true);")

    # Keep STUN for coordination but drop all direct discovery and WireGuard
    # packets so the peer can only reach this test build via a relay.
    microlink_read(ml_net_io.c net_io)
    set(classify "    pkt_type_t type = classify_packet(data, len);")
    microlink_replace(net_io "${classify}" "${classify}\n    if (type != PKT_STUN) { free(data); return; }")
    microlink_use(ml_net_io.c net_io)
endif()
microlink_use(ml_wg_mgr.c wireguard)

# Read the full MagicDNS name from the authenticated netmap before the
# upstream parser truncates it for the fixed-size peer record.
microlink_read(ml_coord.c coord)
set(parse_peers "static void parse_peers_from_map_response(microlink_t *ml, cJSON *root) {")
microlink_replace(coord "${parse_peers}"
    "extern void espframe_tailscale_map_peer(microlink_t *, const char *, uint32_t, uint16_t);\n${parse_peers}")
set(queue_peer "        /* Send to wg_mgr task via queue */")
microlink_replace(coord "${queue_peer}"
    "        espframe_tailscale_map_peer(ml, name ? name->valuestring : NULL, update->vpn_ip, update->derp_region);\n${queue_peer}")
microlink_use(ml_coord.c coord)

# MicroLink uses one relay connection for outgoing encrypted packets. Use the
# configured peer's advertised region when it is available in the relay map.
microlink_read(ml_derp.c derp)
set(derp_connect "esp_err_t ml_derp_connect(microlink_t *ml) {")
microlink_replace(derp "${derp_connect}"
    "extern uint16_t espframe_tailscale_relay_region(void);\n${derp_connect}")
set(relay_region "    uint16_t derp_region_used = ml->derp_home_region ? ml->derp_home_region : ML_DERP_REGION;")
microlink_replace(derp "${relay_region}"
    "${relay_region}\n    const uint16_t peer_region = espframe_tailscale_relay_region();\n    for (int i = 0; peer_region && i < ml->derp_region_count; ++i) {\n        if (ml->derp_regions[i].region_id == peer_region) {\n            derp_region_used = peer_region;\n            break;\n        }\n    }")
microlink_replace(derp "if (ml->derp_region_count > 0 && ml->derp_home_region > 0)"
    "if (ml->derp_region_count > 0)")
microlink_replace(derp "if (ml->derp_regions[i].region_id == ml->derp_home_region)"
    "if (ml->derp_regions[i].region_id == derp_region_used)")
microlink_use(ml_derp.c derp)

target_compile_options(${microlink_lib} PRIVATE -Os)
idf_component_get_property(native_main_lib main COMPONENT_LIB)
target_link_libraries(${native_main_lib} PRIVATE ${microlink_lib})
