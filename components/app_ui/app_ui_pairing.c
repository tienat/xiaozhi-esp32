#include "app_ui.h"
#include "app_ui_theme.h"
#include "app_ui_i18n.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>
#include "app_config.h"
#include "app_network.h"

#define TAG "app_ui_pairing"
#define QR_CANVAS_SIZE 210
#define QR_MAX_VERSION 20

/* Khai bao nguyen mau ham qrcodegen da co san trong du an tu esp_emote_gfx */
enum qrcodegen_Ecc {
    qrcodegen_Ecc_LOW = 0,
    qrcodegen_Ecc_MEDIUM,
    qrcodegen_Ecc_QUARTILE,
    qrcodegen_Ecc_HIGH
};
enum qrcodegen_Mask {
    qrcodegen_Mask_AUTO = -1
};
#define qrcodegen_BUFFER_LEN_FOR_VERSION(n) ((((n) * 4 + 17) * ((n) * 4 + 17) + 7) / 8 + 1)

extern bool qrcodegen_encodeText(const char *text, uint8_t tempBuffer[], uint8_t qrcode[],
    enum qrcodegen_Ecc ecc, int minVersion, int maxVersion, enum qrcodegen_Mask mask, bool boostEcl);
extern int qrcodegen_getSize(const uint8_t qrcode[]);
extern bool qrcodegen_getModule(const uint8_t qrcode[], int x, int y);

static lv_obj_t *s_pairing_root = NULL;
static lv_obj_t *s_qr_container = NULL;
static lv_obj_t *s_qr_canvas = NULL;
static uint16_t *s_canvas_buf = NULL;

static lv_obj_t *s_lbl_title = NULL;
static lv_obj_t *s_lbl_guide = NULL;
static lv_obj_t *s_lbl_device_name = NULL;
static lv_obj_t *s_lbl_countdown = NULL;
static lv_obj_t *s_progress_bar = NULL;
static lv_obj_t *s_lbl_token = NULL;
static lv_obj_t *s_lbl_status = NULL;

static lv_obj_t *s_expired_box = NULL;
static lv_obj_t *s_lbl_expired = NULL;
static lv_obj_t *s_btn_refresh = NULL;

static lv_timer_t *s_countdown_timer = NULL;
static int s_remaining_sec;
static char s_last_token[128] = {0};

static void render_qr_on_canvas(const char *text)
{
    if (!s_canvas_buf || !s_qr_canvas || !text || strlen(text) == 0) return;

    /* Buffer cho qrcodegen version 1..20 */
    uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];
    uint8_t temp_buf[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];

    /* Xoa trang toan bo canvas RGB565 */
    for (int i = 0; i < QR_CANVAS_SIZE * QR_CANVAS_SIZE; i++) {
        s_canvas_buf[i] = 0xFFFF; // White RGB565
    }

    ESP_LOGI(TAG, "Rendering pairing QR (URL length=%zu)", strlen(text));
    bool ok = qrcodegen_encodeText(text, temp_buf, qrcode, qrcodegen_Ecc_MEDIUM, 1, QR_MAX_VERSION, qrcodegen_Mask_AUTO, true);
    if (!ok) {
        ESP_LOGE(TAG, "Pairing QR encoding failed (URL length=%zu)", strlen(text));
        lv_obj_invalidate(s_qr_canvas);
        return;
    }

    int qr_modules = qrcodegen_getSize(qrcode);
    int quiet_zone = 4; // 4 modules trang theo tieu chuan
    int total_modules = qr_modules + 2 * quiet_zone;
    int scale = QR_CANVAS_SIZE / total_modules;
    if (scale < 1) scale = 1;

    int offset_x = (QR_CANVAS_SIZE - total_modules * scale) / 2 + quiet_zone * scale;
    int offset_y = (QR_CANVAS_SIZE - total_modules * scale) / 2 + quiet_zone * scale;

    for (int my = 0; my < qr_modules; my++) {
        for (int mx = 0; mx < qr_modules; mx++) {
            if (qrcodegen_getModule(qrcode, mx, my)) {
                int px = offset_x + mx * scale;
                int py = offset_y + my * scale;
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        int cx = px + dx;
                        int cy = py + dy;
                        if (cx >= 0 && cx < QR_CANVAS_SIZE && cy >= 0 && cy < QR_CANVAS_SIZE) {
                            s_canvas_buf[cy * QR_CANVAS_SIZE + cx] = 0x0000; // Black RGB565
                        }
                    }
                }
            }
        }
    }

    lv_obj_invalidate(s_qr_canvas);
    ESP_LOGI(TAG, "Pairing QR rendered: %d modules, canvas %dx%d",
             qr_modules, QR_CANVAS_SIZE, QR_CANVAS_SIZE);
}

static void on_refresh_btn_click(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Nguoi dung bam Tao ma moi");
    if (s_lbl_status) {
        lv_label_set_text(s_lbl_status, "Đang tạo mã liên kết mới...");
    }
    if (s_expired_box) {
        lv_obj_add_flag(s_expired_box, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_qr_container) {
        lv_obj_remove_flag(s_qr_container, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_lbl_countdown) {
        lv_label_set_text(s_lbl_countdown, UI_CREATING_QR);
    }
    s_remaining_sec = 0;
    s_last_token[0] = '\0';
    app_gateway_create_pairing();
}

static void pairing_countdown_cb(lv_timer_t *t)
{
    (void)t;
    const app_pairing_session_t *session = app_gateway_get_pairing_session();
    s_remaining_sec = session ? session->remaining_sec : 0;
    if (s_remaining_sec > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d:%02d", s_remaining_sec / 60, s_remaining_sec % 60);
        if (s_lbl_countdown) lv_label_set_text(s_lbl_countdown, buf);
        if (s_progress_bar) lv_bar_set_value(s_progress_bar, s_remaining_sec, LV_ANIM_ON);
    } else if (session && session->retry_available) {
        if (s_countdown_timer) lv_timer_pause(s_countdown_timer);
        if (s_qr_container) lv_obj_add_flag(s_qr_container, LV_OBJ_FLAG_HIDDEN);
        if (s_expired_box) lv_obj_remove_flag(s_expired_box, LV_OBJ_FLAG_HIDDEN);
        if (s_lbl_countdown) lv_label_set_text(s_lbl_countdown, "00:00");
        if (s_progress_bar) lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
    }
}

lv_obj_t *app_ui_pairing_create(lv_obj_t *parent)
{
    s_pairing_root = lv_obj_create(parent);
    lv_obj_set_size(s_pairing_root, 480, 480);
    lv_obj_set_style_bg_color(s_pairing_root, lv_color_hex(APP_UI_BG), 0);
    lv_obj_set_style_border_width(s_pairing_root, 0, 0);
    lv_obj_set_style_pad_all(s_pairing_root, 0, 0);
    lv_obj_clear_flag(s_pairing_root, LV_OBJ_FLAG_SCROLLABLE);

    /* 1. Tieu de man hinh */
    s_lbl_title = lv_label_create(s_pairing_root);
    lv_label_set_text(s_lbl_title, UI_LINK_TITLE);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_lbl_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_lbl_title, LV_ALIGN_TOP_MID, 0, 16);

    /* 2. Huong dan su dung tren App NobleX */
    s_lbl_guide = lv_label_create(s_pairing_root);
    lv_label_set_text(s_lbl_guide, UI_LINK_GUIDE);
    lv_obj_set_style_text_color(s_lbl_guide, lv_color_hex(0x9E9E9E), 0);
    lv_obj_set_style_text_align(s_lbl_guide, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_lbl_guide, 448);
    lv_obj_align(s_lbl_guide, LV_ALIGN_TOP_MID, 0, 46);

    /* 3. Khung chua ma QR (Nen trang tuyet doi, vien bo tron, can le giua) */
    s_qr_container = lv_obj_create(s_pairing_root);
    lv_obj_set_size(s_qr_container, 226, 226);
    lv_obj_set_style_bg_color(s_qr_container, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(s_qr_container, 14, 0);
    lv_obj_set_style_border_width(s_qr_container, 0, 0);
    lv_obj_set_style_pad_all(s_qr_container, 8, 0);
    lv_obj_clear_flag(s_qr_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_qr_container, LV_ALIGN_TOP_MID, 0, 116);

    /* Ma QR ve tren LVGL Canvas (Kich thuoc 210px) */
    if (!s_canvas_buf) {
        s_canvas_buf = (uint16_t *)heap_caps_malloc(QR_CANVAS_SIZE * QR_CANVAS_SIZE * sizeof(uint16_t),
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    s_qr_canvas = lv_canvas_create(s_qr_container);
    if (s_canvas_buf) {
        for (int i = 0; i < QR_CANVAS_SIZE * QR_CANVAS_SIZE; i++) {
            s_canvas_buf[i] = 0xFFFF; // Xoa trang tuyet doi RGB565 (tranh noise)
        }
        lv_canvas_set_buffer(s_qr_canvas, s_canvas_buf, QR_CANVAS_SIZE, QR_CANVAS_SIZE, LV_COLOR_FORMAT_RGB565);
    }
    lv_obj_center(s_qr_canvas);

    /* 4. Khung thong bao khi Het han hoac Bi tu choi */
    s_expired_box = lv_obj_create(s_pairing_root);
    lv_obj_set_size(s_expired_box, 360, 200);
    lv_obj_set_style_bg_color(s_expired_box, lv_color_hex(APP_UI_SURFACE), 0);
    lv_obj_set_style_radius(s_expired_box, 16, 0);
    lv_obj_set_style_border_color(s_expired_box, lv_color_hex(0xE53935), 0);
    lv_obj_set_style_border_width(s_expired_box, 2, 0);
    lv_obj_align(s_expired_box, LV_ALIGN_TOP_MID, 0, 126);
    lv_obj_clear_flag(s_expired_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_expired_box, LV_OBJ_FLAG_HIDDEN);

    s_lbl_expired = lv_label_create(s_expired_box);
    lv_label_set_text(s_lbl_expired, UI_EXPIRED);
    lv_obj_set_style_text_color(s_lbl_expired, lv_color_hex(0xFFCDD2), 0);
    lv_obj_set_style_text_align(s_lbl_expired, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_lbl_expired, LV_ALIGN_TOP_MID, 0, 20);

    s_btn_refresh = lv_button_create(s_expired_box);
    lv_obj_set_size(s_btn_refresh, 180, 48);
    lv_obj_set_style_bg_color(s_btn_refresh, lv_color_hex(APP_UI_ACCENT), 0);
    lv_obj_set_style_radius(s_btn_refresh, 24, 0);
    lv_obj_align(s_btn_refresh, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_add_event_cb(s_btn_refresh, on_refresh_btn_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_lbl = lv_label_create(s_btn_refresh);
    lv_label_set_text(btn_lbl, UI_NEW_QR);
    lv_obj_set_style_text_color(btn_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btn_lbl);

    /* 5. Hang thong tin: Ten thiet bi ben trai, Thoi gian dem nguoc ben phai */
    s_lbl_device_name = lv_label_create(s_pairing_root);
    lv_label_set_text(s_lbl_device_name, "Home Center");
    lv_obj_set_style_text_color(s_lbl_device_name, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(s_lbl_device_name, LV_ALIGN_TOP_LEFT, 40, 366);

    s_lbl_countdown = lv_label_create(s_pairing_root);
    lv_label_set_text(s_lbl_countdown, "02:00");
    lv_obj_set_style_text_color(s_lbl_countdown, lv_color_hex(APP_UI_ACCENT), 0);
    lv_obj_align(s_lbl_countdown, LV_ALIGN_TOP_RIGHT, -40, 366);

    /* 6. Thanh progress bar dem nguoc */
    s_progress_bar = lv_bar_create(s_pairing_root);
    lv_obj_set_size(s_progress_bar, 400, 8);
    lv_obj_align(s_progress_bar, LV_ALIGN_TOP_MID, 0, 400);
    lv_obj_set_style_radius(s_progress_bar, 4, 0);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x292929), 0);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(APP_UI_ACCENT), LV_PART_INDICATOR);
    lv_bar_set_range(s_progress_bar, 0, 120);
    lv_bar_set_value(s_progress_bar, 120, LV_ANIM_OFF);

    /* Bo 2 dong: Ma nhap tay va San sang lien ket */
    s_lbl_token = NULL;
    s_lbl_status = NULL;

    /* Timer dem nguoc 1 giay don dieu (tam dung cho den khi nhan duoc phien pairing) */
    s_countdown_timer = lv_timer_create(pairing_countdown_cb, 1000, NULL);
    lv_timer_pause(s_countdown_timer);

    return s_pairing_root;
}

void app_ui_pairing_update(void)
{
    if (!s_pairing_root) return;

    app_gw_state_t state = app_gateway_get_state();
    const app_pairing_session_t *sess = app_gateway_get_pairing_session();

    if (s_lbl_status && (state == GW_STATE_DISCONNECTED || state == GW_STATE_ERROR)) {
        lv_label_set_text(s_lbl_status, UI_SERVER_LOST);
    }

    /* Hien nut thu lai ngay khi Gateway het han, khong doi timer UI tu dem ve 0. */
    if (sess && sess->retry_available && !sess->active && !sess->denied &&
        state != GW_STATE_LINKED_RUNNING) {
        if (s_countdown_timer) lv_timer_pause(s_countdown_timer);
        if (s_qr_container) lv_obj_add_flag(s_qr_container, LV_OBJ_FLAG_HIDDEN);
        if (s_expired_box) lv_obj_remove_flag(s_expired_box, LV_OBJ_FLAG_HIDDEN);
        bool expired = sess->token[0] && sess->remaining_sec <= 0;
        if (s_lbl_expired) {
            lv_label_set_text(s_lbl_expired, expired ?
                UI_EXPIRED : UI_NEW_QR);
        }
        if (s_lbl_countdown) lv_label_set_text(s_lbl_countdown, expired ? UI_EXPIRED : UI_NEW_QR);
        return;
    }

    if (sess != NULL && (state == GW_STATE_UNLINKED_PAIRING ||
                         (sess->active && state != GW_STATE_LINKED_RUNNING))) {
        if (sess->denied) {
            /* Bi tu choi tren app */
            if (s_countdown_timer) lv_timer_pause(s_countdown_timer);
            if (s_qr_container) lv_obj_add_flag(s_qr_container, LV_OBJ_FLAG_HIDDEN);
            if (s_expired_box) lv_obj_remove_flag(s_expired_box, LV_OBJ_FLAG_HIDDEN);
            if (s_lbl_expired) {
                lv_label_set_text(s_lbl_expired, UI_DENIED);
            }
            if (s_lbl_status) lv_label_set_text(s_lbl_status, UI_DENIED);
            return;
        }

        if (sess->active && strlen(sess->pair_url) > 0) {
            if (s_countdown_timer) lv_timer_resume(s_countdown_timer);
            /* Kiem tra xem token co moi khong */
            if (strcmp(s_last_token, sess->token) != 0) {
                snprintf(s_last_token, sizeof(s_last_token), "%s", sess->token);
                s_remaining_sec = sess->remaining_sec;
                if (s_progress_bar) {
                    int max_sec = sess->remaining_sec > 0 ? sess->remaining_sec : 120;
                    lv_bar_set_range(s_progress_bar, 0, max_sec);
                    lv_bar_set_value(s_progress_bar, s_remaining_sec, LV_ANIM_OFF);
                }
                if (s_lbl_countdown && s_remaining_sec > 0) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%02d:%02d", s_remaining_sec / 60, s_remaining_sec % 60);
                    lv_label_set_text(s_lbl_countdown, buf);
                }

                /* Ve ma QR tren Canvas tu pair_url nguyen van */
                render_qr_on_canvas(sess->pair_url);

                /* Hien QR container, an hop expired */
                lv_obj_remove_flag(s_qr_container, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(s_expired_box, LV_OBJ_FLAG_HIDDEN);

                /* Cap nhat token trần */
                char token_buf[160];
                snprintf(token_buf, sizeof(token_buf), UI_CODE, sess->token);
                if (s_lbl_token) lv_label_set_text(s_lbl_token, token_buf);

                if (s_lbl_status) lv_label_set_text(s_lbl_status, UI_READY);
            }
        } else {
            if (s_countdown_timer) lv_timer_pause(s_countdown_timer);
            if (s_lbl_countdown && !sess->denied) {
                lv_label_set_text(s_lbl_countdown, UI_WAIT_QR);
            }
        }
    } else if (state == GW_STATE_DISCONNECTED || state == GW_STATE_ERROR) {
        if (s_countdown_timer) lv_timer_pause(s_countdown_timer);
        if (s_lbl_status) {
            if (!app_network_is_connected()) {
                lv_label_set_text(s_lbl_status, UI_WIFI_ERROR);
            } else if (strlen(app_config_get_client_secret()) == 0) {
                lv_label_set_text(s_lbl_status, UI_CLOUD_WAIT);
            } else {
                lv_label_set_text(s_lbl_status, UI_SERVER_LOST);
            }
        }
    } else if (state == GW_STATE_AUTHENTICATING || state == GW_STATE_FETCHING_CONFIG || state == GW_STATE_MQTT_CONNECTING) {
        if (s_lbl_status) {
            lv_label_set_text(s_lbl_status, UI_CLOUD_WAIT);
        }
    }
}
