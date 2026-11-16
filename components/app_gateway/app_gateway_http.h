#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_GW_ACCESS_TOKEN_SIZE 2048
#define APP_GW_MAX_PAYLOAD_SIZE (64 * 1024)
#define APP_GW_ERR_UNAUTHORIZED (0x7a01)

typedef struct {
    char broker[128];
    char url_tls[192];
    char url_wss[192];
    char username[128];
    char topic_down[128];
    char topic_up[128];
    uint32_t command_ttl_max_ms;
    uint32_t token_ttl_s;
} app_gw_mqtt_config_t;

/**
 * @brief Fetch OAuth token from NobleX REST (/v1/hc/oauth/token).
 */
esp_err_t app_gateway_http_fetch_token(const char *base_url,
                                       const char *client_id,
                                       const char *secret,
                                       char *out_token,
                                       size_t max_len,
                                       uint32_t *out_expires_in);

/**
 * @brief Fetch dynamic MQTT config from NobleX REST (/v1/hc/config).
 */
esp_err_t app_gateway_http_fetch_config(const char *base_url,
                                        const char *access_token,
                                        app_gw_mqtt_config_t *out_cfg);

/**
 * @brief Create dynamic pairing session from NobleX REST (/v1/hc/pairings).
 */
esp_err_t app_gateway_http_create_pairing(const char *base_url,
                                          const char *access_token,
                                          char *out_token,
                                          size_t max_token_len,
                                          char *out_pair_url,
                                          size_t max_url_len,
                                          int *out_remaining_sec);

/* JSON REST cap phat tren heap; caller phai free(*out_json) sau khi dung. */
esp_err_t app_gateway_http_fetch_snapshot(const char *base_url, const char *access_token,
                                          char **out_json, size_t *out_len);

#ifdef __cplusplus
}
#endif
