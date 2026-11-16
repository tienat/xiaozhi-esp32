idf_component_register(SRCS "app_spiffs.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES spiffs esp_partition log)
