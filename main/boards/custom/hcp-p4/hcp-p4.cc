#include "application.h"
#include "audio/codecs/box_audio_codec.h"
#include "button.h"
#include "config.h"
#include "display/display.h"
#include "display/lcd_display.h"
#include "lvgl_theme.h"
#include "wifi_board.h"
#include "app_mcp.h"
#include "app_network.h"
#include "app_ui.h"

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_idf_version.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_ldo_regulator.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "st7102_init_seq.inc"

#define TAG "HcpP4Board"

class HcpGpioBacklight : public Backlight {
private:
    gpio_num_t pin_;
public:
    HcpGpioBacklight(gpio_num_t pin) : pin_(pin) {
        gpio_config_t io_cfg = {
            .pin_bit_mask = (1ULL << pin_),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_cfg);
        gpio_set_level(pin_, 0); // Active-LOW: 0 = bat den nen
    }
protected:
    virtual void SetBrightnessImpl(uint8_t brightness) override {
        // Active-LOW: brightness > 0 => muc 0 (bat), brightness == 0 => muc 1 (tat)
        gpio_set_level(pin_, brightness > 0 ? 0 : 1);
    }
};

class HcpP4Board : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    Button boot_button_;
    LcdDisplay* display_ = nullptr;
    esp_lcd_panel_handle_t panel_handle_ = nullptr;
    esp_ldo_channel_handle_t ldo4_handle_ = nullptr;

    /* Bit-bang 9-bit SPI 3-wire for ST7102 init */
    static inline void Spi9ClockBit(int bit) {
        gpio_set_level(LCD_SPI_SCK_PIN, 0);
        gpio_set_level(LCD_SPI_SDA_PIN, bit);
        esp_rom_delay_us(1);
        gpio_set_level(LCD_SPI_SCK_PIN, 1);
        esp_rom_delay_us(1);
    }

    static void Spi9Write(int dc, uint8_t val) {
        gpio_set_level(LCD_SPI_CS_PIN, 0);
        Spi9ClockBit(dc);
        for (int i = 7; i >= 0; i--) {
            Spi9ClockBit((val >> i) & 1);
        }
        gpio_set_level(LCD_SPI_SCK_PIN, 0);
        gpio_set_level(LCD_SPI_CS_PIN, 1);
        esp_rom_delay_us(1);
    }

    static void SendCmd(uint8_t cmd, const uint8_t* data, int len) {
        Spi9Write(0, cmd);
        for (int i = 0; i < len; i++) {
            Spi9Write(1, data[i]);
        }
    }

    void InitSt7102Controller() {
        /* DB0 NC pull low */
        gpio_config_t nc_cfg = {
            .pin_bit_mask = (1ULL << LCD_DB0_NC_PIN),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&nc_cfg);
        gpio_set_level(LCD_DB0_NC_PIN, 0);

        /* SPI control pins */
        gpio_config_t ctrl_cfg = {
            .pin_bit_mask = (1ULL << LCD_SPI_CS_PIN) | (1ULL << LCD_SPI_SCK_PIN) |
                            (1ULL << LCD_SPI_SDA_PIN) | (1ULL << LCD_RST_PIN),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&ctrl_cfg);
        gpio_set_level(LCD_SPI_CS_PIN, 1);
        gpio_set_level(LCD_SPI_SCK_PIN, 0);

        /* Reset cycle */
        gpio_set_level(LCD_RST_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(LCD_RST_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(LCD_RST_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(100));

        /* Send init sequence */
        for (size_t i = 0; i < sizeof(s_init_seq) / sizeof(s_init_seq[0]); i++) {
            const lcd_init_cmd_t* c = &s_init_seq[i];
            SendCmd(c->cmd, c->data, c->data_len);
            if (c->delay_ms) {
                vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
            }
        }
    }

    void InitializeI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = AUDIO_CODEC_I2C_PORT,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus_));
    }

    void InitializeLcd() {
        ESP_LOGI(TAG, "Cap nguon LDO_VO4 cho GPIO39-48");
        esp_ldo_channel_config_t ldo_cfg = {
            .chan_id = 4,
            .voltage_mv = 3300,
        };
        ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &ldo4_handle_));
        vTaskDelay(pdMS_TO_TICKS(20));

        ESP_LOGI(TAG, "Khoi tao ST7102 controller qua SPI bit-bang");
        InitSt7102Controller();

        ESP_LOGI(TAG, "Khoi tao RGB LCD Panel");
        esp_lcd_rgb_panel_config_t rgb_cfg = {
            .clk_src = LCD_CLK_SRC_DEFAULT,
            .timings = {
                .pclk_hz = LCD_PCLK_HZ,
                .h_res = DISPLAY_WIDTH,
                .v_res = DISPLAY_HEIGHT,
                .hsync_pulse_width = LCD_HPW,
                .hsync_back_porch = LCD_HBP,
                .hsync_front_porch = LCD_HFP,
                .vsync_pulse_width = LCD_VPW,
                .vsync_back_porch = LCD_VBP,
                .vsync_front_porch = LCD_VFP,
                .flags = {
                    .hsync_idle_low = 0,
                    .vsync_idle_low = 0,
                    .de_idle_high = 0,
                    .pclk_active_neg = 0,
                    .pclk_idle_high = 0,
                },
            },
            .data_width = 16,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
            .in_color_format = LCD_COLOR_FMT_RGB565,
            .out_color_format = LCD_COLOR_FMT_RGB565,
#else
            .bits_per_pixel = 16,
#endif
            .num_fbs = 2,
            .bounce_buffer_size_px = DISPLAY_WIDTH * LCD_RGB_BOUNCE_BUFFER_LINES,
            .dma_burst_size = 64,
            .hsync_gpio_num = LCD_RGB_HSYNC_PIN,
            .vsync_gpio_num = LCD_RGB_VSYNC_PIN,
            .de_gpio_num = LCD_RGB_DE_PIN,
            .pclk_gpio_num = LCD_RGB_PCLK_PIN,
            .disp_gpio_num = GPIO_NUM_NC,
            .data_gpio_nums = {
                /* B[4:0] = DB1..5 */
                GPIO_NUM_11, GPIO_NUM_39, GPIO_NUM_40, GPIO_NUM_41, GPIO_NUM_42,
                /* G[5:0] = DB6..11 */
                GPIO_NUM_43, GPIO_NUM_44, GPIO_NUM_45, GPIO_NUM_46, GPIO_NUM_47, GPIO_NUM_48,
                /* R[4:0] = DB13..17: R4 rework tu GPIO54 sang GPIO49 */
                GPIO_NUM_50, GPIO_NUM_51, GPIO_NUM_52, GPIO_NUM_53, GPIO_NUM_49,
            },
            .flags = {
                .fb_in_psram = 1,
            },
        };

        ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&rgb_cfg, &panel_handle_));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle_));

        /* Gui runtime command qua SPI bit-bang sau khi RGB peripheral da chay */
        const uint8_t colmod = 0x77;
        const uint8_t madctl = 0x00;
        SendCmd(0x3A, &colmod, 1);
        SendCmd(0x36, &madctl, 1);

        display_ = new RgbLcdDisplay(nullptr, panel_handle_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                     DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    /* ==================== ST7102 TDDI Touch Driver ==================== */
    static inline SemaphoreHandle_t s_touch_sem = nullptr;
    static inline portMUX_TYPE s_touch_mux = portMUX_INITIALIZER_UNLOCKED;
    static inline struct {
        bool pressed;
        int16_t x;
        int16_t y;
    } s_touch_state = { false, 0, 0 };
    static inline volatile int64_t s_vsync_time_us = 0;
    static inline i2c_master_dev_handle_t s_touch_i2c_dev = nullptr;

    static IRAM_ATTR void VsyncGpioIsr(void *arg) {
        (void)arg;
        s_vsync_time_us = esp_timer_get_time();
    }

    static IRAM_ATTR void TouchGpioIsr(void *arg) {
        (void)arg;
        BaseType_t hp = pdFALSE;
        if (s_touch_sem) {
            xSemaphoreGiveFromISR(s_touch_sem, &hp);
        }
        if (hp) portYIELD_FROM_ISR();
    }

    static void WaitVfpGap(int need_us) {
        if (s_vsync_time_us == 0) return;
        const int64_t period_us = 12460;
        const int64_t vfp_start = period_us * 750 / 1000;
        const int64_t vfp_end   = period_us * 830 / 1000 - need_us;

        for (int k = 0; k < 3; k++) {
            int64_t now = esp_timer_get_time();
            int64_t dt = now - s_vsync_time_us;
            if (dt < 0 || dt > 100000) return;
            if (dt >= vfp_start && dt <= vfp_end) return;

            int64_t wait_us = (dt < vfp_start) ? (vfp_start - dt) : (period_us - dt + vfp_start);
            if (wait_us <= 0 || wait_us > period_us) return;

            if (wait_us > 1500) {
                vTaskDelay(pdMS_TO_TICKS(wait_us / 1000));
            } else {
                esp_rom_delay_us(wait_us);
            }
        }
    }

    static void TouchTask(void *arg) {
        auto dev = static_cast<i2c_master_dev_handle_t>(arg);
        int no_point = 0;
        TickType_t t_int_low = 0;

        while (true) {
            bool was_pressed = false;
            portENTER_CRITICAL(&s_touch_mux);
            was_pressed = s_touch_state.pressed;
            portEXIT_CRITICAL(&s_touch_mux);

            // Khi dang cham: hoi deu moi 20ms de theo doi keo/nha.
            // Khi khong cham: cho ngat canh xuong tren TP_INT (timeout 100ms de phong ket).
            bool edge = xSemaphoreTake(s_touch_sem, pdMS_TO_TICKS(was_pressed ? 20 : 100)) == pdTRUE;
            if (!edge && !was_pressed) {
                if (gpio_get_level(TOUCH_INT_PIN) != 0) {
                    t_int_low = 0;
                    continue;
                }
                TickType_t now = xTaskGetTickCount();
                if (t_int_low == 0) { t_int_low = now; continue; }
                if (now - t_int_low < pdMS_TO_TICKS(100)) continue;
                t_int_low = now;
            }

            // Canh dung cua so VFP 750-830 o/oo chu ky khung (Luat 3 / K2)
            WaitVfpGap(400);

            // 1 giao dich duy nhat 11 byte tu 0x0010 (adv_info 4 byte + point0 7 byte)
            const uint8_t reg[2] = { 0x00, 0x10 };
            uint8_t buf[11] = { 0 };
            esp_err_t err = dev ? i2c_master_transmit_receive(dev, reg, 2, buf, 11, 30) : ESP_ERR_INVALID_STATE;
            if (err != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            bool with_coord = (buf[0] & 0x08) != 0;
            bool valid = (buf[4] & 0x80) != 0;

            if (with_coord && valid) {
                int rx = ((buf[4] & 0x3F) << 8) | buf[5];
                int ry = ((buf[6] & 0x3F) << 8) | buf[7];
                if (rx < DISPLAY_WIDTH && ry < DISPLAY_HEIGHT) {
                    no_point = 0;
                    portENTER_CRITICAL(&s_touch_mux);
                    s_touch_state.x = (int16_t)rx;
                    s_touch_state.y = (int16_t)ry;
                    s_touch_state.pressed = true;
                    portEXIT_CRITICAL(&s_touch_mux);
                    continue;
                }
            }

            // Can 2 lan doc khong co diem lien tiep (>= 25-50ms) de xac nhan nha tay (chong nha gia gay giat UI)
            if (was_pressed) {
                if (++no_point >= 2) {
                    portENTER_CRITICAL(&s_touch_mux);
                    s_touch_state.pressed = false;
                    portEXIT_CRITICAL(&s_touch_mux);
                    no_point = 0;
                }
            }
        }
    }

    static void CustomTouchReadCb(lv_indev_t *indev, lv_indev_data_t *data) {
        (void)indev;
        portENTER_CRITICAL(&s_touch_mux);
        if (s_touch_state.pressed) {
            data->point.x = s_touch_state.x;
            data->point.y = s_touch_state.y;
            data->state = LV_INDEV_STATE_PRESSED;
        } else {
            data->state = LV_INDEV_STATE_RELEASED;
        }
        portEXIT_CRITICAL(&s_touch_mux);
    }

    void InitializeTouch() {
        ESP_LOGI(TAG, "Khoi tao ST7102 TDDI Touch driver doc lap (VFP Gated)");

        s_touch_sem = xSemaphoreCreateBinary();
        s_touch_mux = portMUX_INITIALIZER_UNLOCKED;
        s_touch_state = { false, 0, 0 };
        s_vsync_time_us = 0;

        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = TOUCH_I2C_ADDR;
        dev_cfg.scl_speed_hz = 400000;
        dev_cfg.scl_wait_us = 30000;

        esp_err_t err = i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &s_touch_i2c_dev);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Loi them I2C device touch: %s", esp_err_to_name(err));
            return;
        }

        gpio_config_t io_cfg = {};
        io_cfg.pin_bit_mask = (1ULL << TOUCH_INT_PIN);
        io_cfg.mode = GPIO_MODE_INPUT;
        io_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
        io_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_cfg.intr_type = GPIO_INTR_NEGEDGE;
        gpio_config(&io_cfg);

        gpio_install_isr_service(0);
        gpio_isr_handler_add(TOUCH_INT_PIN, TouchGpioIsr, NULL);

        // Lang nghe VSYNC tren LCD_RGB_VSYNC_PIN (GPIO 33)
        // LUU Y: TUYET DOI KHONG goi gpio_set_direction vi se ngat peripheral LCDCAM khoi GPIO33!
        // Chi bat input buffer tren GPIO33 de doc canh xuong VSYNC tu LCDCAM phuc vu cua so VFP touch:
        gpio_input_enable(LCD_RGB_VSYNC_PIN);
        gpio_set_intr_type(LCD_RGB_VSYNC_PIN, GPIO_INTR_NEGEDGE);
        gpio_isr_handler_add(LCD_RGB_VSYNC_PIN, VsyncGpioIsr, NULL);

        xTaskCreatePinnedToCore(TouchTask, "hcp_touch", 4096, s_touch_i2c_dev, 5, NULL, 1);

        if (lvgl_port_lock(0)) {
            lv_indev_t *indev = lv_indev_create();
            lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(indev, CustomTouchReadCb);
            lv_indev_set_display(indev, lv_display_get_default());
            lvgl_port_unlock();
            ESP_LOGI(TAG, "Dang ky non-blocking ST7102 touch indev thanh cong vao LVGL!");
        }
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeTools() {
        AppMcpBridge::GetInstance().Initialize();
    }

public:
    HcpP4Board() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeLcd();
        if (lvgl_port_lock(0)) {
            app_ui_show_splash();
            lvgl_port_unlock();
        }
        InitializeTouch();
        InitializeButtons();
        InitializeTools();
        GetBacklight()->RestoreBrightness();
    }

    virtual ~HcpP4Board() {
        delete display_;
        display_ = nullptr;

        if (panel_handle_) {
            esp_lcd_panel_del(panel_handle_);
            panel_handle_ = nullptr;
        }

        if (ldo4_handle_) {
            esp_ldo_release_channel(ldo4_handle_);
            ldo4_handle_ = nullptr;
        }
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR,
            AUDIO_CODEC_ES7210_ADDR, false, 37.5f, 0, 0.0f, 5.0f);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    virtual Backlight* GetBacklight() override {
        /* An toan: Q4 AO3401 tren GPIO27, Active-LOW.
         * Tuyet doi khong dung GPIO12 hay PWM. */
        static HcpGpioBacklight backlight(DISPLAY_BACKLIGHT_PIN);
        return &backlight;
    }

    virtual void StartNetwork() override {
        /* Dong bo trang thai ket noi tu app_network (wifi_c6 qua SDIO C6) voi Xiaozhi Application */
        app_network_register_event_cb([](app_network_event_t event, void *ctx) {
            auto self = static_cast<HcpP4Board*>(ctx);
            switch (event) {
            case APP_NETWORK_EVENT_CONNECTED: {
                char ssid[33] = {0};
                app_network_get_ssid(ssid, sizeof(ssid));
                self->OnNetworkEvent(NetworkEvent::Connected, ssid);
                break;
            }
            case APP_NETWORK_EVENT_CONNECTING: {
                char ssid[33] = {0};
                app_network_get_ssid(ssid, sizeof(ssid));
                self->OnNetworkEvent(NetworkEvent::Connecting, ssid);
                break;
            }
            case APP_NETWORK_EVENT_DISCONNECTED:
            case APP_NETWORK_EVENT_FAILED:
                self->OnNetworkEvent(NetworkEvent::Disconnected);
                break;
            default:
                break;
            }
        }, this);

        if (app_network_is_connected()) {
            char ssid[33] = {0};
            app_network_get_ssid(ssid, sizeof(ssid));
            OnNetworkEvent(NetworkEvent::Connected, ssid);
        } else {
            OnNetworkEvent(NetworkEvent::Connecting, "");
        }
    }
};

DECLARE_BOARD(HcpP4Board);
