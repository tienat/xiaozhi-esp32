#ifndef _HCP_P4_BOARD_CONFIG_H_
#define _HCP_P4_BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2c_types.h>

/* ==================== Audio (ES8311 DAC + ES7210 ADC) ==================== */
#define AUDIO_INPUT_SAMPLE_RATE     24000
#define AUDIO_OUTPUT_SAMPLE_RATE    24000

#define AUDIO_I2S_GPIO_MCLK         GPIO_NUM_20
#define AUDIO_I2S_GPIO_BCLK         GPIO_NUM_21
#define AUDIO_I2S_GPIO_WS           GPIO_NUM_22
#define AUDIO_I2S_GPIO_DOUT         GPIO_NUM_0   /* -> ES8311 DSDIN */
#define AUDIO_I2S_GPIO_DIN          GPIO_NUM_23  /* <- ES7210 SDOUT1 */

#define AUDIO_CODEC_PA_PIN          GPIO_NUM_31  /* NS4150, Active HIGH */
#define AUDIO_CODEC_I2C_PORT        I2C_NUM_0
#define AUDIO_CODEC_I2C_SDA_PIN     GPIO_NUM_7
#define AUDIO_CODEC_I2C_SCL_PIN     GPIO_NUM_8

/* esp_codec_dev expects 8-bit I2C addresses (shifts >> 1 to derive 7-bit addr).
 * ES8311 7-bit: 0x18 -> 8-bit: 0x30 (ES8311_CODEC_DEFAULT_ADDR)
 * ES7210 7-bit: 0x40 -> 8-bit: 0x80 (ES7210_CODEC_DEFAULT_ADDR) */
#define AUDIO_CODEC_ES8311_ADDR     0x30
#define AUDIO_CODEC_ES7210_ADDR     0x80

/* ==================== LCD ST7102 480x480 RGB ==================== */
#define DISPLAY_WIDTH               480
#define DISPLAY_HEIGHT              480
#define DISPLAY_OFFSET_X            0
#define DISPLAY_OFFSET_Y            0
#define DISPLAY_MIRROR_X            false
#define DISPLAY_MIRROR_Y            false
#define DISPLAY_SWAP_XY             false

/* Backlight: Q4 AO3401 (P-channel), Active-LOW.
 * CANH BAO: GPIO12 la chan FB cua IC boost AP3032: TUYET DOI KHONG CAU HINH OUTPUT/PWM. */
#define DISPLAY_BACKLIGHT_PIN       GPIO_NUM_27
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT true

/* ST7102 SPI 3-wire init bit-bang */
#define LCD_SPI_CS_PIN              GPIO_NUM_4
#define LCD_SPI_SCK_PIN             GPIO_NUM_5
#define LCD_SPI_SDA_PIN             GPIO_NUM_6
#define LCD_RST_PIN                 GPIO_NUM_13
#define LCD_DB0_NC_PIN              GPIO_NUM_2

/* ST7102 RGB Control */
#define LCD_RGB_DE_PIN              GPIO_NUM_1
#define LCD_RGB_PCLK_PIN            GPIO_NUM_3
#define LCD_RGB_HSYNC_PIN           GPIO_NUM_34
#define LCD_RGB_VSYNC_PIN           GPIO_NUM_33

/* ST7102 RGB Timings (32MHz ~80Hz) */
#define LCD_PCLK_HZ                 32000000
#define LCD_HPW                     40
#define LCD_HBP                     40
#define LCD_HFP                     30
#define LCD_VPW                     5
#define LCD_VBP                     11
#define LCD_VFP                     180
#define LCD_RGB_BOUNCE_BUFFER_LINES 0

/* LCD Data Pins:
 * B[4:0] = DB1..5:  GPIO 11, 39, 40, 41, 42
 * G[5:0] = DB6..11: GPIO 43, 44, 45, 46, 47, 48
 * R[4:0] = DB13..17: GPIO 50, 51, 52, 53, 49 (R4 rework tu GPIO54)
 */

/* ==================== Touch ST7102 TDDI ==================== */
#define TOUCH_I2C_ADDR              0x55
#define TOUCH_INT_PIN               GPIO_NUM_10
#define TOUCH_RST_PIN               GPIO_NUM_9

/* ==================== Buttons & Status ==================== */
#define BOOT_BUTTON_GPIO            GPIO_NUM_35
#define STATUS_LED_GPIO             GPIO_NUM_32

#endif  // _HCP_P4_BOARD_CONFIG_H_
