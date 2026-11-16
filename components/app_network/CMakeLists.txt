idf_component_register(SRCS "app_network.c"
                       INCLUDE_DIRS "include"
                       REQUIRES wifi_c6
                       PRIV_REQUIRES esp_event esp_timer esp_netif log freertos)

# Chi them duong dan, khong dua bi mat vao command line cua compiler.
get_filename_component(HCP_LOCAL_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../_secrets" ABSOLUTE)
if(EXISTS "${HCP_LOCAL_CONFIG_DIR}/hcp_device_local.h")
    target_include_directories(${COMPONENT_LIB} PRIVATE "${HCP_LOCAL_CONFIG_DIR}")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HCP_HAS_LOCAL_DEVICE_CONFIG=1)
endif()
