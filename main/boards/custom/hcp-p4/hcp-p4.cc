#include "application.h"
#include "audio/codecs/box_audio_codec.h"
#include "button.h"
#include "config.h"
#include "display/display.h"
#include "display/lcd_display.h"
#include "lvgl_theme.h"
#include "wifi_board.h"

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

public:
    HcpP4Board() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeLcd();
        InitializeButtons();
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
};

DECLARE_BOARD(HcpP4Board);
