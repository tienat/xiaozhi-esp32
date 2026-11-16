#include "app_ui_onboarding.h"
#include "app_ui_onboarding_state.h"
#include "app_ui_theme.h"
#include "app_ui_i18n.h"
#include "app_network.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define AP_LIMIT 12

static lv_obj_t *s_root, *s_page, *s_list, *s_ssid_input, *s_password, *s_keyboard;
static lv_obj_t *s_secure, *s_summary, *s_home_name;
static onboarding_screen_t s_screen = ONBOARD_SPLASH;
static uint32_t s_boot_tick, s_phase_tick;
static bool s_edit_wifi, s_entered_home, s_boot_checked, s_was_connected;
static bool s_initial_snapshot_checked;
static bool s_manual, s_selected_secure, s_scan_requested;
static char s_ssid[33];
static wifi_c6_ap_t s_aps[AP_LIMIT];
static int s_ap_count;
static esp_err_t s_scan_result;
static atomic_bool s_scan_busy, s_scan_done;

static void show_page(onboarding_screen_t screen);
static void start_scan(void);

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int width,
                       uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_width(obj, width);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    return obj;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int width,
                        lv_event_cb_t cb, void *data)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, 52);
    lv_obj_set_style_bg_color(obj, lv_color_hex(APP_UI_ACCENT), 0);
    lv_obj_set_style_radius(obj, 26, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, data);
    lv_obj_t *text_obj = lv_label_create(obj);
    lv_label_set_text(text_obj, text);
    lv_obj_center(text_obj);
    return obj;
}

static lv_obj_t *button_dark(lv_obj_t *parent, const char *text, int x, int y, int width,
                             lv_event_cb_t cb, void *data)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, 52);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x242424), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x383838), 0);
    lv_obj_set_style_radius(obj, 26, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, data);
    lv_obj_t *text_obj = lv_label_create(obj);
    lv_label_set_text(text_obj, text);
    lv_obj_set_style_text_color(text_obj, lv_color_hex(APP_UI_TEXT), 0);
    lv_obj_center(text_obj);
    return obj;
}

static void scan_worker(void *arg)
{
    (void)arg;
    s_ap_count = 0;
    s_scan_result = app_network_scan(s_aps, AP_LIMIT, &s_ap_count);
    atomic_store_explicit(&s_scan_done, true, memory_order_release);
    vTaskDelete(NULL);
}

static void start_scan(void)
{
    s_scan_requested = true;
    if (!app_network_is_initialized() || atomic_load(&s_scan_busy)) return;
    atomic_store(&s_scan_busy, true);
    atomic_store(&s_scan_done, false);
    s_scan_requested = false;
    if (s_list) {
        lv_obj_clean(s_list);
        label(s_list, UI_SCANNING, 0, 10, 400, APP_UI_MUTED);
    }
    if (xTaskCreate(scan_worker, "ui_wifi_scan", 4096, NULL, 2, NULL) != pdPASS) {
        s_ap_count = 0;
        s_scan_result = ESP_ERR_NO_MEM;
        atomic_store_explicit(&s_scan_done, true, memory_order_release);
    }
}

static void on_scan(lv_event_t *event)
{
    (void)event;
    start_scan();
}

static void on_other(lv_event_t *event)
{
    (void)event;
    s_manual = true;
    s_selected_secure = true;
    s_ssid[0] = '\0';
    show_page(ONBOARD_PASSWORD);
}

static void on_ap(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    if (index < 0 || index >= s_ap_count || atomic_load(&s_scan_busy)) return;
    snprintf(s_ssid, sizeof(s_ssid), "%s", s_aps[index].ssid);
    s_selected_secure = s_aps[index].secure;
    s_manual = false;
    show_page(ONBOARD_PASSWORD);
}

static void on_back(lv_event_t *event)
{
    (void)event;
    s_edit_wifi = true;
    show_page(ONBOARD_SCAN);
    start_scan();
}

static void on_reenter(lv_event_t *event)
{
    (void)event;
    s_edit_wifi = true;
    s_manual = !s_ssid[0];
    s_selected_secure = true;
    show_page(ONBOARD_PASSWORD);
}

static void on_eye(lv_event_t *event)
{
    (void)event;
    lv_textarea_set_password_mode(s_password, !lv_textarea_get_password_mode(s_password));
}

static void on_focus(lv_event_t *event)
{
    lv_keyboard_set_textarea(s_keyboard, lv_event_get_target_obj(event));
    lv_obj_remove_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void on_security(lv_event_t *event)
{
    (void)event;
    bool secure = lv_obj_has_state(s_secure, LV_STATE_CHECKED);
    if (secure) lv_obj_remove_flag(s_password, LV_OBJ_FLAG_HIDDEN);
    else {
        lv_textarea_set_text(s_password, "");
        lv_obj_add_flag(s_password, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_keyboard, s_ssid_input);
    }
}

static void on_connect(lv_event_t *event)
{
    (void)event;
    if (s_manual) {
        snprintf(s_ssid, sizeof(s_ssid), "%s", lv_textarea_get_text(s_ssid_input));
        s_selected_secure = lv_obj_has_state(s_secure, LV_STATE_CHECKED);
    }
    if (!s_ssid[0]) return;
    const char *password = s_selected_secure ? lv_textarea_get_text(s_password) : "";
    /* WPA/WPA2 PSK, hoac 64 ky tu hex; mang mo khong can mat khau. */
    size_t length = strlen(password);
    if (s_selected_secure && length < 8) {
        lv_obj_set_style_border_color(s_password, lv_color_hex(APP_UI_ERROR), 0);
        return;
    }
    esp_err_t err = app_network_connect(s_ssid, password);
    lv_textarea_set_text(s_password, "");
    s_edit_wifi = false;
    s_phase_tick = lv_tick_get();
    show_page(err == ESP_OK ? ONBOARD_CONNECTING : ONBOARD_WIFI_ERROR);
}

static void on_time_retry(lv_event_t *event)
{
    (void)event;
    /* SNTP dang chay trong driver; mo lai cua so cho lan thu chu dong. */
    s_phase_tick = lv_tick_get();
    app_network_retry_time_sync();
    show_page(ONBOARD_CLOUD);
}

static void on_enter(lv_event_t *event)
{
    (void)event;
    s_entered_home = true;
    app_ui_refresh();
}

static void render_scan(void)
{
    if (!s_list) return;
    lv_obj_clean(s_list);
    if (s_scan_result != ESP_OK || !s_ap_count) {
        label(s_list, s_scan_result != ESP_OK ? UI_SCAN_ERROR : UI_NO_AP,
              0, 20, 400, APP_UI_MUTED);
        return;
    }
    for (int i = 0; i < s_ap_count; ++i) {
        lv_obj_t *row = button(s_list, "", 0, i * 58, 416, on_ap, (void *)(intptr_t)i);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(APP_UI_SURFACE), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        label(row, s_aps[i].ssid, 12, 14, 280, APP_UI_TEXT);
        char info[32];
        snprintf(info, sizeof(info), "%d %s", s_aps[i].rssi, s_aps[i].secure ? "\xEF\x80\xA3" : ">");
        label(row, info, 296, 14, 104, APP_UI_MUTED);
    }
}

static void progress(const char *title, const char *detail)
{
    lv_obj_t *spinner = lv_spinner_create(s_page);
    lv_obj_set_size(spinner, 64, 64);
    lv_obj_set_pos(spinner, 208, 120);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(APP_UI_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_INDICATOR);
    lv_obj_t *heading = label(s_page, title, 24, 215, 432, APP_UI_TEXT);
    lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *subtitle = label(s_page, detail, 24, 260, 432, APP_UI_MUTED);
    lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);
}

static lv_obj_t *input(const char *placeholder, int y, bool secret)
{
    lv_obj_t *obj = lv_textarea_create(s_page);
    lv_obj_set_pos(obj, 24, y);
    lv_obj_set_size(obj, secret ? 370 : 432, 52);
    lv_textarea_set_one_line(obj, true);
    lv_textarea_set_max_length(obj, secret ? 64 : 32);
    lv_textarea_set_placeholder_text(obj, placeholder);
    lv_textarea_set_password_mode(obj, secret);
    lv_textarea_set_password_show_time(obj, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(APP_UI_SURFACE), 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(APP_UI_TEXT), 0);
    lv_obj_add_event_cb(obj, on_focus, LV_EVENT_FOCUSED, NULL);
    return obj;
}

static void show_page(onboarding_screen_t screen)
{
    s_screen = screen;
    ESP_LOGI("onboarding", "Screen=%d", (int)screen);
    /* Xoa password truoc khi giai phong widget. */
    if (s_password) lv_textarea_set_text(s_password, "");
    lv_obj_clean(s_page);
    s_list = s_ssid_input = s_password = s_keyboard = s_secure = s_summary = s_home_name = NULL;
    lv_obj_remove_flag(s_page, LV_OBJ_FLAG_HIDDEN);
    if (screen == ONBOARD_SPLASH) {
        lv_obj_t *logo = label(s_page, "NOBLEX", 0, 175, 480, APP_UI_ACCENT);
        lv_obj_set_style_text_font(logo, &font_36, 0);
        lv_obj_set_style_text_align(logo, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t *line = lv_obj_create(s_page);
        lv_obj_set_size(line, 44, 2);
        lv_obj_set_pos(line, 218, 230);
        lv_obj_set_style_bg_color(line, lv_color_hex(APP_UI_ACCENT), 0);
        lv_obj_set_style_border_width(line, 0, 0);
        lv_obj_t *sub = label(s_page, "H O M E   P A N E L", 0, 250, 480, APP_UI_MUTED);
        lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    } else if (screen == ONBOARD_SCAN) {
        label(s_page, UI_WIFI, 24, 24, 432, APP_UI_TEXT);
        s_list = lv_obj_create(s_page);
        lv_obj_set_pos(s_list, 24, 76);
        lv_obj_set_size(s_list, 432, 312);
        lv_obj_set_style_bg_color(s_list, lv_color_hex(APP_UI_BG), 0);
        lv_obj_set_style_border_width(s_list, 0, 0);
        lv_obj_set_style_pad_all(s_list, 0, 0);
        label(s_list, UI_SCANNING, 0, 20, 400, APP_UI_MUTED);
        button_dark(s_page, UI_SCAN, 24, 408, 208, on_scan, NULL);
        button_dark(s_page, UI_OTHER, 248, 408, 208, on_other, NULL);
    } else if (screen == ONBOARD_PASSWORD) {
        button_dark(s_page, LV_SYMBOL_LEFT, 24, 16, 52, on_back, NULL);
        if (s_manual) {
            s_ssid_input = input(UI_SSID, 72, false);
            s_secure = lv_checkbox_create(s_page);
            lv_obj_set_pos(s_secure, 24, 126);
            lv_checkbox_set_text(s_secure, UI_SECURE);
            lv_obj_add_state(s_secure, LV_STATE_CHECKED);
            lv_obj_add_event_cb(s_secure, on_security, LV_EVENT_VALUE_CHANGED, NULL);
        } else {
            label(s_page, s_ssid, 88, 28, 368, APP_UI_TEXT);
        }
        s_password = input(UI_PASSWORD, s_manual ? 160 : 80, true);
        button(s_page, LV_SYMBOL_EYE_OPEN, 402, s_manual ? 160 : 80, 54, on_eye, NULL);
        label(s_page, UI_PASSWORD_MIN_LEN, 24, s_manual ? 218 : 138, 432, APP_UI_MUTED);
        if (!s_selected_secure) lv_obj_add_flag(s_password, LV_OBJ_FLAG_HIDDEN);
        s_keyboard = lv_keyboard_create(s_page);
        lv_obj_set_size(s_keyboard, 480, 196);
        lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(s_keyboard, lv_color_hex(APP_UI_SURFACE), 0);
        lv_obj_set_style_bg_color(s_keyboard, lv_color_hex(0x292929), LV_PART_ITEMS);
        lv_obj_set_style_text_color(s_keyboard, lv_color_hex(APP_UI_TEXT), LV_PART_ITEMS);
        lv_keyboard_set_textarea(s_keyboard, s_manual ? s_ssid_input : s_password);
        button_dark(s_page, UI_CONNECT, 248, 226, 208, on_connect, NULL);
        if (!s_selected_secure && !s_manual) {
            label(s_page, UI_OPEN, 24, 155, 432, APP_UI_MUTED);
            lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (screen == ONBOARD_WIFI_ERROR || screen == ONBOARD_TIME_ERROR) {
        label(s_page, screen == ONBOARD_WIFI_ERROR ? UI_WIFI_ERROR : UI_TIME_ERROR,
              24, 160, 432, APP_UI_TEXT);
        label(s_page, screen == ONBOARD_WIFI_ERROR ? UI_WIFI_HINT : UI_TIME_HINT,
              24, 218, 432, APP_UI_MUTED);
        button_dark(s_page, UI_OTHER, 24, 408, 208, on_back, NULL);
        button_dark(s_page, screen == ONBOARD_WIFI_ERROR ? UI_REENTER : UI_RETRY,
                    248, 408, 208, screen == ONBOARD_WIFI_ERROR ? on_reenter : on_time_retry, NULL);
    } else if (screen == ONBOARD_TIME) {
        progress(UI_TIME, UI_WIFI_OK);
    } else if (screen == ONBOARD_CONNECTING) {
        progress(UI_CONNECTING, s_ssid);
    } else if (screen == ONBOARD_CLOUD) {
        lv_obj_t *title = label(s_page, UI_CLOUD, 0, 36, 480, APP_UI_TEXT);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
        label(s_page, LV_SYMBOL_OK, 48, 110, 24, 0x00E676);
        label(s_page, UI_WIFI_OK, 80, 110, 360, 0x00E676);
        label(s_page, LV_SYMBOL_OK, 48, 170, 24, 0x00E676);
        label(s_page, UI_TIME_OK, 80, 170, 360, 0x00E676);
        label(s_page, "\xE2\x80\xA2", 52, 230, 20, APP_UI_ACCENT);
        label(s_page, UI_CLOUD_WAIT, 80, 230, 360, APP_UI_TEXT);
        label(s_page, "\xE2\x80\xA2", 52, 290, 20, APP_UI_MUTED);
        label(s_page, UI_GET_QR, 80, 290, 360, APP_UI_MUTED);
    } else if (screen == ONBOARD_SUCCESS) {
        lv_obj_t *icon = label(s_page, LV_SYMBOL_OK, 0, 100, 480, APP_UI_ACCENT);
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t *t = label(s_page, UI_LINKED, 0, 160, 480, APP_UI_ACCENT);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        s_home_name = label(s_page, "", 0, 212, 480, APP_UI_TEXT);
        lv_obj_set_style_text_align(s_home_name, LV_TEXT_ALIGN_CENTER, 0);
        s_summary = label(s_page, UI_SYNC_DATA, 0, 274, 480, APP_UI_MUTED);
        lv_obj_set_style_text_align(s_summary, LV_TEXT_ALIGN_CENTER, 0);
        button(s_page, UI_ENTER, 24, 408, 432, on_enter, NULL);
    }
}

void app_ui_show_splash(void)
{
    if (s_root) return;
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_root, 480, 480);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(APP_UI_BG), 0);
    lv_obj_set_style_text_color(s_root, lv_color_hex(APP_UI_TEXT), 0);
    lv_obj_set_style_text_font(s_root, &font_20, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    s_page = lv_obj_create(s_root);
    lv_obj_set_size(s_page, 480, 480);
    lv_obj_set_style_pad_all(s_page, 0, 0);
    lv_obj_set_style_border_width(s_page, 0, 0);
    lv_obj_set_style_radius(s_page, 0, 0);
    lv_obj_set_style_bg_color(s_page, lv_color_hex(APP_UI_BG), 0);
    lv_obj_remove_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
    s_boot_tick = s_phase_tick = lv_tick_get();
    show_page(ONBOARD_SPLASH);
}

lv_obj_t *app_ui_onboarding_root(void) { return s_root; }
void app_ui_onboarding_hide(void) { lv_obj_add_flag(s_page, LV_OBJ_FLAG_HIDDEN); }

app_ui_screen_t app_ui_onboarding_update(void)
{
    uint32_t now = lv_tick_get();
    bool connected = app_network_is_connected();
    if (connected && !s_was_connected) s_phase_tick = now;
    s_was_connected = connected;
    const app_snapshot_t *snapshot = app_gateway_get_snapshot();
    bool cached = snapshot && snapshot->home_id[0];
    if (!s_initial_snapshot_checked) {
        s_entered_home = cached;
        s_initial_snapshot_checked = true;
    }
    app_gw_state_t gw = app_gateway_get_state();
    if ((!cached && gw != GW_STATE_LINKED_RUNNING) || gw == GW_STATE_UNLINKED_PAIRING) {
        s_entered_home = false;
    }
    const app_pairing_session_t *session = app_gateway_get_pairing_session();
    bool ready = app_network_is_initialized();
    char saved_ssid[33] = {0};
    bool has_saved = app_network_get_ssid(saved_ssid, sizeof(saved_ssid)) && saved_ssid[0] != '\0';
    if (has_saved && !s_ssid[0]) {
        snprintf(s_ssid, sizeof(s_ssid), "%s", saved_ssid);
    }

    if (!s_boot_checked && ready && now - s_boot_tick >= 1200) {
        s_boot_checked = true;
        s_phase_tick = now;
        if (!has_saved && !cached && !connected) {
            s_edit_wifi = true;
            show_page(ONBOARD_SCAN);
            start_scan();
        }
    }
    if (atomic_exchange_explicit(&s_scan_done, false, memory_order_acquire)) {
        /* Khong ghi de bo dem AP khi danh sach dang duoc render/doc. */
        atomic_store(&s_scan_busy, false);
        if (s_screen == ONBOARD_SCAN) render_scan();
    }
    if (s_scan_requested && s_screen == ONBOARD_SCAN && !atomic_load(&s_scan_busy)) start_scan();

    onboarding_input_t in = {
        .splash_done = now - s_boot_tick >= 1200,
        .network_ready = ready,
        .has_saved_wifi = has_saved,
        .connected = connected,
        .time_synced = app_network_is_time_synced(),
        .wifi_failed = app_network_get_state() == WIFI_C6_FAILED,
        .cached_home = cached && gw != GW_STATE_UNLINKED_PAIRING,
        .linked = gw == GW_STATE_LINKED_RUNNING,
        .pairing = (session && (session->active || session->denied || session->retry_available)) ||
                   gw == GW_STATE_UNLINKED_PAIRING,
        .edit_wifi = s_edit_wifi,
        .entered_home = s_entered_home,
        .elapsed_ms = now - s_phase_tick,
    };
    onboarding_screen_t next = onboarding_decide(s_screen, &in);
    if (next != s_screen) {
        show_page(next);
        if (next == ONBOARD_SCAN) start_scan();
    }
    if (next == ONBOARD_SUCCESS && s_summary && snapshot) {
        lv_label_set_text(s_home_name, snapshot->home_name);
        if (app_gateway_has_snapshot()) {
            char counts[160];
            snprintf(counts, sizeof(counts), UI_COUNTS, (unsigned)snapshot->room_count,
                     (unsigned)snapshot->device_count, (unsigned)snapshot->scene_count);
            lv_label_set_text(s_summary, counts);
        } else lv_label_set_text(s_summary, UI_SYNC_DATA);
    }
    if (next == ONBOARD_HOME) return APP_UI_SCREEN_DASHBOARD;
    if (next == ONBOARD_PAIRING) return APP_UI_SCREEN_PAIRING;
    return APP_UI_SCREEN_ONBOARDING;
}
