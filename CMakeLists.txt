cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_SOURCE_DIR}/version.cmake)

set(PROJECT_VER "${BUILD_TIMESTAMP}-${GIT_FULL_REV_ID}")
message(STATUS "Project Version: ${PROJECT_VER}")
message(STATUS "Date code: ${BUILD_TIMESTAMP}")
message(STATUS "Git rev: ${GIT_FULL_REV_ID}")

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/light_driver
    )
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(e32wamb)

# Force reconfig every time
add_dependencies(e32wamb.elf force_reconfig)
