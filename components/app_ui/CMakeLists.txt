idf_component_register(SRCS "app_ui.c" "app_ui_onboarding.c" "app_ui_pairing.c" "app_ui_cards.c" "app_ui_ai_widget.c"
                       "../../../hcp_panel/main/fonts/font_16.c"
                       "../../../hcp_panel/main/fonts/font_20.c"
                       "../../../hcp_panel/main/fonts/font_36.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES app_gateway app_config app_network lvgl esp_lvgl_port freertos)
