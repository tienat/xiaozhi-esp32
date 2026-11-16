#pragma once
#include "app_ui.h"

/* Tat ca ham UI duoc goi khi da giu LVGL lock. */
lv_obj_t *app_ui_onboarding_root(void);
app_ui_screen_t app_ui_onboarding_update(void);
void app_ui_onboarding_hide(void);
