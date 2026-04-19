cmake_minimum_required(VERSION 3.16)

# Add microlink-v2 component path
set(EXTRA_COMPONENT_DIRS
    "../../components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(microlink_v2_cellular_connect)
