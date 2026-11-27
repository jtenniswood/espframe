set(repo_root "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(touch_source "${repo_root}/components/gsl3680")
set(device guition-esp32-p4-jc8012p4a1)
if(SWIPE_BOARD STREQUAL "v1")
    set(original_model "${repo_root}/components/mipi_dsi/models/guition.py")
    set(original_arg --original-model "${original_model}")
else()
    string(APPEND device "-${SWIPE_BOARD}")
endif()
set(panel_profile "${repo_root}/devices/${device}/device/device.yaml")
set(panel_generator "${CMAKE_CURRENT_LIST_DIR}/../tools/generate_panel_init.py")
set(panel_init "${CMAKE_CURRENT_BINARY_DIR}/panel_init.inc")
set(clock_catalog "${repo_root}/components/espframe/timezones.py")
set(clock_generator "${CMAKE_CURRENT_LIST_DIR}/../tools/generate_clock_zones.py")
set(clock_zones "${CMAKE_CURRENT_BINARY_DIR}/native_clock_zones.inc")
if(SWIPE_ESPHOME_TRIAL)
    set(platform_source esphome_trial.cpp)
else()
    set(platform_source home_assistant_server.cpp stock_settings.cpp stock_frame_identity.cpp)
endif()
if(SWIPE_TRIAL_IMMICH)
    list(APPEND platform_source live_trial.cpp stock_frame_identity.cpp)
endif()

idf_component_register(
    SRCS app.cpp clock_overlay.cpp clock_settings.cpp daylight.cpp display.cpp frame_blend.cpp frame_blitter.cpp immich.cpp immich_http.cpp
         immich_memories.cpp immich_selection.cpp improv_serial.cpp native_configuration.cpp native_network.cpp native_release_index.cpp native_release_updates.cpp p4_jpeg_decoder.cpp
         playback_trace.cpp preview_decoder.cpp preview_prefetch.cpp
         photo_caption.cpp photo_controls.cpp recovery_card.cpp screen_tone.cpp settings.cpp touch.cpp
         firmware_update.cpp usb_setup.cpp web_settings.cpp
         ${platform_source}
         web_logs.cpp
         c6_firmware_manifest.cpp c6_firmware_update.cpp
         native_wireguard.cpp
         "${touch_source}/gsl_point_id.cpp"
    PRIV_INCLUDE_DIRS "${touch_source}" "${repo_root}/components/remote_image" "${repo_root}/components/espframe"
                      "${CMAKE_CURRENT_BINARY_DIR}"
    PRIV_REQUIRES esp_lcd esp_hw_support esp_driver_dma esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_timer esp_mm esp_driver_jpeg esp_driver_ppa
                  esp_driver_uart esp_event esp_http_client esp_http_server esp_netif esp_wifi mbedtls nvs_flash
                  app_update bootloader_support esp_app_format libjpeg-turbo-esp32 libwebp-esp32 home_assistant lwip
                  esphome__libsodium
    EMBED_TXTFILES web/native_settings.html web/native_settings.css web/shared_settings.js
                   web/admin_preview.html web/admin_cards.js web/admin/app.js
                   web/recovery.html web/recovery.css web/recovery.js
                   "${repo_root}/docs/public/webserver/style.css"
                   "../installer/setup.css"
    EMBED_FILES "../assets/clock_glyphs.bin" "../assets/recovery_glyphs.bin" "../assets/caption_glyphs.bin"
                "../assets/date_glyphs.bin" "../assets/status_masks.bin"
)

idf_component_get_property(wireguard_lib esphome__wireguard COMPONENT_LIB)
# Four configured networks and the library's entry for the frame address.
target_compile_definitions(${wireguard_lib} PRIVATE CONFIG_WIREGUARD_MAX_SRC_IPS=5)

# Emit native RGB565 words directly, avoiding a second pass through PSRAM.
idf_component_get_property(webp_lib libwebp-esp32 COMPONENT_LIB)
target_compile_definitions(${webp_lib} PRIVATE WEBP_SWAP_16BIT_CSP=1)

set(project_contract "${repo_root}/product/contract/project.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${project_contract}")
file(READ "${project_contract}" project_metadata)
string(JSON public_base_url GET "${project_metadata}" public_base_url)
set(native_release_feed "${public_base_url}/native-preview/")
if(NATIVE_LEGACY_REHEARSAL_FEED)
    set(native_release_feed "${NATIVE_LEGACY_REHEARSAL_FEED}")
elseif(NATIVE_STABLE_VERSION)
    set(native_release_feed "${public_base_url}/native-stable/")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE NATIVE_RELEASE_FEED="${native_release_feed}"
  NATIVE_STABLE_RELEASE=$<BOOL:${NATIVE_STABLE_VERSION}>
  NATIVE_LEGACY_REHEARSAL=$<BOOL:${NATIVE_LEGACY_REHEARSAL_FEED}>
  NATIVE_LEGACY_REHEARSAL_FAIL=$<BOOL:${NATIVE_LEGACY_REHEARSAL_FAIL}>)

set(device_catalog "${repo_root}/product/contract/devices.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${device_catalog}")
file(READ "${device_catalog}" profiles)
string(SUBSTRING "${SWIPE_BOARD}" 1 1 revision)
math(EXPR device_index "${revision} - 1")
string(JSON default_hostname GET "${profiles}" "${device_index}" esphome_name)
string(JSON default_friendly_name GET "${profiles}" "${device_index}" friendly_name)

if(SWIPE_BOARD STREQUAL "v3")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SWIPE_EARLY_P4=0)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SWIPE_EARLY_P4=1)
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE NATIVE_BOARD="${SWIPE_BOARD}" SWIPE_TRACE=$<BOOL:${SWIPE_TRACE}>
                                                   NATIVE_LEGACY_MIGRATION=$<BOOL:${NATIVE_LEGACY_MIGRATION}>
                                                   SWIPE_LINKED_SCANOUT=$<BOOL:${SWIPE_LINKED_SCANOUT}>
                                                   NATIVE_HOSTNAME="${default_hostname}" NATIVE_FRIENDLY_NAME="${default_friendly_name}"
                                                   SWIPE_ESPHOME_TRIAL=$<BOOL:${SWIPE_ESPHOME_TRIAL}>
                                                   SWIPE_TRIAL_IMMICH=$<BOOL:${SWIPE_TRIAL_IMMICH}>
                                                   SWIPE_TRIAL_BENCHMARK=$<BOOL:${SWIPE_TRIAL_BENCHMARK}>
                                                   SWIPE_TRIAL_BENCHMARK_RAPID=$<BOOL:${SWIPE_TRIAL_BENCHMARK_RAPID}>
                                                   SWIPE_TRIAL_WIFI_OUTAGE=$<BOOL:${SWIPE_TRIAL_WIFI_OUTAGE}>
                                                   SWIPE_TRIAL_REPLAY_ORIGIN="${SWIPE_TRIAL_REPLAY_ORIGIN}"
                                                   SWIPE_TRIAL_REPLAY_TOUCH=$<BOOL:${SWIPE_TRIAL_REPLAY_TOUCH}>
                                                   SWIPE_TRIAL_REPLAY_PREPARED=$<BOOL:${SWIPE_TRIAL_REPLAY_PREPARED}>
                                                   SWIPE_TRIAL_REPLAY_HISTORY=$<BOOL:${SWIPE_TRIAL_REPLAY_HISTORY}>
                                                   SWIPE_TRIAL_REPLAY_DISABLE_LOOKAHEAD=$<BOOL:${SWIPE_TRIAL_REPLAY_DISABLE_LOOKAHEAD}>
                                                   SWIPE_TRIAL_REPLAY_DISABLE_COMPANION_PREFETCH=$<BOOL:${SWIPE_TRIAL_REPLAY_DISABLE_COMPANION_PREFETCH}>
                                                   SWIPE_TRIAL_REPLAY_PAIR_FILL=$<BOOL:${SWIPE_TRIAL_REPLAY_PAIR_FILL}>
                                                   SWIPE_TRIAL_REPLAY_STAGED_DETAIL=$<BOOL:${SWIPE_TRIAL_REPLAY_STAGED_DETAIL}>
                                                   SWIPE_TRIAL_REPLAY_EDGE_DETAIL=$<BOOL:${SWIPE_TRIAL_REPLAY_EDGE_DETAIL}>
                                                   SWIPE_NATIVE_OTA_TEST=$<NOT:$<STREQUAL:${SWIPE_NATIVE_OTA_TEST},OFF>>
                                                   SWIPE_NATIVE_OTA_TEST_FAIL=$<STREQUAL:${SWIPE_NATIVE_OTA_TEST},FAIL>
                                                   SWIPE_TRIAL_BENCHMARK_FIXTURES=$<BOOL:${SWIPE_TRIAL_BENCHMARK_FIXTURES}>)

idf_build_get_property(python PYTHON)
add_custom_command(
    OUTPUT "${panel_init}"
    COMMAND "${python}" "${panel_generator}" "${panel_profile}" "${panel_init}" ${original_arg}
    DEPENDS "${panel_generator}" "${panel_profile}" ${original_model}
    VERBATIM
)
set_source_files_properties(display.cpp PROPERTIES OBJECT_DEPENDS "${panel_init}")
add_custom_command(
    OUTPUT "${clock_zones}"
    COMMAND "${python}" "${clock_generator}" --header "${clock_zones}"
    DEPENDS "${clock_generator}" "${clock_catalog}"
    VERBATIM
)
set_source_files_properties(clock_settings.cpp PROPERTIES OBJECT_DEPENDS "${clock_zones}")

# Probe with personal fixtures without adding them to the repository. The two
# required dimensions are already native-MCU-aligned for 4:2:0 JPEG.
foreach(index RANGE 0 4)
    set(photo "${CMAKE_CURRENT_LIST_DIR}/../photos/slide-${index}.jpg")
    if(EXISTS "${photo}")
        target_add_binary_data(${COMPONENT_LIB} "${photo}" BINARY)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE SWIPE_PHOTO_${index}=1)
    endif()
endforeach()
