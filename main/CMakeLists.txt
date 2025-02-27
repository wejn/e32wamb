idf_component_register(
    SRC_DIRS "."
    INCLUDE_DIRS "."
)

add_definitions(-DBUILD_DATE_CODE=${BUILD_TIMESTAMP})
add_definitions(-DBUILD_GIT_REV=${GIT_FULL_REV_ID})

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/trust_center_key.h")
    add_definitions(-DHAVE_TRUST_CENTER_KEY=1)
endif()
