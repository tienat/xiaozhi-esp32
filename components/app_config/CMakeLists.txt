idf_component_register(SRCS "app_config.c"
                        INCLUDE_DIRS "include"
                        PRIV_REQUIRES nvs_flash app_spiffs cjson log efuse)

# Chi them duong dan, khong dua bi mat vao command line cua compiler.
get_filename_component(HCP_LOCAL_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../_secrets" ABSOLUTE)
if(EXISTS "${HCP_LOCAL_CONFIG_DIR}/hcp_device_local.h")
    target_include_directories(${COMPONENT_LIB} PRIVATE "${HCP_LOCAL_CONFIG_DIR}")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HCP_HAS_LOCAL_DEVICE_CONFIG=1)
endif()
