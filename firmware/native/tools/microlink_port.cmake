# Adapt the pinned upstream's lwIP interface for ESP-IDF socket binding.
# Its manual netif insertion otherwise leaves netif->num at zero, potentially
# colliding with the LAN interface and invalidating interface-bound sockets.
idf_component_get_property(microlink_lib microlink COMPONENT_LIB)
idf_component_get_property(microlink_dir microlink COMPONENT_DIR)
set(microlink_wg_source "${microlink_dir}/src/ml_wg_mgr.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${microlink_wg_source}")
file(READ "${microlink_wg_source}" microlink_wireguard)
set(netif_insert "    ctx->netif->next = netif_list;")
set(peer_hostname "    strncpy(p->hostname, update->hostname, sizeof(p->hostname) - 1);")
foreach(expected IN ITEMS "${netif_insert}" "${peer_hostname}")
    string(FIND "${microlink_wireguard}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Review Espframe's MicroLink port against the updated upstream source: ${expected}")
    endif()
endforeach()
string(REPLACE "${netif_insert}" "    ctx->netif->num = 253;
${netif_insert}" microlink_wireguard "${microlink_wireguard}")
string(REPLACE "${peer_hostname}" "    strlcpy(p->hostname, update->hostname, sizeof(p->hostname));"
       microlink_wireguard "${microlink_wireguard}")
set(ported_microlink_wireguard "${CMAKE_CURRENT_BINARY_DIR}/microlink_wg_mgr.c")
file(CONFIGURE OUTPUT "${ported_microlink_wireguard}" CONTENT "@microlink_wireguard@" @ONLY)
get_target_property(microlink_sources ${microlink_lib} SOURCES)
list(FILTER microlink_sources EXCLUDE REGEX "(^|/)ml_wg_mgr[.]c$")
set_property(TARGET ${microlink_lib} PROPERTY SOURCES "${microlink_sources}")
target_sources(${microlink_lib} PRIVATE "${ported_microlink_wireguard}")
target_compile_options(${microlink_lib} PRIVATE -Os)
idf_component_get_property(native_main_lib main COMPONENT_LIB)
target_link_libraries(${native_main_lib} PRIVATE ${microlink_lib})
