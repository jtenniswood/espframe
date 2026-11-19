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

idf_component_register(
    SRCS app.cpp display.cpp immich.cpp preview_decoder.cpp settings.cpp touch.cpp usb_setup.cpp
         "${touch_source}/gsl_point_id.cpp"
    PRIV_INCLUDE_DIRS "${touch_source}" "${repo_root}/components/remote_image" "${CMAKE_CURRENT_BINARY_DIR}"
    PRIV_REQUIRES esp_lcd esp_hw_support esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_timer esp_mm esp_driver_jpeg
                  esp_driver_uart esp_event esp_http_client esp_netif esp_wifi mbedtls nvs_flash
                  libjpeg-turbo-esp32 libwebp-esp32
)

if(SWIPE_BOARD STREQUAL "v3")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SWIPE_EARLY_P4=0)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SWIPE_EARLY_P4=1)
endif()

idf_build_get_property(python PYTHON)
add_custom_command(
    OUTPUT "${panel_init}"
    COMMAND "${python}" "${panel_generator}" "${panel_profile}" "${panel_init}" ${original_arg}
    DEPENDS "${panel_generator}" "${panel_profile}" ${original_model}
    VERBATIM
)
set_source_files_properties(display.cpp PROPERTIES OBJECT_DEPENDS "${panel_init}")

# Probe with personal fixtures without adding them to the repository. The two
# required dimensions are already native-MCU-aligned for 4:2:0 JPEG.
foreach(index RANGE 0 4)
    set(photo "${CMAKE_CURRENT_LIST_DIR}/../photos/slide-${index}.jpg")
    if(EXISTS "${photo}")
        target_add_binary_data(${COMPONENT_LIB} "${photo}" BINARY)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE SWIPE_PHOTO_${index}=1)
    endif()
endforeach()
