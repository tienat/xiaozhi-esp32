#include "app_ui.h"
#include <esp_log.h>
#include <stdio.h>
#include <string.h>

#define TAG "app_ui_ai_widget"

static lv_obj_t *s_ai_widget = NULL;
static lv_obj_t *s_lbl_icon = NULL;
static lv_obj_t *s_lbl_text = NULL;

static app_ui_toggle_voice_cb_t s_toggle_voice_cb = NULL;

void app_ui_register_toggle_voice_cb(app_ui_toggle_voice_cb_t cb)
{
    s_toggle_voice_cb = cb;
}

void app_ui_toggle_voice_chat(void)
{
    ESP_LOGI(TAG, "Touch vao AI Widget -> trigger toggle voice");
    if (s_toggle_voice_cb) {
        s_toggle_voice_cb();
    }
}

static void on_widget_clicked(lv_event_t *e)
{
    (void)e;
    app_ui_toggle_voice_chat();
}

lv_obj_t *app_ui_ai_widget_create(lv_obj_t *parent)
{
    /* Widget dang Floating Pill can le goc duoi ben phai */
    s_ai_widget = lv_obj_create(parent);
    lv_obj_set_size(s_ai_widget, 140, 44);
    lv_obj_set_style_bg_color(s_ai_widget, lv_color_hex(0x222B3C), 0);
    lv_obj_set_style_bg_opa(s_ai_widget, LV_OPA_90, 0);
    lv_obj_set_style_border_color(s_ai_widget, lv_color_hex(0x3B82F6), 0);
    lv_obj_set_style_border_width(s_ai_widget, 2, 0);
    lv_obj_set_style_radius(s_ai_widget, 22, 0);
    lv_obj_set_style_pad_hor(s_ai_widget, 12, 0);
    lv_obj_set_style_pad_ver(s_ai_widget, 6, 0);
    lv_obj_clear_flag(s_ai_widget, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_ai_widget, LV_ALIGN_BOTTOM_RIGHT, -12, -76);

    /* Bắt sự kiện chạm để bật/tắt trò chuyện */
    lv_obj_add_event_cb(s_ai_widget, on_widget_clicked, LV_EVENT_CLICKED, NULL);

    /* Icon trợ lý */
    s_lbl_icon = lv_label_create(s_ai_widget);
    lv_label_set_text(s_lbl_icon, "AI");
    lv_obj_set_style_text_color(s_lbl_icon, lv_color_hex(0x60A5FA), 0);
    lv_obj_align(s_lbl_icon, LV_ALIGN_LEFT_MID, 0, 0);

    /* Text trang thai */
    s_lbl_text = lv_label_create(s_ai_widget);
    lv_label_set_text(s_lbl_text, "Sẵn sàng");
    lv_obj_set_style_text_color(s_lbl_text, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_lbl_text, LV_ALIGN_RIGHT_MID, 0, 0);

    return s_ai_widget;
}

void app_ui_ai_widget_update(const char *state)
{
    if (!s_ai_widget || !state) return;

    if (strcmp(state, "listening") == 0) {
        lv_label_set_text(s_lbl_text, "Đang nghe");
        lv_obj_set_style_border_color(s_ai_widget, lv_color_hex(0x10B981), 0);
        lv_obj_set_style_text_color(s_lbl_icon, lv_color_hex(0x34D399), 0);
    } else if (strcmp(state, "thinking") == 0) {
        lv_label_set_text(s_lbl_text, "Đang xử lý");
        lv_obj_set_style_border_color(s_ai_widget, lv_color_hex(0xF59E0B), 0);
        lv_obj_set_style_text_color(s_lbl_icon, lv_color_hex(0xFBBF24), 0);
    } else if (strcmp(state, "speaking") == 0) {
        lv_label_set_text(s_lbl_text, "Đang nói");
        lv_obj_set_style_border_color(s_ai_widget, lv_color_hex(0x3B82F6), 0);
        lv_obj_set_style_text_color(s_lbl_icon, lv_color_hex(0x60A5FA), 0);
    } else {
        lv_label_set_text(s_lbl_text, "Sẵn sàng");
        lv_obj_set_style_border_color(s_ai_widget, lv_color_hex(0x6B7280), 0);
        lv_obj_set_style_text_color(s_lbl_icon, lv_color_hex(0x9CA3AF), 0);
    }
}
