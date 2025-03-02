# This makes the following variables available:
# - BUILD_TIMESTAMP = "%Y%m%d%H%M%S" (UTC)
# - GIT_SHORT_REV = 7 char revision short
# - GIT_DIRTY_SUFFIX = "-dirty" if there are uncommitted changes
# - GIT_FULL_REV_ID = "${GIT_SHORT_REV}${GIT_DIRTY_SUFFIX}"
# which is useful for project versioning.
#
# If no git is available (or not within repo), GIT_DIRTY_SUFFIX
# and GIT_FULL_REV_ID are set to "nogit".
#
# Also, if this runs within docker (the project is under /project), then the
# /project dir and /opt/.../openthread are marked safe for git. Which avoids
# a verbose failure (to obtain the needed info).
#
# Also provides `force_reconfig` custom target that forces CMake reconfig every
# time a given dependency gets built. That's needed to keep the above variables
# fresh. But you need to plug it in as dependency of your main binary.
#
# ---
#
# Written in 2025 by Michal Jirků (wejn)
# License: CC0 or public domain (whatever's more pleasant for you)
#
# ---
#
# See
# https://wejn.org/2025/03/howto-timestamp-and-githash-esp32-firmware-builds/
# for background.

cmake_minimum_required(VERSION 3.16)

find_package(Git QUIET)

# Gather build timestamp + git revision
string(TIMESTAMP BUILD_TIMESTAMP "%Y%m%d%H%M%S")
set(GIT_DIRTY_SUFFIX "nogit")
set(GIT_SHORT_REV "")
set(GIT_FULL_REV_ID "nogit")

if(GIT_FOUND)
    # Mark the directories safe for git
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL "/project")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} config --global --add safe.directory /project
            COMMAND ${GIT_EXECUTABLE} config --global --add safe.directory /opt/esp/idf/components/openthread/openthread
        )
    endif()

    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --is-inside-work-tree
        RESULT_VARIABLE GIT_REPO_CHECK
        OUTPUT_STRIP_TRAILING_WHITESPACE
        OUTPUT_VARIABLE GIT_REPO_STATUS
    )

    if(GIT_REPO_CHECK EQUAL 0 AND GIT_REPO_STATUS STREQUAL "true")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} show -s --format=%h
            OUTPUT_STRIP_TRAILING_WHITESPACE
            OUTPUT_VARIABLE GIT_SHORT_REV
        )
        execute_process(
            COMMAND bash -c "${GIT_EXECUTABLE} diff --quiet --ignore-submodules || echo '-dirty'"
            OUTPUT_STRIP_TRAILING_WHITESPACE
            OUTPUT_VARIABLE GIT_DIRTY_SUFFIX
        )

        set(GIT_FULL_REV_ID "${GIT_SHORT_REV}${GIT_DIRTY_SUFFIX}")
    endif()
endif()


add_custom_target(force_reconfig
    # I'm not proud of this, but it's needed to get a fresh version every time
    COMMAND touch ${CMAKE_BINARY_DIR}/CMakeCache.txt
)
