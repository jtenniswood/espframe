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
    set(trial_source esphome_trial.cpp)
endif()

idf_component_register(
    SRCS app.cpp clock_overlay.cpp clock_settings.cpp daylight.cpp display.cpp frame_blend.cpp immich.cpp immich_http.cpp
         immich_memories.cpp immich_selection.cpp improv_serial.cpp native_network.cpp p4_jpeg_decoder.cpp playback_trace.cpp preview_decoder.cpp
         photo_caption.cpp recovery_card.cpp screen_tone.cpp settings.cpp touch.cpp
         firmware_update.cpp usb_setup.cpp web_settings.cpp
         ${trial_source}
         "${touch_source}/gsl_point_id.cpp"
    PRIV_INCLUDE_DIRS "${touch_source}" "${repo_root}/components/remote_image" "${repo_root}/components/espframe"
                      "${CMAKE_CURRENT_BINARY_DIR}"
    PRIV_REQUIRES esp_lcd esp_hw_support esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_timer esp_mm esp_driver_jpeg esp_driver_ppa
                  esp_driver_uart esp_event esp_http_client esp_http_server esp_netif esp_wifi mbedtls nvs_flash
                  app_update bootloader_support esp_app_format libjpeg-turbo-esp32 libwebp-esp32
    EMBED_TXTFILES web/settings.html web/settings.js web/recovery.html web/recovery.js
                   "../installer/setup.css" "../installer/photo_source.mjs"
                   "../installer/backup.mjs"
    EMBED_FILES "../assets/clock_glyphs.bin" "../assets/recovery_glyphs.bin" "../assets/caption_glyphs.bin"
)

if(SWIPE_BOARD STREQUAL "v3")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SWIPE_EARLY_P4=0)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SWIPE_EARLY_P4=1)
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE NATIVE_BOARD="${SWIPE_BOARD}" SWIPE_TRACE=$<BOOL:${SWIPE_TRACE}>
                                                   SWIPE_ESPHOME_TRIAL=$<BOOL:${SWIPE_ESPHOME_TRIAL}>)

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
