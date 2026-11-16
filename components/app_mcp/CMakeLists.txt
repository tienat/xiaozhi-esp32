idf_component_register(SRCS "app_mcp.cc"
                       INCLUDE_DIRS "include" "${PROJECT_DIR}/main"
                       PRIV_REQUIRES app_gateway cjson log mbedtls)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++23>)
endif()
