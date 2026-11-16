idf_component_register(SRCS "app_gateway.c"
                       "app_gateway_http.c"
                       "app_gateway_mqtt.c"
                       "app_gateway_parser.c"
                       INCLUDE_DIRS "include" "."
                       PRIV_REQUIRES app_spiffs app_config app_network esp_http_client espressif__mqtt mbedtls cjson esp_event freertos log)
