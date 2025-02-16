idf_component_register(
    SRC_DIRS "." "../zcl_utility/src"
    INCLUDE_DIRS "." "../zcl_utility/include"
)

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/trust_center_key.h")
    add_definitions(-DHAVE_TRUST_CENTER_KEY=1)
endif()
