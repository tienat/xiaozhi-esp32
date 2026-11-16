#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>
#include <lvgl.h>
#include "app_gateway.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_UI_SCREEN_PAIRING = 0,
    APP_UI_SCREEN_DASHBOARD,
    APP_UI_SCREEN_ONBOARDING,
} app_ui_screen_t;

typedef void (*app_ui_toggle_voice_cb_t)(void);

/**
 * @brief Khoi tao UI Subsystem tren nen LVGL 9
 */
esp_err_t app_ui_init(void);

/* Hien logo som khi LCD san sang, goi khi da giu LVGL lock. */
void app_ui_show_splash(void);

/**
 * @brief Chuyen doi man hinh hien thi
 */
void app_ui_switch_screen(app_ui_screen_t screen);

/**
 * @brief Lam moi toan bo UI tu Snapshot va Gateway State
 */
void app_ui_refresh(void);

/**
 * @brief Cap nhat trang thai Voice AI tren Widget
 */
void app_ui_set_ai_state(const char *state);

/**
 * @brief Dang ky callback khi cham vao AI Widget de toggle voice
 */
void app_ui_register_toggle_voice_cb(app_ui_toggle_voice_cb_t cb);

/**
 * @brief Trigger goi chuyen doi chat state
 */
void app_ui_toggle_voice_chat(void);

/* --- Cac ham con noi bo giua cac file UI --- */
lv_obj_t *app_ui_pairing_create(lv_obj_t *parent);
void app_ui_pairing_update(void);

lv_obj_t *app_ui_cards_create(lv_obj_t *parent);
void app_ui_cards_update(void);

lv_obj_t *app_ui_ai_widget_create(lv_obj_t *parent);
void app_ui_ai_widget_update(const char *state);

#ifdef __cplusplus
}
#endif
