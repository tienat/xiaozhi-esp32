#include "app_ui.h"
#include <esp_log.h>
#include <stdio.h>
#include <string.h>

#define TAG "app_ui_cards"

static lv_obj_t *s_cards_root = NULL;
static lv_obj_t *s_lbl_home_name = NULL;
static lv_obj_t *s_lbl_home_subtitle = NULL;
static lv_obj_t *s_btn_unlink = NULL;

static lv_obj_t *s_room_bar = NULL;
static lv_obj_t *s_cards_container = NULL;
static lv_obj_t *s_scenes_bar = NULL;

static int s_selected_room_idx = -1; /* -1: Tat ca */

typedef struct {
    char device_id[64];
    char endpoint_id[16];
    char trait_name[32];
} trait_user_data_t;

/* Callback khi bat/tat Cong tac On/Off */
static void on_switch_toggled(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    trait_user_data_t *ud = (trait_user_data_t *)lv_event_get_user_data(e);
    if (!ud) return;

    bool state = lv_obj_has_state(sw, LV_STATE_CHECKED);
    char payload[32];
    snprintf(payload, sizeof(payload), "{\"on\":%s}", state ? "true" : "false");

    ESP_LOGI(TAG, "Dieu khien On/Off [%s/%s]: %s", ud->device_id, ud->endpoint_id, payload);
    app_gateway_send_command(ud->device_id, ud->endpoint_id, "on_off", payload);
}

/* Callback khi keo slider Do sang Brightness */
static void on_brightness_changed(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    trait_user_data_t *ud = (trait_user_data_t *)lv_event_get_user_data(e);
    if (!ud) return;

    int32_t val = lv_slider_get_value(slider);
    char payload[32];
    snprintf(payload, sizeof(payload), "{\"brightness\":%ld}", (long)val);

    ESP_LOGI(TAG, "Dieu khien Do sang [%s/%s]: %ld%%", ud->device_id, ud->endpoint_id, (long)val);
    app_gateway_send_command(ud->device_id, ud->endpoint_id, "brightness", payload);
}

/* Callback khi bam nut Rem cua (Mo / Dung / Dong) */
static void on_curtain_btn_clicked(lv_event_t *e)
{
    trait_user_data_t *ud = (trait_user_data_t *)lv_event_get_user_data(e);
    if (!ud) return;

    lv_obj_t *target = lv_event_get_target(e);
    const char *action = (const char *)lv_obj_get_user_data(target);
    if (!action) action = "stop";

    char payload[64];
    if (strcmp(action, "open") == 0) {
        snprintf(payload, sizeof(payload), "{\"position\":100}");
    } else if (strcmp(action, "close") == 0) {
        snprintf(payload, sizeof(payload), "{\"position\":0}");
    } else {
        snprintf(payload, sizeof(payload), "{\"action\":\"stop\"}");
    }

    ESP_LOGI(TAG, "Dieu khien Rem [%s/%s]: %s", ud->device_id, ud->endpoint_id, payload);
    app_gateway_send_command(ud->device_id, ud->endpoint_id, "position", payload);
}

/* Callback khi bam Kich ban Scene */
static void on_scene_clicked(lv_event_t *e)
{
    const char *scene_id = (const char *)lv_event_get_user_data(e);
    if (scene_id) {
        ESP_LOGI(TAG, "Kich hoat kịch ban: %s", scene_id);
        app_gateway_run_scene(scene_id);
    }
}

/* Callback khi bam Go lien ket */
static void on_unlink_clicked(lv_event_t *e)
{
    (void)e;
    ESP_LOGW(TAG, "Nguoi dung yeu cau Go lien ket Home Center");
    app_gateway_unlink();
}

/* Callback khi chon Phong */
static void on_room_btn_clicked(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_selected_room_idx = idx;
    app_ui_cards_update();
}

lv_obj_t *app_ui_cards_create(lv_obj_t *parent)
{
    s_cards_root = lv_obj_create(parent);
    lv_obj_set_size(s_cards_root, 480, 480);
    lv_obj_set_style_bg_color(s_cards_root, lv_color_hex(0x10131A), 0);
    lv_obj_set_style_border_width(s_cards_root, 0, 0);
    lv_obj_set_style_pad_all(s_cards_root, 0, 0);
    lv_obj_clear_flag(s_cards_root, LV_OBJ_FLAG_SCROLLABLE);

    /* 1. Header tren cung (Cao 50px) */
    lv_obj_t *header = lv_obj_create(s_cards_root);
    lv_obj_set_size(header, 480, 52);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x181D28), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_hor(header, 14, 0);
    lv_obj_set_style_pad_ver(header, 6, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_home_name = lv_label_create(header);
    lv_label_set_text(s_lbl_home_name, "Nhà thông minh NobleX");
    lv_obj_set_style_text_color(s_lbl_home_name, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_lbl_home_name, LV_ALIGN_LEFT_MID, 0, -8);

    s_lbl_home_subtitle = lv_label_create(header);
    lv_label_set_text(s_lbl_home_subtitle, "0 thiết bị · Trực tuyến");
    lv_obj_set_style_text_color(s_lbl_home_subtitle, lv_color_hex(0x81C784), 0);
    lv_obj_align(s_lbl_home_subtitle, LV_ALIGN_LEFT_MID, 0, 10);

    /* Nut Go lien ket o goc phai */
    s_btn_unlink = lv_button_create(header);
    lv_obj_set_size(s_btn_unlink, 72, 32);
    lv_obj_set_style_bg_color(s_btn_unlink, lv_color_hex(0x374151), 0);
    lv_obj_set_style_radius(s_btn_unlink, 8, 0);
    lv_obj_align(s_btn_unlink, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(s_btn_unlink, on_unlink_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_unlink = lv_label_create(s_btn_unlink);
    lv_label_set_text(lbl_unlink, "Gỡ");
    lv_obj_set_style_text_color(lbl_unlink, lv_color_hex(0xE57373), 0);
    lv_obj_center(lbl_unlink);

    /* 2. Thanh chon phong (Room Bar) - Cuon ngang */
    s_room_bar = lv_obj_create(s_cards_root);
    lv_obj_set_size(s_room_bar, 480, 42);
    lv_obj_set_style_bg_color(s_room_bar, lv_color_hex(0x131720), 0);
    lv_obj_set_style_border_width(s_room_bar, 0, 0);
    lv_obj_set_style_pad_hor(s_room_bar, 10, 0);
    lv_obj_set_style_pad_ver(s_room_bar, 4, 0);
    lv_obj_set_flex_flow(s_room_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_room_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(s_room_bar, LV_ALIGN_TOP_MID, 0, 52);

    /* 3. Luoi the thiet bi (Cards Grid Container) - Cuon doc */
    s_cards_container = lv_obj_create(s_cards_root);
    lv_obj_set_size(s_cards_container, 480, 314);
    lv_obj_set_style_bg_color(s_cards_container, lv_color_hex(0x10131A), 0);
    lv_obj_set_style_border_width(s_cards_container, 0, 0);
    lv_obj_set_style_pad_all(s_cards_container, 10, 0);
    lv_obj_set_flex_flow(s_cards_container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_cards_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_align(s_cards_container, LV_ALIGN_TOP_MID, 0, 94);

    /* 4. Thanh kịch ban Scenes o duoi cung (Cao 68px) */
    s_scenes_bar = lv_obj_create(s_cards_root);
    lv_obj_set_size(s_scenes_bar, 480, 68);
    lv_obj_set_style_bg_color(s_scenes_bar, lv_color_hex(0x181D28), 0);
    lv_obj_set_style_border_width(s_scenes_bar, 0, 0);
    lv_obj_set_style_pad_hor(s_scenes_bar, 10, 0);
    lv_obj_set_style_pad_ver(s_scenes_bar, 8, 0);
    lv_obj_set_flex_flow(s_scenes_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_scenes_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(s_scenes_bar, LV_ALIGN_BOTTOM_MID, 0, 0);

    return s_cards_root;
}

void app_ui_cards_update(void)
{
    if (!s_cards_root) return;

    const app_snapshot_t *snap = app_gateway_get_snapshot();
    if (!snap) return;

    /* 1. Cap nhat thong tin nha */
    if (strlen(snap->home_name) > 0) {
        lv_label_set_text(s_lbl_home_name, snap->home_name);
    }
    char sub_buf[64];
    snprintf(sub_buf, sizeof(sub_buf), "%d thiết bị · Trực tuyến", snap->device_count);
    lv_label_set_text(s_lbl_home_subtitle, sub_buf);

    /* 2. Cap nhat danh sach phong tren Room Bar */
    lv_obj_clean(s_room_bar);

    /* Nut "Tat ca" */
    lv_obj_t *btn_all = lv_button_create(s_room_bar);
    lv_obj_set_size(btn_all, 74, 30);
    lv_obj_set_style_radius(btn_all, 15, 0);
    lv_obj_set_style_bg_color(btn_all, (s_selected_room_idx == -1) ? lv_color_hex(0x1976D2) : lv_color_hex(0x263238), 0);
    lv_obj_add_event_cb(btn_all, on_room_btn_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    lv_obj_t *lbl_all = lv_label_create(btn_all);
    lv_label_set_text(lbl_all, "Tất cả");
    lv_obj_set_style_text_color(lbl_all, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_all);

    /* Cac nut Phong */
    for (uint16_t r = 0; r < snap->room_count; r++) {
        lv_obj_t *r_btn = lv_button_create(s_room_bar);
        lv_obj_set_size(r_btn, 88, 30);
        lv_obj_set_style_radius(r_btn, 15, 0);
        lv_obj_set_style_bg_color(r_btn, (s_selected_room_idx == (int)r) ? lv_color_hex(0x1976D2) : lv_color_hex(0x263238), 0);
        lv_obj_add_event_cb(r_btn, on_room_btn_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)r);

        lv_obj_t *r_lbl = lv_label_create(r_btn);
        lv_label_set_text(r_lbl, snap->rooms[r].name);
        lv_obj_set_style_text_color(r_lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(r_lbl);
    }

    /* 3. Cap nhat luoi the thiet bi */
    lv_obj_clean(s_cards_container);

    const char *selected_room_id = NULL;
    if (s_selected_room_idx >= 0 && s_selected_room_idx < (int)snap->room_count) {
        selected_room_id = snap->rooms[s_selected_room_idx].id;
    }

    for (uint16_t i = 0; i < snap->device_count; i++) {
        const app_device_t *dev = &snap->devices[i];

        /* Loc theo phong */
        if (selected_room_id != NULL && strcmp(dev->room_id, selected_room_id) != 0) {
            continue;
        }

        /* Tao the thiet bi (Card) */
        lv_obj_t *card = lv_obj_create(s_cards_container);
        lv_obj_set_size(card, 222, 110);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1B212D), 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0x2D3748), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_pad_all(card, 8, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        /* Dong 1: Ten thiet bi va dau cham online */
        lv_obj_t *lbl_name = lv_label_create(card);
        lv_label_set_text(lbl_name, dev->name);
        lv_obj_set_style_text_color(lbl_name, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_width(lbl_name, 170);
        lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_DOT);
        lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *dot = lv_obj_create(card);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, 4, 0);
        lv_obj_set_style_bg_color(dot, dev->online ? lv_color_hex(0x4CAF50) : lv_color_hex(0x757575), 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, 0, 2);

        /* Dong 2: Loai thiet bi */
        lv_obj_t *lbl_cat = lv_label_create(card);
        lv_label_set_text(lbl_cat, dev->category[0] ? dev->category : "device");
        lv_obj_set_style_text_color(lbl_cat, lv_color_hex(0x90A4AE), 0);
        lv_obj_align(lbl_cat, LV_ALIGN_TOP_LEFT, 0, 20);

        /* Dong 3: Widget dieu khien theo Trait */
        /* Duyet qua cac endpoints va traits */
        bool control_added = false;
        for (uint8_t ep = 0; ep < dev->endpoint_count && !control_added; ep++) {
            const app_endpoint_t *endpoint = &dev->endpoints[ep];
            for (uint8_t tr = 0; tr < endpoint->trait_count && !control_added; tr++) {
                const app_trait_t *trait = &endpoint->traits[tr];

                trait_user_data_t *ud = (trait_user_data_t *)malloc(sizeof(trait_user_data_t));
                if (!ud) continue;
                snprintf(ud->device_id, sizeof(ud->device_id), "%s", dev->id);
                snprintf(ud->endpoint_id, sizeof(ud->endpoint_id), "%s", endpoint->id);
                snprintf(ud->trait_name, sizeof(ud->trait_name), "%s", trait->name);

                if (trait->type == TRAIT_TYPE_ON_OFF) {
                    lv_obj_t *sw = lv_switch_create(card);
                    lv_obj_set_size(sw, 48, 26);
                    lv_obj_align(sw, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
                    if (trait->state_valid && trait->state.b_val) {
                        lv_obj_add_state(sw, LV_STATE_CHECKED);
                    }
                    lv_obj_add_event_cb(sw, on_switch_toggled, LV_EVENT_VALUE_CHANGED, ud);
                    control_added = true;
                } else if (trait->type == TRAIT_TYPE_BRIGHTNESS) {
                    lv_obj_t *slider = lv_slider_create(card);
                    lv_obj_set_size(slider, 120, 14);
                    lv_slider_set_range(slider, 0, 100);
                    if (trait->state_valid) {
                        lv_slider_set_value(slider, (int32_t)trait->state.num_val, LV_ANIM_OFF);
                    }
                    lv_obj_align(slider, LV_ALIGN_BOTTOM_LEFT, 0, -4);
                    lv_obj_add_event_cb(slider, on_brightness_changed, LV_EVENT_RELEASED, ud);
                    control_added = true;
                } else if (trait->type == TRAIT_TYPE_POSITION || trait->type == TRAIT_TYPE_COVER_MOTION) {
                    lv_obj_t *btn_open = lv_button_create(card);
                    lv_obj_set_size(btn_open, 42, 26);
                    lv_obj_align(btn_open, LV_ALIGN_BOTTOM_LEFT, 0, 0);
                    lv_obj_set_user_data(btn_open, (void *)"open");
                    lv_obj_add_event_cb(btn_open, on_curtain_btn_clicked, LV_EVENT_CLICKED, ud);
                    lv_obj_t *l_open = lv_label_create(btn_open);
                    lv_label_set_text(l_open, "Mở");
                    lv_obj_center(l_open);

                    lv_obj_t *btn_stop = lv_button_create(card);
                    lv_obj_set_size(btn_stop, 42, 26);
                    lv_obj_align(btn_stop, LV_ALIGN_BOTTOM_LEFT, 46, 0);
                    lv_obj_set_user_data(btn_stop, (void *)"stop");
                    lv_obj_add_event_cb(btn_stop, on_curtain_btn_clicked, LV_EVENT_CLICKED, ud);
                    lv_obj_t *l_stop = lv_label_create(btn_stop);
                    lv_label_set_text(l_stop, "Dừng");
                    lv_obj_center(l_stop);

                    lv_obj_t *btn_close = lv_button_create(card);
                    lv_obj_set_size(btn_close, 42, 26);
                    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_LEFT, 92, 0);
                    lv_obj_set_user_data(btn_close, (void *)"close");
                    lv_obj_add_event_cb(btn_close, on_curtain_btn_clicked, LV_EVENT_CLICKED, ud);
                    lv_obj_t *l_close = lv_label_create(btn_close);
                    lv_label_set_text(l_close, "Đóng");
                    lv_obj_center(l_close);

                    control_added = true;
                } else if (trait->type == TRAIT_TYPE_TEMPERATURE || trait->type == TRAIT_TYPE_HUMIDITY) {
                    char val_str[32];
                    if (trait->state_valid) {
                        snprintf(val_str, sizeof(val_str), "%.1f %s", trait->state.num_val, trait->unit);
                    } else {
                        snprintf(val_str, sizeof(val_str), "-- %s", trait->unit);
                    }
                    lv_obj_t *lbl_val = lv_label_create(card);
                    lv_label_set_text(lbl_val, val_str);
                    lv_obj_set_style_text_color(lbl_val, lv_color_hex(0x4FC3F7), 0);
                    lv_obj_align(lbl_val, LV_ALIGN_BOTTOM_LEFT, 0, 0);
                    free(ud);
                    control_added = true;
                } else {
                    free(ud);
                }
            }
        }
    }

    /* 4. Cap nhat danh sach Kịch ban Scenes */
    lv_obj_clean(s_scenes_bar);
    for (uint16_t s = 0; s < snap->scene_count; s++) {
        const app_scene_t *sc = &snap->scenes[s];
        lv_obj_t *sc_btn = lv_button_create(s_scenes_bar);
        lv_obj_set_size(sc_btn, 110, 48);
        lv_obj_set_style_radius(sc_btn, 12, 0);
        lv_obj_set_style_bg_color(sc_btn, lv_color_hex(0x2A3447), 0);
        lv_obj_add_event_cb(sc_btn, on_scene_clicked, LV_EVENT_CLICKED, (void *)sc->id);

        lv_obj_t *sc_lbl = lv_label_create(sc_btn);
        lv_label_set_text(sc_lbl, sc->name);
        lv_obj_set_style_text_color(sc_lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(sc_lbl);
    }
}
