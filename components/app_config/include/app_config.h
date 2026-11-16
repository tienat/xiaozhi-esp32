#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_CONFIG_FILE_PATH     "/spiffs/config.json"
#define APP_CONFIG_NVS_NAMESPACE "app_config"

#define APP_CONFIG_DEFAULT_API_URL  "https://api-smart.noblex.vn"
#define APP_CONFIG_DEFAULT_HOME_ID  "home-demo"
#define APP_CONFIG_DEFAULT_VOLUME   70
#define APP_CONFIG_DEFAULT_BRIGHT   100

typedef struct {
    char home_id[64];
    char api_base_url[128];
    char auth_token[512];
    char client_id[64];
    char client_secret[128];
    uint8_t volume;         /* 0 - 100 */
    uint8_t brightness;     /* 0 - 100 */
    uint16_t auto_sleep_s;  /* Screen standby seconds, 0 = never */
} app_config_t;

/**
 * @brief Initialize app_config subsystem. NVS and SPIFFS must be initialized.
 *        Attempts to read from SPIFFS config.json first; falls back to NVS;
 *        falls back to default values if not configured.
 */
esp_err_t app_config_init(void);

/**
 * @brief Get read-only pointer to global configuration.
 */
const app_config_t *app_config_get(void);

const char *app_config_get_home_id(void);
esp_err_t app_config_set_home_id(const char *home_id);

const char *app_config_get_api_url(void);
esp_err_t app_config_set_api_url(const char *url);

const char *app_config_get_token(void);
esp_err_t app_config_set_token(const char *token);

const char *app_config_get_client_id(void);
esp_err_t app_config_set_client_id(const char *client_id);

const char *app_config_get_client_secret(void);
esp_err_t app_config_set_client_secret(const char *client_secret);

uint8_t app_config_get_volume(void);
esp_err_t app_config_set_volume(uint8_t volume);

uint8_t app_config_get_brightness(void);
esp_err_t app_config_set_brightness(uint8_t brightness);

uint16_t app_config_get_auto_sleep_s(void);
esp_err_t app_config_set_auto_sleep_s(uint16_t seconds);

/**
 * @brief Persist current configuration into both NVS and /spiffs/config.json.
 */
esp_err_t app_config_save(void);

/**
 * @brief Reset all configurations to default values and save.
 */
esp_err_t app_config_reset_defaults(void);

#ifdef __cplusplus
}
#endif
