# Compile a patched copy of just the DPI driver, using the same ESP-IDF target
# and dependencies. The SDK installation is shared with ordinary builds.
idf_component_get_property(lcd_library esp_lcd COMPONENT_LIB)
idf_component_get_property(lcd_directory esp_lcd COMPONENT_DIR)
set(original_dpi "${lcd_directory}/dsi/esp_lcd_panel_dpi.c")
file(SHA256 "${original_dpi}" original_sha)
if(NOT original_sha STREQUAL "51c49c410db7d072af3f1d3257bb47b8f57908d8c949bff0f9d60ea905a1e8ac")
    message(FATAL_ERROR "Linked scanout requires the unmodified ESP-IDF v6.1.1 DPI source.")
endif()

set(linked_dpi_dir "${CMAKE_BINARY_DIR}/linked-dpi")
set(linked_dpi_source "${linked_dpi_dir}/esp_lcd_panel_dpi.c")
set(linked_dpi_patch "${CMAKE_CURRENT_LIST_DIR}/esp-idf-v6.1.1-linked-dpi.patch")
file(MAKE_DIRECTORY "${linked_dpi_dir}")
file(COPY_FILE "${original_dpi}" "${linked_dpi_source}")
find_package(Git REQUIRED)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
                "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}" "${GIT_EXECUTABLE}" apply "${linked_dpi_patch}"
                WORKING_DIRECTORY "${linked_dpi_dir}" COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original_dpi}" "${linked_dpi_patch}")

get_target_property(lcd_sources "${lcd_library}" SOURCES)
if(NOT original_dpi IN_LIST lcd_sources)
    message(FATAL_ERROR "Could not select the pinned SDK DPI source.")
endif()
list(REMOVE_ITEM lcd_sources "${original_dpi}")
list(APPEND lcd_sources "${linked_dpi_source}")
set_property(TARGET "${lcd_library}" PROPERTY SOURCES "${lcd_sources}")
target_include_directories("${lcd_library}" PRIVATE "${lcd_directory}/dsi" "${CMAKE_CURRENT_LIST_DIR}/../main")
