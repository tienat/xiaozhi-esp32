#include "app_config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "app_spiffs.h"

static const char *TAG = "app_config";

#ifdef HCP_HAS_LOCAL_DEVICE_CONFIG
#include "hcp_device_local.h"
#endif

#if defined(HCP_LOCAL_CLIENT_ID) != defined(HCP_LOCAL_CLIENT_SECRET)
#error "Local NobleX configuration requires both client ID and secret"
#endif

static app_config_t s_config;
static bool s_initialized = false;

static void apply_local_identity(void)
{
#ifdef HCP_LOCAL_CLIENT_ID
    _Static_assert(sizeof(HCP_LOCAL_CLIENT_ID) > 1 &&
                   sizeof(HCP_LOCAL_CLIENT_ID) <= sizeof(s_config.client_id),
                   "Local client ID has invalid length");
    _Static_assert(sizeof(HCP_LOCAL_CLIENT_SECRET) > 1 &&
                   sizeof(HCP_LOCAL_CLIENT_SECRET) <= sizeof(s_config.client_secret),
                   "Local client secret has invalid length");
    /* Luon nap ca cap de khong tron danh tinh local voi secret cu. */
    memset(s_config.client_id, 0, sizeof(s_config.client_id));
    memset(s_config.client_secret, 0, sizeof(s_config.client_secret));
    memcpy(s_config.client_id, HCP_LOCAL_CLIENT_ID, sizeof(HCP_LOCAL_CLIENT_ID));
    memcpy(s_config.client_secret, HCP_LOCAL_CLIENT_SECRET, sizeof(HCP_LOCAL_CLIENT_SECRET));
#endif
}

static void set_defaults(void)
{
    memset(&s_config, 0, sizeof(s_config));
    strncpy(s_config.home_id, APP_CONFIG_DEFAULT_HOME_ID, sizeof(s_config.home_id) - 1);
    strncpy(s_config.api_base_url, APP_CONFIG_DEFAULT_API_URL, sizeof(s_config.api_base_url) - 1);

    uint8_t mac[6] = {0};
    if (esp_efuse_mac_get_default(mac) == ESP_OK) {
        snprintf(s_config.client_id, sizeof(s_config.client_id), "dev.HC-%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        strncpy(s_config.client_id, "dev.HC-DEMO01", sizeof(s_config.client_id) - 1);
    }

    apply_local_identity();

    s_config.volume = APP_CONFIG_DEFAULT_VOLUME;
    s_config.brightness = APP_CONFIG_DEFAULT_BRIGHT;
    s_config.auto_sleep_s = 60;
}

static bool load_from_spiffs(void)
{
    if (!app_spiffs_file_exists(APP_CONFIG_FILE_PATH)) {
        return false;
    }

    size_t len = 0;
    char *json_str = app_spiffs_read_file(APP_CONFIG_FILE_PATH, &len);
    if (!json_str || len == 0) {
        if (json_str) free(json_str);
        return false;
    }

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON from %s", APP_CONFIG_FILE_PATH);
        return false;
    }

    cJSON *item = cJSON_GetObjectItem(root, "home_id");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(s_config.home_id, item->valuestring, sizeof(s_config.home_id) - 1);
    }

    item = cJSON_GetObjectItem(root, "api_base_url");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(s_config.api_base_url, item->valuestring, sizeof(s_config.api_base_url) - 1);
    }

    item = cJSON_GetObjectItem(root, "auth_token");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(s_config.auth_token, item->valuestring, sizeof(s_config.auth_token) - 1);
    }

    item = cJSON_GetObjectItem(root, "client_id");
    if (cJSON_IsString(item) && item->valuestring && strlen(item->valuestring) > 0) {
        snprintf(s_config.client_id, sizeof(s_config.client_id), "%s", item->valuestring);
    }

    item = cJSON_GetObjectItem(root, "client_secret");
    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(s_config.client_secret, sizeof(s_config.client_secret), "%s", item->valuestring);
    }

    item = cJSON_GetObjectItem(root, "volume");
    if (cJSON_IsNumber(item)) {
        s_config.volume = (uint8_t)item->valueint;
    }

    item = cJSON_GetObjectItem(root, "brightness");
    if (cJSON_IsNumber(item)) {
        s_config.brightness = (uint8_t)item->valueint;
    }

    item = cJSON_GetObjectItem(root, "auto_sleep_s");
    if (cJSON_IsNumber(item)) {
        s_config.auto_sleep_s = (uint16_t)item->valueint;
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded config from %s: home_id='%s', api_url='%s'",
             APP_CONFIG_FILE_PATH, s_config.home_id, s_config.api_base_url);
    return true;
}

static bool load_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(APP_CONFIG_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t len = sizeof(s_config.home_id);
    nvs_get_str(handle, "home_id", s_config.home_id, &len);

    len = sizeof(s_config.api_base_url);
    nvs_get_str(handle, "api_url", s_config.api_base_url, &len);

    len = sizeof(s_config.auth_token);
    nvs_get_str(handle, "token", s_config.auth_token, &len);

    char cid[sizeof(s_config.client_id)] = {0};
    len = sizeof(cid);
    if (nvs_get_str(handle, "client_id", cid, &len) == ESP_OK && strlen(cid) > 0) {
        snprintf(s_config.client_id, sizeof(s_config.client_id), "%s", cid);
    }

    len = sizeof(s_config.client_secret);
    nvs_get_str(handle, "secret", s_config.client_secret, &len);

    uint8_t u8_val = 0;
    if (nvs_get_u8(handle, "volume", &u8_val) == ESP_OK) {
        s_config.volume = u8_val;
    }
    if (nvs_get_u8(handle, "brightness", &u8_val) == ESP_OK) {
        s_config.brightness = u8_val;
    }
    uint16_t u16_val = 0;
    if (nvs_get_u16(handle, "sleep_s", &u16_val) == ESP_OK) {
        s_config.auto_sleep_s = u16_val;
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Loaded config from NVS: home_id='%s'", s_config.home_id);
    return true;
}

esp_err_t app_config_init(void)
{
    set_defaults();

    /* Try SPIFFS first, then NVS */
    if (!load_from_spiffs()) {
        if (!load_from_nvs()) {
            ESP_LOGI(TAG, "No config found in SPIFFS or NVS, using default values");
        }
    }

    apply_local_identity();
    if (s_config.client_secret[0] == '\0') {
        ESP_LOGW(TAG, "NobleX device secret is not provisioned");
    }

    ESP_LOGI(TAG, "Config initialized: client_id='%s', secret_len=%zu, api_url='%s'",
             s_config.client_id, strlen(s_config.client_secret), s_config.api_base_url);

    s_initialized = true;
    return ESP_OK;
}

const app_config_t *app_config_get(void)
{
    if (!s_initialized) {
        app_config_init();
    }
    return &s_config;
}

const char *app_config_get_home_id(void)
{
    return app_config_get()->home_id;
}

esp_err_t app_config_set_home_id(const char *home_id)
{
    if (!home_id) return ESP_ERR_INVALID_ARG;
    strncpy(s_config.home_id, home_id, sizeof(s_config.home_id) - 1);
    s_config.home_id[sizeof(s_config.home_id) - 1] = '\0';
    return ESP_OK;
}

const char *app_config_get_api_url(void)
{
    return app_config_get()->api_base_url;
}

esp_err_t app_config_set_api_url(const char *url)
{
    if (!url) return ESP_ERR_INVALID_ARG;
    strncpy(s_config.api_base_url, url, sizeof(s_config.api_base_url) - 1);
    s_config.api_base_url[sizeof(s_config.api_base_url) - 1] = '\0';
    return ESP_OK;
}

const char *app_config_get_token(void)
{
    return app_config_get()->auth_token;
}

esp_err_t app_config_set_token(const char *token)
{
    if (!token) return ESP_ERR_INVALID_ARG;
    strncpy(s_config.auth_token, token, sizeof(s_config.auth_token) - 1);
    s_config.auth_token[sizeof(s_config.auth_token) - 1] = '\0';
    return ESP_OK;
}

const char *app_config_get_client_id(void)
{
    return app_config_get()->client_id;
}

esp_err_t app_config_set_client_id(const char *client_id)
{
    if (!client_id) return ESP_ERR_INVALID_ARG;
    strncpy(s_config.client_id, client_id, sizeof(s_config.client_id) - 1);
    s_config.client_id[sizeof(s_config.client_id) - 1] = '\0';
    return ESP_OK;
}

const char *app_config_get_client_secret(void)
{
    return app_config_get()->client_secret;
}

esp_err_t app_config_set_client_secret(const char *client_secret)
{
    if (!client_secret) return ESP_ERR_INVALID_ARG;
    strncpy(s_config.client_secret, client_secret, sizeof(s_config.client_secret) - 1);
    s_config.client_secret[sizeof(s_config.client_secret) - 1] = '\0';
    return ESP_OK;
}

uint8_t app_config_get_volume(void)
{
    return app_config_get()->volume;
}

esp_err_t app_config_set_volume(uint8_t volume)
{
    if (volume > 100) volume = 100;
    s_config.volume = volume;
    return ESP_OK;
}

uint8_t app_config_get_brightness(void)
{
    return app_config_get()->brightness;
}

esp_err_t app_config_set_brightness(uint8_t brightness)
{
    if (brightness > 100) brightness = 100;
    s_config.brightness = brightness;
    return ESP_OK;
}

uint16_t app_config_get_auto_sleep_s(void)
{
    return app_config_get()->auto_sleep_s;
}

esp_err_t app_config_set_auto_sleep_s(uint16_t seconds)
{
    s_config.auto_sleep_s = seconds;
    return ESP_OK;
}

esp_err_t app_config_save(void)
{
    /* 1. Save to NVS */
    nvs_handle_t handle;
    esp_err_t err = nvs_open(APP_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_set_str(handle, "home_id", s_config.home_id);
        nvs_set_str(handle, "api_url", s_config.api_base_url);
        nvs_set_str(handle, "token", s_config.auth_token);
        nvs_set_str(handle, "client_id", s_config.client_id);
        nvs_set_str(handle, "secret", s_config.client_secret);
        nvs_set_u8(handle, "volume", s_config.volume);
        nvs_set_u8(handle, "brightness", s_config.brightness);
        nvs_set_u16(handle, "sleep_s", s_config.auto_sleep_s);
        nvs_commit(handle);
        nvs_close(handle);
    }

    /* 2. Save to SPIFFS JSON if mounted */
    if (app_spiffs_is_mounted()) {
        cJSON *root = cJSON_CreateObject();
        if (root) {
            cJSON_AddStringToObject(root, "home_id", s_config.home_id);
            cJSON_AddStringToObject(root, "api_base_url", s_config.api_base_url);
            cJSON_AddStringToObject(root, "auth_token", s_config.auth_token);
            cJSON_AddStringToObject(root, "client_id", s_config.client_id);
            cJSON_AddStringToObject(root, "client_secret", s_config.client_secret);
            cJSON_AddNumberToObject(root, "volume", s_config.volume);
            cJSON_AddNumberToObject(root, "brightness", s_config.brightness);
            cJSON_AddNumberToObject(root, "auto_sleep_s", s_config.auto_sleep_s);

            char *json_str = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);

            if (json_str) {
                app_spiffs_write_file(APP_CONFIG_FILE_PATH, json_str, strlen(json_str));
                free(json_str);
            }
        }
    }

    ESP_LOGI(TAG, "Config saved successfully");
    return ESP_OK;
}

esp_err_t app_config_reset_defaults(void)
{
    set_defaults();
    return app_config_save();
}
