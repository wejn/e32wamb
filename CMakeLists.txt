cmake_minimum_required(VERSION 3.16)

# Mark the directories safe for git
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL "/project")
    execute_process(
        COMMAND git config --global --add safe.directory /project
        COMMAND git config --global --add safe.directory /opt/esp/idf/components/openthread/openthread
    )
endif()

# Gather build timestamp + git revision
string(TIMESTAMP BUILD_TIMESTAMP "%Y%m%d%H%M%S")

execute_process(
    COMMAND git rev-parse --is-inside-work-tree
    RESULT_VARIABLE GIT_REPO_CHECK
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE GIT_REPO_STATUS
)

set(GIT_DIRTY_SUFFIX "")
set(GIT_SHORT_REV "")

if(GIT_REPO_CHECK EQUAL 0 AND GIT_REPO_STATUS STREQUAL "true")
    execute_process(
        COMMAND git show -s --format=%h
        OUTPUT_STRIP_TRAILING_WHITESPACE
        OUTPUT_VARIABLE GIT_SHORT_REV
    )
    execute_process(
        COMMAND bash -c "git diff --quiet --ignore-submodules || echo '-dirty'"
        OUTPUT_STRIP_TRAILING_WHITESPACE
        OUTPUT_VARIABLE GIT_DIRTY_SUFFIX
    )
else()
    set(GIT_DIRTY_SUFFIX "nogit")
endif()

set(GIT_FULL_REV_ID "${GIT_SHORT_REV}${GIT_DIRTY_SUFFIX}")
set(PROJECT_VER "${BUILD_TIMESTAMP}-${GIT_FULL_REV_ID}")
message(STATUS "Project Version: ${PROJECT_VER}")
message(STATUS "Date code: ${BUILD_TIMESTAMP}")
message(STATUS "Git rev: ${GIT_FULL_REV_ID}")

add_custom_target(force_reconfig
    # I'm not proud of this, but it's needed to get a fresh version every time
    COMMAND touch ${CMAKE_CURRENT_SOURCE_DIR}/build/CMakeCache.txt
)

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/light_driver
    )
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(e32wamb)

# Force reconfig every time
add_dependencies(e32wamb.elf force_reconfig)
