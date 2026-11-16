#include "app_ui.h"
#include "app_ui_onboarding.h"
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <string.h>
#include <stdatomic.h>

#define TAG "app_ui"

static lv_obj_t *s_scr_pairing = NULL;
static lv_obj_t *s_scr_dashboard = NULL;
static lv_obj_t *s_ai_widget = NULL;
static app_ui_screen_t s_current_screen = APP_UI_SCREEN_PAIRING;
static bool s_initialized = false;
static atomic_bool s_refresh_pending = ATOMIC_VAR_INIT(false);
static lv_timer_t *s_refresh_timer = NULL;

static void request_ui_refresh(void)
{
    /* Callback Gateway dang giu mutex: chi gui co, khong cham LVGL. */
    if (!atomic_exchange_explicit(&s_refresh_pending, true, memory_order_acq_rel)) {
        ESP_LOGI(TAG, "Gateway update queued for LVGL refresh");
    }
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    app_ui_refresh();
}

void app_ui_switch_screen(app_ui_screen_t screen)
{
    if (!s_scr_pairing || !s_scr_dashboard) return;

    if (screen == APP_UI_SCREEN_ONBOARDING) {
        lv_obj_add_flag(s_scr_pairing, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_scr_dashboard, LV_OBJ_FLAG_HIDDEN);
        if (s_ai_widget) lv_obj_add_flag(s_ai_widget, LV_OBJ_FLAG_HIDDEN);
        s_current_screen = screen;
        return;
    }
    app_ui_onboarding_hide();

    if (screen == APP_UI_SCREEN_PAIRING) {
        lv_obj_remove_flag(s_scr_pairing, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_scr_dashboard, LV_OBJ_FLAG_HIDDEN);
        if (s_ai_widget) lv_obj_add_flag(s_ai_widget, LV_OBJ_FLAG_HIDDEN);
        s_current_screen = APP_UI_SCREEN_PAIRING;
    } else {
        lv_obj_add_flag(s_scr_pairing, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_scr_dashboard, LV_OBJ_FLAG_HIDDEN);
        if (s_ai_widget) lv_obj_remove_flag(s_ai_widget, LV_OBJ_FLAG_HIDDEN);
        s_current_screen = APP_UI_SCREEN_DASHBOARD;
    }
}

void app_ui_refresh(void)
{
    if (!s_initialized) return;

    if (lvgl_port_lock(0)) {
        bool dirty = atomic_exchange_explicit(&s_refresh_pending, false, memory_order_acq_rel);
        app_ui_screen_t next = app_ui_onboarding_update();
        bool changed = next != s_current_screen;
        app_ui_switch_screen(next);
        if (next == APP_UI_SCREEN_DASHBOARD && (dirty || changed)) app_ui_cards_update();
        if (next == APP_UI_SCREEN_PAIRING && (dirty || changed)) app_ui_pairing_update();

        lvgl_port_unlock();
    }
}

void app_ui_set_ai_state(const char *state)
{
    if (!s_initialized) return;

    if (lvgl_port_lock(0)) {
        app_ui_ai_widget_update(state);
        lvgl_port_unlock();
    }
}

esp_err_t app_ui_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "app_ui da duoc khoi tao");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Khoi tao giao dien Home Gateway & QR NobleX tren nen LVGL 9");

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "Khong the lock lvgl_port trong luc khoi tao UI");
        return ESP_FAIL;
    }

    app_ui_show_splash();
    lv_obj_t *act_scr = app_ui_onboarding_root();
    if (!act_scr) {
        lvgl_port_unlock();
        ESP_LOGE(TAG, "Khong co active screen nao trong LVGL");
        return ESP_ERR_INVALID_STATE;
    }

    s_refresh_timer = lv_timer_create(refresh_timer_cb, 100, NULL);
    if (!s_refresh_timer) {
        lvgl_port_unlock();
        ESP_LOGE(TAG, "Khong the tao timer refresh UI");
        return ESP_ERR_NO_MEM;
    }

    /* 1. Tao man hinh Pairing */
    s_scr_pairing = app_ui_pairing_create(act_scr);

    /* 2. Tao man hinh Dashboard */
    s_scr_dashboard = app_ui_cards_create(act_scr);

    /* 3. Tao widget Voice AI tren man hinh Dashboard */
    s_ai_widget = app_ui_ai_widget_create(s_scr_dashboard);

    /* Splash luon hien truoc QR/dashboard, ke ca da co cache nha. */
    app_ui_switch_screen(APP_UI_SCREEN_ONBOARDING);

    s_initialized = true;
    lvgl_port_unlock();

    /* Dang ky nhan callback khi Gateway cap nhat (snapshot, state, pairing session) */
    app_gateway_register_update_cb(request_ui_refresh);
    request_ui_refresh();

    ESP_LOGI(TAG, "app_ui khoi tao hoan tat thanh cong");
    return ESP_OK;
}
