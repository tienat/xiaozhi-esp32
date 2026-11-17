#include "app_ui.h"
#include "app_ui_theme.h"
#include "app_ui_i18n.h"
#include "app_network.h"
#include <esp_log.h>
#include <esp_system.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TAG "app_ui_cards"

/* Root dashboard va Tileview 4 trang */
static lv_obj_t *s_cards_root = NULL;
static lv_obj_t *s_tileview = NULL;
static lv_obj_t *s_tile_home = NULL;
static lv_obj_t *s_tile_device = NULL;
static lv_obj_t *s_tile_scene = NULL;
static lv_obj_t *s_tile_settings = NULL;

/* Page Indicator (4 dots o day) */
static lv_obj_t *s_dots_cont = NULL;
static lv_obj_t *s_dots[4] = {NULL, NULL, NULL, NULL};
static uint8_t s_current_tile_idx = 0;

/* Toast Notification Banner */
static lv_obj_t *s_toast_banner = NULL;
static lv_obj_t *s_lbl_toast = NULL;
static lv_timer_t *s_toast_timer = NULL;

/* --- TILE 0: HOME SCREEN WIDGETS --- */
static lv_obj_t *s_home_lbl_name = NULL;
static lv_obj_t *s_home_lbl_status = NULL;
static lv_obj_t *s_home_lbl_clock = NULL;
static lv_obj_t *s_home_lbl_date = NULL;
static lv_obj_t *s_home_lbl_ac_temp = NULL;
static lv_obj_t *s_home_lbl_light_count = NULL;
static lv_obj_t *s_home_lbl_light_sub = NULL;
static lv_obj_t *s_home_quick_devs_cont = NULL;
static lv_obj_t *s_home_quick_scenes_cont = NULL;
static int s_ac_setpoint = 25;

/* --- TILE 1: DEVICE SCREEN WIDGETS --- */
static lv_obj_t *s_dev_lbl_subtitle = NULL;
static lv_obj_t *s_room_bar = NULL;
static lv_obj_t *s_cards_container = NULL;
static int s_selected_room_idx = -1; /* -1: Tat ca */

/* --- OVERLAY: MULTI-BUTTON SCREEN (many_button.png) --- */
static lv_obj_t *s_detail_overlay = NULL;
static lv_obj_t *s_detail_title = NULL;
static lv_obj_t *s_detail_cols_cont = NULL;
static char s_detail_device_id[64] = {0};

/* --- TILE 2: SCENES SCREEN WIDGETS --- */
static lv_obj_t *s_scene_lbl_subtitle = NULL;
static lv_obj_t *s_scenes_grid = NULL;

/* --- TILE 3: SETTINGS SCREEN WIDGETS --- */
static lv_obj_t *s_settings_lbl_wifi = NULL;
static lv_obj_t *s_settings_lbl_ip = NULL;
static lv_obj_t *s_settings_lbl_cloud = NULL;

/* Context tham chieu dieu khien nut & endpoint an toan khong leak */
typedef struct {
    char device_id[64];
    char endpoint_id[16];
    bool target_state;
} dev_ep_cmd_t;

typedef struct {
    char scene_id[64];
    char scene_name[48];
} scene_cmd_t;

/* Ham tu dong giai phong bo nho heap cua user_data khi LVGL huy object */
static void on_obj_delete_free_ud(lv_event_t *e)
{
    void *ud = lv_event_get_user_data(e);
    if (ud) {
        free(ud);
    }
}

/* ========================================================================= */
/* Toast Notification Banner                                                 */
/* ========================================================================= */
static void toast_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_toast_banner) {
        lv_obj_add_flag(s_toast_banner, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_toast_timer) {
        lv_timer_delete(s_toast_timer);
        s_toast_timer = NULL;
    }
}

static void show_toast(const char *msg)
{
    if (!s_toast_banner || !s_lbl_toast || !msg) return;

    lv_label_set_text(s_lbl_toast, msg);
    lv_obj_remove_flag(s_toast_banner, LV_OBJ_FLAG_HIDDEN);

    if (s_toast_timer) {
        lv_timer_reset(s_toast_timer);
    } else {
        s_toast_timer = lv_timer_create(toast_timer_cb, 2500, NULL);
    }
}

/* ========================================================================= */
/* Page Indicator Dots                                                       */
/* ========================================================================= */
static void update_page_dots(uint8_t active_idx)
{
    s_current_tile_idx = active_idx;
    for (int i = 0; i < 4; i++) {
        if (!s_dots[i]) continue;
        if (i == active_idx) {
            lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_size(s_dots[i], 16, 4);
        } else {
            lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(0x475569), 0);
            lv_obj_set_size(s_dots[i], 5, 4);
        }
    }
}

static void on_tileview_scroll_end(lv_event_t *e)
{
    lv_obj_t *tv = lv_event_get_target(e);
    int32_t sx = lv_obj_get_scroll_x(tv);
    int page = (sx + 240) / 480;
    if (page < 0) page = 0;
    if (page > 3) page = 3;
    update_page_dots((uint8_t)page);
}

/* ========================================================================= */
/* Helper thoi gian va dong bo                                              */
/* ========================================================================= */
static void update_home_clock(void)
{
    if (!s_home_lbl_clock || !s_home_lbl_date) return;

    time_t now = 0;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char time_str[16];
    char date_str[64];

    if (timeinfo.tm_year > (2020 - 1900)) {
        snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        const char *wday_names[] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};
        const char *wday = (timeinfo.tm_wday >= 0 && timeinfo.tm_wday <= 6) ? wday_names[timeinfo.tm_wday] : "Hôm nay";
        snprintf(date_str, sizeof(date_str), "%s\n%d tháng %d", wday, timeinfo.tm_mday, timeinfo.tm_mon + 1);
    } else {
        snprintf(time_str, sizeof(time_str), "18:31");
        snprintf(date_str, sizeof(date_str), "Thứ Năm\n12 tháng 11");
    }

    lv_label_set_text(s_home_lbl_clock, time_str);
    lv_label_set_text(s_home_lbl_date, date_str);
}

static void async_refresh_cards_cb(void *user_data)
{
    (void)user_data;
    app_ui_cards_update();
}

static void render_multi_button_columns(const app_device_t *dev);

/* ========================================================================= */
/* Callbacks Dieu khien Thiet bi & Canh                                      */
/* ========================================================================= */
static void on_ep_toggle_clicked(lv_event_t *e)
{
    dev_ep_cmd_t *cmd = (dev_ep_cmd_t *)lv_event_get_user_data(e);
    if (!cmd) return;

    const char *payload = cmd->target_state ? "true" : "false";
    ESP_LOGI(TAG, "Dieu khien On/Off [%s/%s]: %s", cmd->device_id, cmd->endpoint_id, payload);
    app_gateway_send_command(cmd->device_id, cmd->endpoint_id, "on_off", payload);
    lv_async_call(async_refresh_cards_cb, NULL);
}

static void on_all_lights_off_clicked(lv_event_t *e)
{
    (void)e;
    const app_snapshot_t *snap = app_gateway_get_snapshot();
    if (!snap) return;

    ESP_LOGI(TAG, "Nguoi dung bam 'Tat het' den");
    for (uint16_t i = 0; i < snap->device_count; i++) {
        const app_device_t *dev = &snap->devices[i];
        if (strcmp(dev->category, "light") == 0) {
            for (uint8_t ep = 0; ep < dev->endpoint_count; ep++) {
                const app_endpoint_t *endpoint = &dev->endpoints[ep];
                for (uint8_t tr = 0; tr < endpoint->trait_count; tr++) {
                    if (endpoint->traits[tr].type == TRAIT_TYPE_ON_OFF) {
                        app_gateway_send_command(dev->id, endpoint->id, "on_off", "false");
                    }
                }
            }
        }
    }
    show_toast("✓ Đã tắt toàn bộ đèn");
    lv_async_call(async_refresh_cards_cb, NULL);
}

static void on_ac_temp_btn_clicked(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    s_ac_setpoint += delta;
    if (s_ac_setpoint < 16) s_ac_setpoint = 16;
    if (s_ac_setpoint > 30) s_ac_setpoint = 30;

    if (s_home_lbl_ac_temp) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d°", s_ac_setpoint);
        lv_label_set_text(s_home_lbl_ac_temp, buf);
    }
}

static void on_scene_clicked(lv_event_t *e)
{
    scene_cmd_t *sc = (scene_cmd_t *)lv_event_get_user_data(e);
    if (!sc) return;

    ESP_LOGI(TAG, "Kich hoat canh: %s (%s)", sc->scene_name, sc->scene_id);
    app_gateway_run_scene(sc->scene_id);

    char toast_msg[80];
    snprintf(toast_msg, sizeof(toast_msg), "✓ Đã chạy cảnh %s", sc->scene_name);
    show_toast(toast_msg);
}

/* ========================================================================= */
/* Overlay Multi-Button Screen (many_button.png)                             */
/* ========================================================================= */
static void on_close_detail_clicked(lv_event_t *e)
{
    (void)e;
    if (s_detail_overlay) {
        lv_obj_add_flag(s_detail_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    s_detail_device_id[0] = '\0';
}

static void on_detail_batch_all_clicked(lv_event_t *e)
{
    bool target_on = (bool)(intptr_t)lv_event_get_user_data(e);
    const app_snapshot_t *snap = app_gateway_get_snapshot();
    if (!snap || strlen(s_detail_device_id) == 0) return;

    const app_device_t *target_dev = NULL;
    for (uint16_t i = 0; i < snap->device_count; i++) {
        if (strcmp(snap->devices[i].id, s_detail_device_id) == 0) {
            target_dev = &snap->devices[i];
            break;
        }
    }
    if (!target_dev) return;

    const char *payload = target_on ? "true" : "false";
    for (uint8_t ep = 0; ep < target_dev->endpoint_count; ep++) {
        app_gateway_send_command(target_dev->id, target_dev->endpoints[ep].id, "on_off", payload);
    }

    show_toast(target_on ? "✓ Đã bật tất cả nút" : "✓ Đã tắt tất cả nút");
    lv_async_call(async_refresh_cards_cb, NULL);
}

static void render_multi_button_columns(const app_device_t *dev)
{
    if (!s_detail_cols_cont || !dev) return;

    lv_obj_clean(s_detail_cols_cont);

    uint8_t count = dev->endpoint_count;
    if (count == 0) count = 1;
    if (count > 4) count = 4;

    /* Tinh toan chieu rong cot */
    int col_widths[5] = {200, 200, 204, 136, 102};
    int w = col_widths[count];

    for (uint8_t ep = 0; ep < count; ep++) {
        const app_endpoint_t *endpoint = &dev->endpoints[ep];
        bool is_on = false;
        for (uint8_t tr = 0; tr < endpoint->trait_count; tr++) {
            if (endpoint->traits[tr].type == TRAIT_TYPE_ON_OFF && endpoint->traits[tr].state_valid) {
                is_on = endpoint->traits[tr].state.b_val;
                break;
            }
        }

        /* Cot dung dai */
        lv_obj_t *col = lv_button_create(s_detail_cols_cont);
        lv_obj_set_size(col, w, 282);
        lv_obj_set_style_radius(col, 24, 0);
        lv_obj_set_style_border_width(col, is_on ? 0 : 1, 0);
        lv_obj_set_style_border_color(col, lv_color_hex(0x2C3444), 0);
        lv_obj_set_style_bg_color(col, is_on ? lv_color_hex(0xF7F2EB) : lv_color_hex(0x1F232C), 0);
        lv_obj_set_style_pad_all(col, 8, 0);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

        /* Do bong noi (Elevation Shadow) cho cot nut nhan */
        lv_obj_set_style_shadow_width(col, is_on ? 22 : 14, 0);
        lv_obj_set_style_shadow_spread(col, is_on ? 2 : 0, 0);
        lv_obj_set_style_shadow_offset_y(col, is_on ? 8 : 5, 0);
        lv_obj_set_style_shadow_offset_x(col, 0, 0);
        lv_obj_set_style_shadow_color(col, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(col, is_on ? LV_OPA_60 : LV_OPA_40, 0);

        /* Vach notch ngang tren dinh */
        lv_obj_t *notch = lv_obj_create(col);
        lv_obj_set_size(notch, 36, 4);
        lv_obj_set_style_radius(notch, 2, 0);
        lv_obj_set_style_border_width(notch, 0, 0);
        lv_obj_set_style_bg_color(notch, is_on ? lv_color_hex(0xD97706) : lv_color_hex(0x424752), 0);
        lv_obj_align(notch, LV_ALIGN_TOP_MID, 0, 12);
        lv_obj_clear_flag(notch, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(notch, LV_OBJ_FLAG_CLICKABLE);

        /* Icon Power truc tiep tren cot nut (khong dung circle tach biet, hoa quyen 100% vao nen nut, mat soc doc) */
        lv_obj_t *lbl_icon = lv_label_create(col);
        lv_obj_set_style_text_font(lbl_icon, &font_20, 0);
        lv_label_set_text(lbl_icon, LV_SYMBOL_POWER);
        lv_obj_set_style_text_color(lbl_icon, is_on ? lv_color_hex(0xD97706) : lv_color_hex(0x8592A6), 0);
        lv_obj_align(lbl_icon, LV_ALIGN_CENTER, 0, -10);
        lv_obj_remove_flag(lbl_icon, LV_OBJ_FLAG_CLICKABLE);

        /* Khoi ten nut va trang thai o day cot: dua len cao de khong bi cat chu */
        lv_obj_t *b_txt_cont = lv_obj_create(col);
        lv_obj_set_size(b_txt_cont, w - 8, 56);
        lv_obj_set_style_bg_opa(b_txt_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(b_txt_cont, 0, 0);
        lv_obj_set_style_pad_all(b_txt_cont, 0, 0);
        lv_obj_set_flex_flow(b_txt_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b_txt_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(b_txt_cont, 2, 0);
        lv_obj_align(b_txt_cont, LV_ALIGN_BOTTOM_MID, 0, -22);
        lv_obj_clear_flag(b_txt_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(b_txt_cont, LV_OBJ_FLAG_CLICKABLE);

        /* Ten nut: dung font_20 */
        lv_obj_t *lbl_name = lv_label_create(b_txt_cont);
        lv_obj_set_style_text_font(lbl_name, &font_20, 0);
        lv_obj_set_style_pad_all(lbl_name, 0, 0);
        lv_label_set_text(lbl_name, endpoint->name[0] ? endpoint->name : "Nút");
        lv_obj_set_style_text_color(lbl_name, is_on ? lv_color_hex(0x111827) : lv_color_hex(0xF8FAFC), 0);
        lv_obj_set_width(lbl_name, w - 12);
        lv_obj_set_style_text_align(lbl_name, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_DOT);
        lv_obj_remove_flag(lbl_name, LV_OBJ_FLAG_CLICKABLE);

        /* Trang thai Bat / Tat: dung font_16, nho hon ten 1 size, khong bi cat chu */
        lv_obj_t *lbl_st = lv_label_create(b_txt_cont);
        lv_obj_set_style_text_font(lbl_st, &font_16, 0);
        lv_obj_set_style_pad_all(lbl_st, 0, 0);
        lv_label_set_text(lbl_st, is_on ? "Bật" : "Tắt");
        lv_obj_set_style_text_color(lbl_st, is_on ? lv_color_hex(0x6B7280) : lv_color_hex(0x94A3B8), 0);
        lv_obj_remove_flag(lbl_st, LV_OBJ_FLAG_CLICKABLE);

        /* Dang ky event toggle cho tung cot */
        dev_ep_cmd_t *cmd = (dev_ep_cmd_t *)malloc(sizeof(dev_ep_cmd_t));
        if (cmd) {
            snprintf(cmd->device_id, sizeof(cmd->device_id), "%s", dev->id);
            snprintf(cmd->endpoint_id, sizeof(cmd->endpoint_id), "%s", endpoint->id);
            cmd->target_state = !is_on;
            lv_obj_add_event_cb(col, on_ep_toggle_clicked, LV_EVENT_CLICKED, cmd);
            lv_obj_add_event_cb(col, on_obj_delete_free_ud, LV_EVENT_DELETE, cmd);
        }
    }
}

static void open_multi_button_detail(const app_device_t *dev)
{
    if (!s_detail_overlay || !dev) return;

    snprintf(s_detail_device_id, sizeof(s_detail_device_id), "%s", dev->id);
    lv_label_set_text(s_detail_title, dev->name);
    render_multi_button_columns(dev);
    lv_obj_remove_flag(s_detail_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void on_device_card_clicked(lv_event_t *e)
{
    const char *dev_id = (const char *)lv_event_get_user_data(e);
    if (!dev_id) return;

    const app_snapshot_t *snap = app_gateway_get_snapshot();
    if (!snap) return;

    for (uint16_t i = 0; i < snap->device_count; i++) {
        if (strcmp(snap->devices[i].id, dev_id) == 0) {
            const app_device_t *dev = &snap->devices[i];
            if (dev->endpoint_count > 1) {
                open_multi_button_detail(dev);
            } else if (dev->endpoint_count == 1) {
                /* Single switch toggle truc tiep */
                bool is_on = false;
                if (dev->endpoints[0].trait_count > 0 && dev->endpoints[0].traits[0].state_valid) {
                    is_on = dev->endpoints[0].traits[0].state.b_val;
                }
                const char *payload = !is_on ? "true" : "false";
                app_gateway_send_command(dev->id, dev->endpoints[0].id, "on_off", payload);
                lv_async_call(async_refresh_cards_cb, NULL);
            }
            break;
        }
    }
}

/* ========================================================================= */
/* Room Bar Selection                                                        */
/* ========================================================================= */
static void on_room_btn_clicked(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_selected_room_idx = idx;
    app_ui_cards_update();
}

/* ========================================================================= */
/* Settings Actions                                                          */
/* ========================================================================= */
static void on_unlink_clicked(lv_event_t *e)
{
    (void)e;
    ESP_LOGW(TAG, "Gỡ liên kết Home Center theo yêu cầu người dùng");
    app_gateway_unlink();
    app_ui_switch_screen(APP_UI_SCREEN_PAIRING);
}

static void on_change_wifi_clicked(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Chuyển sang cấu hình đổi mạng Wi-Fi");
    app_ui_switch_screen(APP_UI_SCREEN_ONBOARDING);
}

static void on_restart_clicked(lv_event_t *e)
{
    (void)e;
    ESP_LOGW(TAG, "Khoi dong lai he thong");
    esp_restart();
}

/* ========================================================================= */
/* KHOI TAO DASHBOARD TILEVIEW (480x480)                                      */
/* ========================================================================= */
lv_obj_t *app_ui_cards_create(lv_obj_t *parent)
{
    /* 1. Root Container */
    s_cards_root = lv_obj_create(parent);
    lv_obj_set_size(s_cards_root, 480, 480);
    lv_obj_set_style_bg_color(s_cards_root, lv_color_hex(0x0A0D14), 0);
    lv_obj_set_style_border_width(s_cards_root, 0, 0);
    lv_obj_set_style_pad_all(s_cards_root, 0, 0);
    lv_obj_clear_flag(s_cards_root, LV_OBJ_FLAG_SCROLLABLE);

    /* 2. Container 4 man hinh ngang snap-scroll */
    s_tileview = lv_obj_create(s_cards_root);
    lv_obj_set_size(s_tileview, 480, 480);
    lv_obj_set_style_bg_color(s_tileview, lv_color_hex(0x0A0D14), 0);
    lv_obj_set_style_border_width(s_tileview, 0, 0);
    lv_obj_set_style_pad_all(s_tileview, 0, 0);
    lv_obj_set_flex_flow(s_tileview, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_snap_x(s_tileview, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(s_tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tileview, on_tileview_scroll_end, LV_EVENT_SCROLL_END, NULL);

    s_tile_home     = lv_obj_create(s_tileview);
    s_tile_device   = lv_obj_create(s_tileview);
    s_tile_scene    = lv_obj_create(s_tileview);
    s_tile_settings = lv_obj_create(s_tileview);

    lv_obj_t *tiles[] = {s_tile_home, s_tile_device, s_tile_scene, s_tile_settings};
    for (int t = 0; t < 4; t++) {
        lv_obj_set_size(tiles[t], 480, 480);
        lv_obj_set_style_bg_color(tiles[t], lv_color_hex(0x0A0D14), 0);
        lv_obj_set_style_border_width(tiles[t], 0, 0);
        lv_obj_set_style_pad_all(tiles[t], 0, 0);
        lv_obj_clear_flag(tiles[t], LV_OBJ_FLAG_SCROLLABLE);
    }

    /* --------------------------------------------------------------------- */
    /* 3. Dung UI cho TILE 0: HOME                                           */
    /* --------------------------------------------------------------------- */
    /* 3.1 Header (y = 4, h = 30px) */
    lv_obj_t *home_hdr = lv_obj_create(s_tile_home);
    lv_obj_set_size(home_hdr, 480, 32);
    lv_obj_set_style_bg_opa(home_hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(home_hdr, 0, 0);
    lv_obj_set_style_pad_hor(home_hdr, 14, 0);
    lv_obj_align(home_hdr, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_clear_flag(home_hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dot_online = lv_obj_create(home_hdr);
    lv_obj_set_size(dot_online, 8, 8);
    lv_obj_set_style_radius(dot_online, 4, 0);
    lv_obj_set_style_bg_color(dot_online, lv_color_hex(0x10B981), 0);
    lv_obj_set_style_border_width(dot_online, 0, 0);
    lv_obj_align(dot_online, LV_ALIGN_LEFT_MID, 0, 0);

    s_home_lbl_name = lv_label_create(home_hdr);
    lv_label_set_text(s_home_lbl_name, "Nhà thông minh");
    lv_obj_set_style_text_color(s_home_lbl_name, lv_color_hex(0xE2E8F0), 0);
    lv_obj_align(s_home_lbl_name, LV_ALIGN_LEFT_MID, 16, 0);

    s_home_lbl_status = lv_label_create(home_hdr);
    lv_label_set_text(s_home_lbl_status, "27.5°  " LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_home_lbl_status, lv_color_hex(0x94A3B8), 0);
    lv_obj_align(s_home_lbl_status, LV_ALIGN_RIGHT_MID, 0, 0);

    /* 3.2 Dong ho & Thoi tiet (y = 38, h = 84px) */
    lv_obj_t *clock_cont = lv_obj_create(s_tile_home);
    lv_obj_set_size(clock_cont, 480, 84);
    lv_obj_set_style_bg_opa(clock_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_cont, 0, 0);
    lv_obj_set_style_pad_hor(clock_cont, 14, 0);
    lv_obj_align(clock_cont, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_clear_flag(clock_cont, LV_OBJ_FLAG_SCROLLABLE);

    s_home_lbl_clock = lv_label_create(clock_cont);
    lv_obj_set_style_text_font(s_home_lbl_clock, &font_36, 0);
    lv_label_set_text(s_home_lbl_clock, "18:31");
    lv_obj_set_style_text_color(s_home_lbl_clock, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_home_lbl_clock, LV_ALIGN_LEFT_MID, 0, -4);

    s_home_lbl_date = lv_label_create(clock_cont);
    lv_label_set_text(s_home_lbl_date, "Thứ Năm\n12 tháng 11");
    lv_obj_set_style_text_color(s_home_lbl_date, lv_color_hex(0x94A3B8), 0);
    lv_obj_align(s_home_lbl_date, LV_ALIGN_LEFT_MID, 116, 0);

    lv_obj_t *lbl_weather_temp = lv_label_create(clock_cont);
    lv_obj_set_style_text_font(lbl_weather_temp, &font_36, 0);
    lv_label_set_text(lbl_weather_temp, "32°");
    lv_obj_set_style_text_color(lbl_weather_temp, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_weather_temp, LV_ALIGN_RIGHT_MID, 0, -8);

    lv_obj_t *lbl_weather_desc = lv_label_create(clock_cont);
    lv_label_set_text(lbl_weather_desc, "Nhiều mây");
    lv_obj_set_style_text_color(lbl_weather_desc, lv_color_hex(0x94A3B8), 0);
    lv_obj_align(lbl_weather_desc, LV_ALIGN_RIGHT_MID, 0, 18);

    /* 3.3 Khối Thẻ Điều Khiển Lớn (Điều hòa & Đèn) (y = 124, h = 132px) */
    /* Thẻ Điều hòa (Bên trái) */
    lv_obj_t *card_ac = lv_obj_create(s_tile_home);
    lv_obj_set_size(card_ac, 222, 130);
    lv_obj_set_style_radius(card_ac, 18, 0);
    lv_obj_set_style_bg_color(card_ac, lv_color_hex(0x2563EB), 0);
    lv_obj_set_style_border_width(card_ac, 0, 0);
    lv_obj_set_style_pad_all(card_ac, 12, 0);
    lv_obj_set_style_shadow_width(card_ac, 16, 0);
    lv_obj_set_style_shadow_offset_y(card_ac, 6, 0);
    lv_obj_set_style_shadow_color(card_ac, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card_ac, LV_OPA_30, 0);
    lv_obj_align(card_ac, LV_ALIGN_TOP_LEFT, 12, 124);
    lv_obj_clear_flag(card_ac, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_ac_title = lv_label_create(card_ac);
    lv_label_set_text(lbl_ac_title, "Điều hoà");
    lv_obj_set_style_text_color(lbl_ac_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_ac_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *ic_ac = lv_label_create(card_ac);
    lv_label_set_text(ic_ac, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(ic_ac, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(ic_ac, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *lbl_ac_sub = lv_label_create(card_ac);
    lv_label_set_text(lbl_ac_sub, "Phòng khách · Mát");
    lv_obj_set_style_text_color(lbl_ac_sub, lv_color_hex(0xBFDBFE), 0);
    lv_obj_align(lbl_ac_sub, LV_ALIGN_TOP_LEFT, 0, 24);

    s_home_lbl_ac_temp = lv_label_create(card_ac);
    lv_obj_set_style_text_font(s_home_lbl_ac_temp, &font_36, 0);
    lv_label_set_text(s_home_lbl_ac_temp, "25°");
    lv_obj_set_style_text_color(s_home_lbl_ac_temp, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_home_lbl_ac_temp, LV_ALIGN_BOTTOM_LEFT, 2, -4);

    lv_obj_t *btn_ac_minus = lv_button_create(card_ac);
    lv_obj_set_size(btn_ac_minus, 38, 38);
    lv_obj_set_style_radius(btn_ac_minus, 19, 0);
    lv_obj_set_style_bg_color(btn_ac_minus, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(btn_ac_minus, LV_OPA_20, 0);
    lv_obj_set_style_border_width(btn_ac_minus, 0, 0);
    lv_obj_align(btn_ac_minus, LV_ALIGN_BOTTOM_RIGHT, -46, -4);
    lv_obj_add_event_cb(btn_ac_minus, on_ac_temp_btn_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    lv_obj_t *lbl_m = lv_label_create(btn_ac_minus);
    lv_label_set_text(lbl_m, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(lbl_m, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_m);

    lv_obj_t *btn_ac_plus = lv_button_create(card_ac);
    lv_obj_set_size(btn_ac_plus, 38, 38);
    lv_obj_set_style_radius(btn_ac_plus, 19, 0);
    lv_obj_set_style_bg_color(btn_ac_plus, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(btn_ac_plus, LV_OPA_20, 0);
    lv_obj_set_style_border_width(btn_ac_plus, 0, 0);
    lv_obj_align(btn_ac_plus, LV_ALIGN_BOTTOM_RIGHT, 0, -4);
    lv_obj_add_event_cb(btn_ac_plus, on_ac_temp_btn_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_t *lbl_p = lv_label_create(btn_ac_plus);
    lv_label_set_text(lbl_p, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(lbl_p, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_p);

    /* Thẻ Đèn Tổng quan (Bên phải - Phong cách vàng hổ phách ấm đúng home.png) */
    lv_obj_t *card_light = lv_obj_create(s_tile_home);
    lv_obj_set_size(card_light, 222, 130);
    lv_obj_set_style_radius(card_light, 18, 0);
    lv_obj_set_style_bg_color(card_light, lv_color_hex(0xD97706), 0);
    lv_obj_set_style_border_width(card_light, 0, 0);
    lv_obj_set_style_pad_all(card_light, 12, 0);
    lv_obj_set_style_shadow_width(card_light, 16, 0);
    lv_obj_set_style_shadow_offset_y(card_light, 6, 0);
    lv_obj_set_style_shadow_color(card_light, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card_light, LV_OPA_30, 0);
    lv_obj_align(card_light, LV_ALIGN_TOP_RIGHT, -12, 124);
    lv_obj_clear_flag(card_light, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_lt_title = lv_label_create(card_light);
    lv_label_set_text(lbl_lt_title, "Đèn");
    lv_obj_set_style_text_color(lbl_lt_title, lv_color_hex(0x1A1208), 0);
    lv_obj_align(lbl_lt_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *ic_lt = lv_label_create(card_light);
    lv_label_set_text(ic_lt, LV_SYMBOL_POWER);
    lv_obj_set_style_text_color(ic_lt, lv_color_hex(0x1A1208), 0);
    lv_obj_align(ic_lt, LV_ALIGN_TOP_RIGHT, 0, 0);

    s_home_lbl_light_sub = lv_label_create(card_light);
    lv_label_set_text(s_home_lbl_light_sub, "0 / 0 đang bật");
    lv_obj_set_style_text_color(s_home_lbl_light_sub, lv_color_hex(0x4A2A06), 0);
    lv_obj_align(s_home_lbl_light_sub, LV_ALIGN_TOP_LEFT, 0, 24);

    s_home_lbl_light_count = lv_label_create(card_light);
    lv_obj_set_style_text_font(s_home_lbl_light_count, &font_36, 0);
    lv_label_set_text(s_home_lbl_light_count, "0");
    lv_obj_set_style_text_color(s_home_lbl_light_count, lv_color_hex(0x1A1208), 0);
    lv_obj_align(s_home_lbl_light_count, LV_ALIGN_BOTTOM_LEFT, 2, -4);

    lv_obj_t *btn_all_off = lv_button_create(card_light);
    lv_obj_set_size(btn_all_off, 94, 38);
    lv_obj_set_style_radius(btn_all_off, 19, 0);
    lv_obj_set_style_bg_color(btn_all_off, lv_color_hex(0xFDE68A), 0);
    lv_obj_set_style_border_width(btn_all_off, 0, 0);
    lv_obj_align(btn_all_off, LV_ALIGN_BOTTOM_RIGHT, 0, -4);
    lv_obj_add_event_cb(btn_all_off, on_all_lights_off_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_off_txt = lv_label_create(btn_all_off);
    lv_label_set_text(lbl_off_txt, "Tắt hết");
    lv_obj_set_style_text_color(lbl_off_txt, lv_color_hex(0x1A1208), 0);
    lv_obj_center(lbl_off_txt);

    /* 3.4 Khối Thiết bị Nhanh (y = 262, h = 88px) */
    s_home_quick_devs_cont = lv_obj_create(s_tile_home);
    lv_obj_set_size(s_home_quick_devs_cont, 480, 88);
    lv_obj_set_style_bg_opa(s_home_quick_devs_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_home_quick_devs_cont, 0, 0);
    lv_obj_set_style_pad_hor(s_home_quick_devs_cont, 12, 0);
    lv_obj_set_style_pad_ver(s_home_quick_devs_cont, 0, 0);
    lv_obj_set_flex_flow(s_home_quick_devs_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_home_quick_devs_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(s_home_quick_devs_cont, LV_ALIGN_TOP_MID, 0, 262);
    lv_obj_clear_flag(s_home_quick_devs_cont, LV_OBJ_FLAG_SCROLLABLE);

    /* 3.5 Khối Cảnh Nhanh (y = 358, h = 92px) */
    s_home_quick_scenes_cont = lv_obj_create(s_tile_home);
    lv_obj_set_size(s_home_quick_scenes_cont, 480, 92);
    lv_obj_set_style_bg_opa(s_home_quick_scenes_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_home_quick_scenes_cont, 0, 0);
    lv_obj_set_style_pad_hor(s_home_quick_scenes_cont, 12, 0);
    lv_obj_set_style_pad_ver(s_home_quick_scenes_cont, 0, 0);
    lv_obj_set_flex_flow(s_home_quick_scenes_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_home_quick_scenes_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(s_home_quick_scenes_cont, LV_ALIGN_TOP_MID, 0, 358);
    lv_obj_clear_flag(s_home_quick_scenes_cont, LV_OBJ_FLAG_SCROLLABLE);

    /* --------------------------------------------------------------------- */
    /* 4. Dung UI cho TILE 1: DEVICES (Thiết bị)                             */
    /* --------------------------------------------------------------------- */
    /* Header (y = 6, h = 38px) */
    lv_obj_t *dev_hdr = lv_obj_create(s_tile_device);
    lv_obj_set_size(dev_hdr, 480, 38);
    lv_obj_set_style_bg_opa(dev_hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dev_hdr, 0, 0);
    lv_obj_set_style_pad_hor(dev_hdr, 14, 0);
    lv_obj_align(dev_hdr, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_clear_flag(dev_hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_dev_title = lv_label_create(dev_hdr);
    lv_label_set_text(lbl_dev_title, "Thiết bị");
    lv_obj_set_style_text_color(lbl_dev_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_dev_title, LV_ALIGN_LEFT_MID, 0, 0);

    s_dev_lbl_subtitle = lv_label_create(dev_hdr);
    lv_label_set_text(s_dev_lbl_subtitle, "0 thiết bị · 0 đang bật");
    lv_obj_set_style_text_color(s_dev_lbl_subtitle, lv_color_hex(0x9CA3AF), 0);
    lv_obj_align(s_dev_lbl_subtitle, LV_ALIGN_RIGHT_MID, 0, 0);

    /* Room Bar (y = 44, h = 38px) */
    s_room_bar = lv_obj_create(s_tile_device);
    lv_obj_set_size(s_room_bar, 480, 38);
    lv_obj_set_style_bg_opa(s_room_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_room_bar, 0, 0);
    lv_obj_set_style_pad_hor(s_room_bar, 10, 0);
    lv_obj_set_style_pad_ver(s_room_bar, 2, 0);
    lv_obj_set_flex_flow(s_room_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_room_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(s_room_bar, LV_ALIGN_TOP_MID, 0, 44);

    /* Device Grid Container (y = 84, h = 382px) */
    s_cards_container = lv_obj_create(s_tile_device);
    lv_obj_set_size(s_cards_container, 480, 382);
    lv_obj_set_style_bg_opa(s_cards_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_cards_container, 0, 0);
    lv_obj_set_style_pad_all(s_cards_container, 10, 0);
    lv_obj_set_style_pad_row(s_cards_container, 10, 0);
    lv_obj_set_style_pad_column(s_cards_container, 10, 0);
    lv_obj_set_flex_flow(s_cards_container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_cards_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_align(s_cards_container, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_scrollbar_mode(s_cards_container, LV_SCROLLBAR_MODE_OFF);

    /* --------------------------------------------------------------------- */
    /* 5. Dung UI cho TILE 2: SCENES (Cảnh)                                  */
    /* --------------------------------------------------------------------- */
    lv_obj_t *sc_hdr = lv_obj_create(s_tile_scene);
    lv_obj_set_size(sc_hdr, 480, 42);
    lv_obj_set_style_bg_opa(sc_hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sc_hdr, 0, 0);
    lv_obj_set_style_pad_hor(sc_hdr, 14, 0);
    lv_obj_align(sc_hdr, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_clear_flag(sc_hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_sc_title = lv_label_create(sc_hdr);
    lv_label_set_text(lbl_sc_title, "Cảnh");
    lv_obj_set_style_text_color(lbl_sc_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_sc_title, LV_ALIGN_LEFT_MID, 0, 0);

    s_scene_lbl_subtitle = lv_label_create(sc_hdr);
    lv_label_set_text(s_scene_lbl_subtitle, "0 cảnh");
    lv_obj_set_style_text_color(s_scene_lbl_subtitle, lv_color_hex(0x9CA3AF), 0);
    lv_obj_align(s_scene_lbl_subtitle, LV_ALIGN_RIGHT_MID, 0, 0);

    s_scenes_grid = lv_obj_create(s_tile_scene);
    lv_obj_set_size(s_scenes_grid, 480, 410);
    lv_obj_set_style_bg_opa(s_scenes_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_scenes_grid, 0, 0);
    lv_obj_set_style_pad_all(s_scenes_grid, 10, 0);
    lv_obj_set_flex_flow(s_scenes_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_scenes_grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_align(s_scenes_grid, LV_ALIGN_TOP_MID, 0, 50);

    /* --------------------------------------------------------------------- */
    /* 6. Dung UI cho TILE 3: SETTINGS (Cài đặt)                             */
    /* --------------------------------------------------------------------- */
    lv_obj_t *set_hdr = lv_obj_create(s_tile_settings);
    lv_obj_set_size(set_hdr, 480, 42);
    lv_obj_set_style_bg_opa(set_hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(set_hdr, 0, 0);
    lv_obj_set_style_pad_hor(set_hdr, 14, 0);
    lv_obj_align(set_hdr, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_clear_flag(set_hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_set_title = lv_label_create(set_hdr);
    lv_label_set_text(lbl_set_title, "Cài đặt");
    lv_obj_set_style_text_color(lbl_set_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_set_title, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *settings_scroll = lv_obj_create(s_tile_settings);
    lv_obj_set_size(settings_scroll, 480, 410);
    lv_obj_set_style_bg_opa(settings_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(settings_scroll, 0, 0);
    lv_obj_set_style_pad_all(settings_scroll, 14, 0);
    lv_obj_align(settings_scroll, LV_ALIGN_TOP_MID, 0, 50);

    /* Card Wi-Fi */
    lv_obj_t *box_wifi = lv_obj_create(settings_scroll);
    lv_obj_set_size(box_wifi, 452, 74);
    lv_obj_set_style_radius(box_wifi, 14, 0);
    lv_obj_set_style_bg_color(box_wifi, lv_color_hex(0x181D26), 0);
    lv_obj_set_style_border_color(box_wifi, lv_color_hex(0x2E384D), 0);
    lv_obj_set_style_border_width(box_wifi, 1, 0);
    lv_obj_align(box_wifi, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(box_wifi, LV_OBJ_FLAG_SCROLLABLE);

    s_settings_lbl_wifi = lv_label_create(box_wifi);
    lv_label_set_text(s_settings_lbl_wifi, "Wi-Fi: Đang kết nối");
    lv_obj_set_style_text_color(s_settings_lbl_wifi, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_settings_lbl_wifi, LV_ALIGN_TOP_LEFT, 0, 0);

    s_settings_lbl_ip = lv_label_create(box_wifi);
    lv_label_set_text(s_settings_lbl_ip, "IP: 192.168.x.x");
    lv_obj_set_style_text_color(s_settings_lbl_ip, lv_color_hex(0x94A3B8), 0);
    lv_obj_align(s_settings_lbl_ip, LV_ALIGN_TOP_LEFT, 0, 24);

    /* Card Cloud */
    lv_obj_t *box_cloud = lv_obj_create(settings_scroll);
    lv_obj_set_size(box_cloud, 452, 74);
    lv_obj_set_style_radius(box_cloud, 14, 0);
    lv_obj_set_style_bg_color(box_cloud, lv_color_hex(0x181D26), 0);
    lv_obj_set_style_border_color(box_cloud, lv_color_hex(0x2E384D), 0);
    lv_obj_set_style_border_width(box_cloud, 1, 0);
    lv_obj_align(box_cloud, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_clear_flag(box_cloud, LV_OBJ_FLAG_SCROLLABLE);

    s_settings_lbl_cloud = lv_label_create(box_cloud);
    lv_label_set_text(s_settings_lbl_cloud, "NobleX Cloud: Trực tuyến");
    lv_obj_set_style_text_color(s_settings_lbl_cloud, lv_color_hex(0x10B981), 0);
    lv_obj_align(s_settings_lbl_cloud, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_hw = lv_label_create(box_cloud);
    lv_label_set_text(lbl_hw, "Phần cứng: ESP32-P4 + C6 | FW v1.0.0");
    lv_obj_set_style_text_color(lbl_hw, lv_color_hex(0x94A3B8), 0);
    lv_obj_align(lbl_hw, LV_ALIGN_TOP_LEFT, 0, 24);

    /* Nut Doi Wi-Fi */
    lv_obj_t *btn_chg_wifi = lv_button_create(settings_scroll);
    lv_obj_set_size(btn_chg_wifi, 452, 48);
    lv_obj_set_style_radius(btn_chg_wifi, 12, 0);
    lv_obj_set_style_bg_color(btn_chg_wifi, lv_color_hex(0x2563EB), 0);
    lv_obj_align(btn_chg_wifi, LV_ALIGN_TOP_MID, 0, 168);
    lv_obj_add_event_cb(btn_chg_wifi, on_change_wifi_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *l_cw = lv_label_create(btn_chg_wifi);
    lv_label_set_text(l_cw, "Đổi mạng Wi-Fi");
    lv_obj_set_style_text_color(l_cw, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(l_cw);

    /* Nut Go lien ket */
    lv_obj_t *btn_unlink = lv_button_create(settings_scroll);
    lv_obj_set_size(btn_unlink, 452, 48);
    lv_obj_set_style_radius(btn_unlink, 12, 0);
    lv_obj_set_style_bg_color(btn_unlink, lv_color_hex(0xDC2626), 0);
    lv_obj_align(btn_unlink, LV_ALIGN_TOP_MID, 0, 224);
    lv_obj_add_event_cb(btn_unlink, on_unlink_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *l_ul = lv_label_create(btn_unlink);
    lv_label_set_text(l_ul, "Gỡ liên kết (Unlink)");
    lv_obj_set_style_text_color(l_ul, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(l_ul);

    /* Nut Reset */
    lv_obj_t *btn_rst = lv_button_create(settings_scroll);
    lv_obj_set_size(btn_rst, 452, 48);
    lv_obj_set_style_radius(btn_rst, 12, 0);
    lv_obj_set_style_bg_color(btn_rst, lv_color_hex(0x374151), 0);
    lv_obj_align(btn_rst, LV_ALIGN_TOP_MID, 0, 280);
    lv_obj_add_event_cb(btn_rst, on_restart_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *l_rst = lv_label_create(btn_rst);
    lv_label_set_text(l_rst, "Khởi động lại Panel");
    lv_obj_set_style_text_color(l_rst, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(l_rst);

    /* --------------------------------------------------------------------- */
    /* 7. Page Indicator Dots (4 dots nho o day y = 472)                      */
    /* --------------------------------------------------------------------- */
    s_dots_cont = lv_obj_create(s_cards_root);
    lv_obj_set_size(s_dots_cont, 80, 8);
    lv_obj_set_style_bg_opa(s_dots_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_dots_cont, 0, 0);
    lv_obj_set_style_pad_all(s_dots_cont, 0, 0);
    lv_obj_set_flex_flow(s_dots_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_dots_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(s_dots_cont, LV_ALIGN_BOTTOM_MID, 0, -3);
    lv_obj_clear_flag(s_dots_cont, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 4; i++) {
        s_dots[i] = lv_obj_create(s_dots_cont);
        lv_obj_set_size(s_dots[i], (i == 0) ? 16 : 5, 4);
        lv_obj_set_style_radius(s_dots[i], 2, 0);
        lv_obj_set_style_border_width(s_dots[i], 0, 0);
        lv_obj_set_style_bg_color(s_dots[i], (i == 0) ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x475569), 0);
        lv_obj_set_style_margin_hor(s_dots[i], 3, 0);
        lv_obj_clear_flag(s_dots[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    /* --------------------------------------------------------------------- */
    /* 8. Toast Notification Banner (O giua day)                              */
    /* --------------------------------------------------------------------- */
    s_toast_banner = lv_obj_create(s_cards_root);
    lv_obj_set_size(s_toast_banner, 380, 52);
    lv_obj_set_style_radius(s_toast_banner, 26, 0);
    lv_obj_set_style_bg_color(s_toast_banner, lv_color_hex(0xF3F4F6), 0);
    lv_obj_set_style_border_width(s_toast_banner, 0, 0);
    lv_obj_set_style_pad_hor(s_toast_banner, 16, 0);
    lv_obj_align(s_toast_banner, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_obj_add_flag(s_toast_banner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_toast_banner, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_toast = lv_label_create(s_toast_banner);
    lv_label_set_text(s_lbl_toast, "✓ Thông báo");
    lv_obj_set_style_text_color(s_lbl_toast, lv_color_hex(0x111827), 0);
    lv_obj_center(s_lbl_toast);

    /* --------------------------------------------------------------------- */
    /* 9. Overlay: Multi-button Detail Screen (many_button.png)               */
    /* --------------------------------------------------------------------- */
    s_detail_overlay = lv_obj_create(s_cards_root);
    lv_obj_set_size(s_detail_overlay, 480, 480);
    lv_obj_set_style_bg_color(s_detail_overlay, lv_color_hex(0x13161D), 0);
    lv_obj_set_style_border_width(s_detail_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_detail_overlay, 0, 0);
    lv_obj_align(s_detail_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_detail_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_detail_overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* Header Overlay */
    lv_obj_t *ov_hdr = lv_obj_create(s_detail_overlay);
    lv_obj_set_size(ov_hdr, 480, 52);
    lv_obj_set_style_bg_opa(ov_hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ov_hdr, 0, 0);
    lv_obj_set_style_pad_hor(ov_hdr, 12, 0);
    lv_obj_align(ov_hdr, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_clear_flag(ov_hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_back = lv_button_create(ov_hdr);
    lv_obj_set_size(btn_back, 44, 44);
    lv_obj_set_style_radius(btn_back, 22, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x1E232E), 0);
    lv_obj_set_style_shadow_width(btn_back, 10, 0);
    lv_obj_set_style_shadow_offset_y(btn_back, 3, 0);
    lv_obj_set_style_shadow_color(btn_back, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(btn_back, LV_OPA_40, 0);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(btn_back, on_close_detail_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_bk = lv_label_create(btn_back);
    lv_label_set_text(lbl_bk, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(lbl_bk, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_bk);

    s_detail_title = lv_label_create(ov_hdr);
    lv_obj_set_style_text_font(s_detail_title, &font_20, 0);
    lv_label_set_text(s_detail_title, "Công tắc nhiều nút");
    lv_obj_set_style_text_color(s_detail_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_detail_title, LV_ALIGN_CENTER, 0, 0);

    /* Container cac cot nut dung */
    s_detail_cols_cont = lv_obj_create(s_detail_overlay);
    lv_obj_set_size(s_detail_cols_cont, 480, 314);
    lv_obj_set_style_bg_opa(s_detail_cols_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_detail_cols_cont, 0, 0);
    lv_obj_set_style_pad_hor(s_detail_cols_cont, 10, 0);
    lv_obj_set_style_pad_ver(s_detail_cols_cont, 6, 0);
    lv_obj_set_style_pad_column(s_detail_cols_cont, 10, 0);
    lv_obj_set_flex_flow(s_detail_cols_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_detail_cols_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(s_detail_cols_cont, LV_ALIGN_TOP_MID, 0, 66);
    lv_obj_clear_flag(s_detail_cols_cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Hang nut dieu khien day: Tat tat ca | Bat tat ca (co do bong noi Elevation Shadow) */
    lv_obj_t *btn_all_off_dt = lv_button_create(s_detail_overlay);
    lv_obj_set_size(btn_all_off_dt, 216, 54);
    lv_obj_set_style_radius(btn_all_off_dt, 27, 0);
    lv_obj_set_style_bg_color(btn_all_off_dt, lv_color_hex(0x181C24), 0);
    lv_obj_set_style_border_color(btn_all_off_dt, lv_color_hex(0x2E384D), 0);
    lv_obj_set_style_border_width(btn_all_off_dt, 1, 0);
    lv_obj_set_style_shadow_width(btn_all_off_dt, 18, 0);
    lv_obj_set_style_shadow_offset_y(btn_all_off_dt, 6, 0);
    lv_obj_set_style_shadow_offset_x(btn_all_off_dt, 0, 0);
    lv_obj_set_style_shadow_spread(btn_all_off_dt, 1, 0);
    lv_obj_set_style_shadow_color(btn_all_off_dt, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(btn_all_off_dt, LV_OPA_60, 0);
    lv_obj_align(btn_all_off_dt, LV_ALIGN_BOTTOM_LEFT, 16, -18);
    lv_obj_add_event_cb(btn_all_off_dt, on_detail_batch_all_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)0);

    lv_obj_t *l_d_off = lv_label_create(btn_all_off_dt);
    lv_label_set_text(l_d_off, "Tắt tất cả");
    lv_obj_set_style_text_color(l_d_off, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(l_d_off);

    lv_obj_t *btn_all_on_dt = lv_button_create(s_detail_overlay);
    lv_obj_set_size(btn_all_on_dt, 216, 54);
    lv_obj_set_style_radius(btn_all_on_dt, 27, 0);
    lv_obj_set_style_bg_color(btn_all_on_dt, lv_color_hex(0x181C24), 0);
    lv_obj_set_style_border_color(btn_all_on_dt, lv_color_hex(0x2E384D), 0);
    lv_obj_set_style_border_width(btn_all_on_dt, 1, 0);
    lv_obj_set_style_shadow_width(btn_all_on_dt, 18, 0);
    lv_obj_set_style_shadow_offset_y(btn_all_on_dt, 6, 0);
    lv_obj_set_style_shadow_offset_x(btn_all_on_dt, 0, 0);
    lv_obj_set_style_shadow_spread(btn_all_on_dt, 1, 0);
    lv_obj_set_style_shadow_color(btn_all_on_dt, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(btn_all_on_dt, LV_OPA_60, 0);
    lv_obj_align(btn_all_on_dt, LV_ALIGN_BOTTOM_RIGHT, -16, -18);
    lv_obj_add_event_cb(btn_all_on_dt, on_detail_batch_all_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    lv_obj_t *l_d_on = lv_label_create(btn_all_on_dt);
    lv_label_set_text(l_d_on, "Bật tất cả");
    lv_obj_set_style_text_color(l_d_on, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(l_d_on);

    return s_cards_root;
}

/* ========================================================================= */
/* CAP NHAT DU LIEU DASHBOARD (app_ui_cards_update)                          */
/* ========================================================================= */
void app_ui_cards_update(void)
{
    if (!s_cards_root) return;

    const app_snapshot_t *snap = app_gateway_get_snapshot();
    update_home_clock();

    /* 1. Cap nhat Header Home */
    if (snap && snap->home_name[0]) {
        lv_label_set_text(s_home_lbl_name, snap->home_name);
    } else {
        lv_label_set_text(s_home_lbl_name, "Nhà thông minh");
    }

    /* 2. Tinh toan thong ke den */
    int total_lights = 0;
    int on_lights = 0;
    int total_devices = snap ? snap->device_count : 0;
    int on_devices = 0;

    if (snap) {
        for (uint16_t i = 0; i < snap->device_count; i++) {
            const app_device_t *dev = &snap->devices[i];
            bool dev_is_on = false;
            for (uint8_t ep = 0; ep < dev->endpoint_count; ep++) {
                const app_endpoint_t *endpoint = &dev->endpoints[ep];
                for (uint8_t tr = 0; tr < endpoint->trait_count; tr++) {
                    if (endpoint->traits[tr].type == TRAIT_TYPE_ON_OFF) {
                        if (strcmp(dev->category, "light") == 0) total_lights++;
                        if (endpoint->traits[tr].state_valid && endpoint->traits[tr].state.b_val) {
                            if (strcmp(dev->category, "light") == 0) on_lights++;
                            dev_is_on = true;
                        }
                    }
                }
            }
            if (dev_is_on) on_devices++;
        }
    }

    if (s_home_lbl_light_sub) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d / %d đang bật", on_lights, total_lights);
        lv_label_set_text(s_home_lbl_light_sub, buf);
    }
    if (s_home_lbl_light_count) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", on_lights);
        lv_label_set_text(s_home_lbl_light_count, buf);
    }
    if (s_dev_lbl_subtitle) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d thiết bị · %d đang bật", total_devices, on_devices);
        lv_label_set_text(s_dev_lbl_subtitle, buf);
    }

    /* 3. Cap nhat Thiet bi Nhanh tren Home (3 nut nho) */
    lv_obj_clean(s_home_quick_devs_cont);
    int quick_added = 0;
    if (snap) {
        for (uint16_t i = 0; i < snap->device_count && quick_added < 3; i++) {
            const app_device_t *dev = &snap->devices[i];
            if (dev->endpoint_count == 0) continue;

            const app_endpoint_t *ep = &dev->endpoints[0];
            bool is_on = false;
            for (uint8_t tr = 0; tr < ep->trait_count; tr++) {
                if (ep->traits[tr].type == TRAIT_TYPE_ON_OFF && ep->traits[tr].state_valid) {
                    is_on = ep->traits[tr].state.b_val;
                    break;
                }
            }

            lv_obj_t *q_card = lv_button_create(s_home_quick_devs_cont);
            lv_obj_set_size(q_card, 144, 82);
            lv_obj_set_style_radius(q_card, 14, 0);
            lv_obj_set_style_bg_color(q_card, is_on ? lv_color_hex(0xF5EFEB) : lv_color_hex(0x1B212D), 0);
            lv_obj_set_style_border_color(q_card, lv_color_hex(0x2D3748), 0);
            lv_obj_set_style_border_width(q_card, is_on ? 0 : 1, 0);
            lv_obj_set_style_shadow_width(q_card, 12, 0);
            lv_obj_set_style_shadow_offset_y(q_card, 4, 0);
            lv_obj_set_style_shadow_color(q_card, lv_color_hex(0x000000), 0);
            lv_obj_set_style_shadow_opa(q_card, LV_OPA_30, 0);
            lv_obj_set_style_pad_all(q_card, 8, 0);
            lv_obj_clear_flag(q_card, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *q_circle = lv_obj_create(q_card);
            lv_obj_set_size(q_circle, 32, 32);
            lv_obj_set_style_radius(q_circle, 16, 0);
            lv_obj_set_style_border_width(q_circle, 0, 0);
            lv_obj_set_style_bg_color(q_circle, is_on ? lv_color_hex(0xF59E0B) : lv_color_hex(0x2D3748), 0);
            lv_obj_align(q_circle, LV_ALIGN_LEFT_MID, 0, 0);

            lv_obj_t *q_icon = lv_label_create(q_circle);
            lv_label_set_text(q_icon, LV_SYMBOL_POWER);
            lv_obj_set_style_text_color(q_icon, is_on ? lv_color_hex(0x78350F) : lv_color_hex(0x94A3B8), 0);
            lv_obj_center(q_icon);

            lv_obj_t *q_txt_cont = lv_obj_create(q_card);
            lv_obj_set_size(q_txt_cont, 86, 48);
            lv_obj_set_style_bg_opa(q_txt_cont, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(q_txt_cont, 0, 0);
            lv_obj_set_style_pad_all(q_txt_cont, 0, 0);
            lv_obj_set_flex_flow(q_txt_cont, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(q_txt_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            lv_obj_set_style_pad_gap(q_txt_cont, 2, 0);
            lv_obj_align(q_txt_cont, LV_ALIGN_LEFT_MID, 40, 0);
            lv_obj_clear_flag(q_txt_cont, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *q_name = lv_label_create(q_txt_cont);
            lv_label_set_text(q_name, dev->name);
            lv_obj_set_style_text_color(q_name, is_on ? lv_color_hex(0x111827) : lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_width(q_name, 86);
            lv_label_set_long_mode(q_name, LV_LABEL_LONG_DOT);

            lv_obj_t *q_st = lv_label_create(q_txt_cont);
            lv_label_set_text(q_st, is_on ? "Bật" : "Tắt");
            lv_obj_set_style_text_color(q_st, is_on ? lv_color_hex(0x4B5563) : lv_color_hex(0x9CA3AF), 0);

            dev_ep_cmd_t *cmd = (dev_ep_cmd_t *)malloc(sizeof(dev_ep_cmd_t));
            if (cmd) {
                snprintf(cmd->device_id, sizeof(cmd->device_id), "%s", dev->id);
                snprintf(cmd->endpoint_id, sizeof(cmd->endpoint_id), "%s", ep->id);
                cmd->target_state = !is_on;
                lv_obj_add_event_cb(q_card, on_ep_toggle_clicked, LV_EVENT_CLICKED, cmd);
                lv_obj_add_event_cb(q_card, on_obj_delete_free_ud, LV_EVENT_DELETE, cmd);
            }
            quick_added++;
        }
    }

    /* 4. Cap nhat Canh Nhanh tren Home (4 nut pill) */
    lv_obj_clean(s_home_quick_scenes_cont);
    const char *default_scene_names[] = {"Về nhà", "Ra ngoài", "Đi ngủ", "Xem phim"};
    const char *default_scene_icons[] = {LV_SYMBOL_HOME, LV_SYMBOL_RIGHT, LV_SYMBOL_BELL, LV_SYMBOL_VIDEO};
    uint32_t default_colors[] = {0xF97316, 0x3B82F6, 0x8B5CF6, 0xEC4899};

    for (int s = 0; s < 4; s++) {
        const char *name = default_scene_names[s];
        const char *id = "sample";
        if (snap && s < snap->scene_count) {
            name = snap->scenes[s].name;
            id = snap->scenes[s].id;
        }

        lv_obj_t *sc_btn = lv_button_create(s_home_quick_scenes_cont);
        lv_obj_set_size(sc_btn, 106, 76);
        lv_obj_set_style_radius(sc_btn, 14, 0);
        lv_obj_set_style_bg_color(sc_btn, lv_color_hex(0x1A1E26), 0);
        lv_obj_set_style_border_color(sc_btn, lv_color_hex(0x2B3240), 0);
        lv_obj_set_style_border_width(sc_btn, 1, 0);
        lv_obj_set_style_shadow_width(sc_btn, 10, 0);
        lv_obj_set_style_shadow_offset_y(sc_btn, 4, 0);
        lv_obj_set_style_shadow_color(sc_btn, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(sc_btn, LV_OPA_30, 0);
        lv_obj_clear_flag(sc_btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_flex_flow(sc_btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(sc_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(sc_btn, 4, 0);
        lv_obj_set_style_pad_gap(sc_btn, 4, 0);

        lv_obj_t *ic = lv_label_create(sc_btn);
        lv_label_set_text(ic, default_scene_icons[s]);
        lv_obj_set_style_text_color(ic, lv_color_hex(default_colors[s]), 0);

        lv_obj_t *txt = lv_label_create(sc_btn);
        lv_label_set_text(txt, name);
        lv_obj_set_style_text_color(txt, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_width(txt, 94);
        lv_obj_set_style_text_align(txt, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(txt, LV_LABEL_LONG_DOT);

        scene_cmd_t *sc_cmd = (scene_cmd_t *)malloc(sizeof(scene_cmd_t));
        if (sc_cmd) {
            snprintf(sc_cmd->scene_id, sizeof(sc_cmd->scene_id), "%s", id);
            snprintf(sc_cmd->scene_name, sizeof(sc_cmd->scene_name), "%s", name);
            lv_obj_add_event_cb(sc_btn, on_scene_clicked, LV_EVENT_CLICKED, sc_cmd);
            lv_obj_add_event_cb(sc_btn, on_obj_delete_free_ud, LV_EVENT_DELETE, sc_cmd);
        }
    }

    /* 5. Cap nhat Room Bar o man hinh Device */
    lv_obj_clean(s_room_bar);
    lv_obj_t *btn_all = lv_button_create(s_room_bar);
    lv_obj_set_size(btn_all, 76, 30);
    lv_obj_set_style_radius(btn_all, 15, 0);
    lv_obj_set_style_bg_color(btn_all, (s_selected_room_idx == -1) ? lv_color_hex(0xECECEC) : lv_color_hex(0x1F242F), 0);
    lv_obj_add_event_cb(btn_all, on_room_btn_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    lv_obj_t *lbl_all = lv_label_create(btn_all);
    lv_label_set_text(lbl_all, "Tất cả");
    lv_obj_set_style_text_color(lbl_all, (s_selected_room_idx == -1) ? lv_color_hex(0x000000) : lv_color_hex(0xD1D5DB), 0);
    lv_obj_center(lbl_all);

    if (snap) {
        for (uint16_t r = 0; r < snap->room_count; r++) {
            lv_obj_t *r_btn = lv_button_create(s_room_bar);
            lv_obj_set_size(r_btn, 88, 30);
            lv_obj_set_style_radius(r_btn, 15, 0);
            lv_obj_set_style_bg_color(r_btn, (s_selected_room_idx == (int)r) ? lv_color_hex(0xECECEC) : lv_color_hex(0x1F242F), 0);
            lv_obj_add_event_cb(r_btn, on_room_btn_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)r);

            lv_obj_t *r_lbl = lv_label_create(r_btn);
            lv_label_set_text(r_lbl, snap->rooms[r].name);
            lv_obj_set_style_text_color(r_lbl, (s_selected_room_idx == (int)r) ? lv_color_hex(0x000000) : lv_color_hex(0xD1D5DB), 0);
            lv_obj_center(r_lbl);
        }
    }

    /* 6. Cap nhat Danh sach Thiet bi o Device Grid */
    lv_obj_clean(s_cards_container);
    const char *sel_room_id = NULL;
    if (snap && s_selected_room_idx >= 0 && s_selected_room_idx < (int)snap->room_count) {
        sel_room_id = snap->rooms[s_selected_room_idx].id;
    }

    if (snap) {
        for (uint16_t i = 0; i < snap->device_count; i++) {
            const app_device_t *dev = &snap->devices[i];
            if (sel_room_id && strcmp(dev->room_id, sel_room_id) != 0) continue;

            /* Dem so endpoint dang bat */
            int ep_on = 0;
            for (uint8_t ep = 0; ep < dev->endpoint_count; ep++) {
                for (uint8_t tr = 0; tr < dev->endpoints[ep].trait_count; tr++) {
                    if (dev->endpoints[ep].traits[tr].type == TRAIT_TYPE_ON_OFF &&
                        dev->endpoints[ep].traits[tr].state_valid &&
                        dev->endpoints[ep].traits[tr].state.b_val) {
                        ep_on++;
                        break;
                    }
                }
            }
            bool is_active = (ep_on > 0);

            /* The cong tac: Thiet ke theo phong cach The Den tren trang Home */
            lv_obj_t *card = lv_obj_create(s_cards_container);
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_size(card, 222, 116);
            lv_obj_set_style_radius(card, 18, 0);
            lv_obj_set_style_bg_color(card, is_active ? lv_color_hex(0xD97706) : lv_color_hex(0x1B212D), 0);
            lv_obj_set_style_border_color(card, lv_color_hex(0x2E384D), 0);
            lv_obj_set_style_border_width(card, is_active ? 0 : 1, 0);
            lv_obj_set_style_pad_all(card, 14, 0);
            lv_obj_set_style_shadow_width(card, 16, 0);
            lv_obj_set_style_shadow_offset_y(card, 6, 0);
            lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
            lv_obj_set_style_shadow_opa(card, is_active ? LV_OPA_30 : LV_OPA_20, 0);
            lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

            /* Ten thiet bi (Top-Left): Dung font_20 (lon hon trang thai 1 size), chieu cao 28px khong bi cat chu */
            lv_obj_t *l_name = lv_label_create(card);
            lv_obj_set_style_text_font(l_name, &font_20, 0);
            lv_label_set_text(l_name, dev->name);
            lv_obj_set_style_text_color(l_name, is_active ? lv_color_hex(0x1A1208) : lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_width(l_name, 155);
            lv_obj_set_height(l_name, 28);
            lv_obj_set_style_pad_all(l_name, 0, 0);
            lv_label_set_long_mode(l_name, LV_LABEL_LONG_DOT);
            lv_obj_align(l_name, LV_ALIGN_TOP_LEFT, 0, 0);

            /* Icon goc tren phai (Top-Right) */
            lv_obj_t *ic = lv_label_create(card);
            lv_label_set_text(ic, LV_SYMBOL_POWER);
            lv_obj_set_style_text_color(ic, is_active ? lv_color_hex(0x1A1208) : lv_color_hex(0x94A3B8), 0);
            lv_obj_align(ic, LV_ALIGN_TOP_RIGHT, 0, 2);

            /* Trang thai o goc duoi trai: Dung font_16 (nho hon ten 1 size, rat sang va tinh te) */
            lv_obj_t *l_st = lv_label_create(card);
            lv_obj_set_style_text_font(l_st, &font_16, 0);
            lv_obj_set_style_pad_all(l_st, 0, 0);
            if (!dev->online) {
                lv_label_set_text(l_st, "Ngoại tuyến");
                lv_obj_set_style_text_color(l_st, is_active ? lv_color_hex(0x7F1D1D) : lv_color_hex(0xEF4444), 0);
            } else if (dev->endpoint_count > 1) {
                char st_buf[64];
                snprintf(st_buf, sizeof(st_buf), "%d/%d đang bật", ep_on, dev->endpoint_count);
                lv_label_set_text(l_st, st_buf);
                lv_obj_set_style_text_color(l_st, is_active ? lv_color_hex(0x1A1208) : lv_color_hex(0x94A3B8), 0);
            } else {
                lv_label_set_text(l_st, is_active ? "Đang bật" : "Đã tắt");
                lv_obj_set_style_text_color(l_st, is_active ? lv_color_hex(0x1A1208) : lv_color_hex(0x94A3B8), 0);
            }
            lv_obj_align(l_st, LV_ALIGN_BOTTOM_LEFT, 0, 0);

            /* Su kien bam card */
            lv_obj_add_event_cb(card, on_device_card_clicked, LV_EVENT_CLICKED, (void *)dev->id);
        }
    }

    /* 7. Cap nhat Man hinh Canh (Scenes Grid) */
    lv_obj_clean(s_scenes_grid);
    if (s_scene_lbl_subtitle) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d cảnh", snap ? snap->scene_count : 0);
        lv_label_set_text(s_scene_lbl_subtitle, buf);
    }

    int scene_cnt = (snap && snap->scene_count > 0) ? snap->scene_count : 6;
    const char *scene_demo_names[] = {"Về nhà", "Ra ngoài", "Đi ngủ", "Xem phim", "Buổi sáng", "Bữa tối"};
    const char *scene_demo_icons[] = {LV_SYMBOL_HOME, LV_SYMBOL_RIGHT, LV_SYMBOL_BELL, LV_SYMBOL_VIDEO, LV_SYMBOL_IMAGE, LV_SYMBOL_REFRESH};
    uint32_t sc_colors[] = {0xF97316, 0x3B82F6, 0x8B5CF6, 0xEC4899, 0xEAB308, 0x10B981};

    for (int s = 0; s < scene_cnt; s++) {
        const char *name = scene_demo_names[s % 6];
        const char *id = "sample_id";
        if (snap && snap->scene_count > 0 && s < snap->scene_count) {
            name = snap->scenes[s].name;
            id = snap->scenes[s].id;
        }

        lv_obj_t *sc_btn = lv_button_create(s_scenes_grid);
        lv_obj_set_size(sc_btn, 222, 84);
        lv_obj_set_style_radius(sc_btn, 16, 0);
        lv_obj_set_style_bg_color(sc_btn, lv_color_hex(0x1C212D), 0);
        lv_obj_set_style_border_color(sc_btn, lv_color_hex(0x2E384D), 0);
        lv_obj_set_style_border_width(sc_btn, 1, 0);
        lv_obj_set_style_shadow_width(sc_btn, 12, 0);
        lv_obj_set_style_shadow_offset_y(sc_btn, 4, 0);
        lv_obj_set_style_shadow_color(sc_btn, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(sc_btn, LV_OPA_30, 0);
        lv_obj_clear_flag(sc_btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *ic = lv_label_create(sc_btn);
        lv_label_set_text(ic, scene_demo_icons[s % 6]);
        lv_obj_set_style_text_color(ic, lv_color_hex(sc_colors[s % 6]), 0);
        lv_obj_align(ic, LV_ALIGN_TOP_LEFT, 12, 10);

        lv_obj_t *txt = lv_label_create(sc_btn);
        lv_label_set_text(txt, name);
        lv_obj_set_style_text_color(txt, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_width(txt, 196);
        lv_label_set_long_mode(txt, LV_LABEL_LONG_DOT);
        lv_obj_align(txt, LV_ALIGN_BOTTOM_LEFT, 12, -12);

        scene_cmd_t *sc_cmd = (scene_cmd_t *)malloc(sizeof(scene_cmd_t));
        if (sc_cmd) {
            snprintf(sc_cmd->scene_id, sizeof(sc_cmd->scene_id), "%s", id);
            snprintf(sc_cmd->scene_name, sizeof(sc_cmd->scene_name), "%s", name);
            lv_obj_add_event_cb(sc_btn, on_scene_clicked, LV_EVENT_CLICKED, sc_cmd);
            lv_obj_add_event_cb(sc_btn, on_obj_delete_free_ud, LV_EVENT_DELETE, sc_cmd);
        }
    }

    /* 8. Cap nhat Man hinh Settings */
    if (s_settings_lbl_wifi) {
        char ssid[34] = {0};
        app_network_get_ssid(ssid, sizeof(ssid));
        int rssi = app_network_get_rssi();
        char buf[64];
        snprintf(buf, sizeof(buf), "Wi-Fi: %s (%d dBm)", ssid[0] ? ssid : "Chưa kết nối", rssi);
        lv_label_set_text(s_settings_lbl_wifi, buf);
    }
    if (s_settings_lbl_ip) {
        char ip[32] = {0};
        app_network_get_ip(ip, sizeof(ip));
        char buf[64];
        snprintf(buf, sizeof(buf), "Địa chỉ IP: %s", ip[0] ? ip : "Chưa có IP");
        lv_label_set_text(s_settings_lbl_ip, buf);
    }

    /* 9. Cap nhat Overlay Multi-Button neu dang mo */
    if (s_detail_overlay && !lv_obj_has_flag(s_detail_overlay, LV_OBJ_FLAG_HIDDEN) && s_detail_device_id[0] != '\0') {
        if (snap) {
            for (uint16_t i = 0; i < snap->device_count; i++) {
                if (strcmp(snap->devices[i].id, s_detail_device_id) == 0) {
                    render_multi_button_columns(&snap->devices[i]);
                    break;
                }
            }
        }
    }
}
