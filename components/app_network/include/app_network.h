#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "wifi_c6.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_NETWORK_EVENT_DISCONNECTED = 0,
    APP_NETWORK_EVENT_CONNECTING,
    APP_NETWORK_EVENT_CONNECTED,        /* Got IP */
    APP_NETWORK_EVENT_FAILED,
    APP_NETWORK_EVENT_TIME_SYNCED,      /* SNTP time synchronized */
} app_network_event_t;

typedef void (*app_network_event_cb_t)(app_network_event_t event, void *ctx);

/**
 * @brief Initialize network subsystem using ESP32-C6 (esp-hosted SDIO).
 *        Starts background monitor task to dispatch state change events.
 */
esp_err_t app_network_init(void);
bool app_network_is_initialized(void);

/**
 * @brief Register a callback for network state events.
 */
esp_err_t app_network_register_event_cb(app_network_event_cb_t cb, void *ctx);

/**
 * @brief Get current Wi-Fi state.
 */
wifi_c6_state_t app_network_get_state(void);

/**
 * @brief Check if Wi-Fi is connected and has valid IP address.
 */
bool app_network_is_connected(void);

/**
 * @brief Get IP address string if connected.
 */
bool app_network_get_ip(char *buf, size_t len);

/**
 * @brief Get Wi-Fi RSSI (dBm).
 */
int app_network_get_rssi(void);

/**
 * @brief Get saved/current SSID.
 */
bool app_network_get_ssid(char *buf, size_t len);

/**
 * @brief Check whether SNTP time has been synchronized.
 */
bool app_network_is_time_synced(void);
void app_network_retry_time_sync(void);

/**
 * @brief Connect to an AP with SSID and password. Saves to NVS on success.
 */
esp_err_t app_network_connect(const char *ssid, const char *pass);

/**
 * @brief Forget saved Wi-Fi credentials and disconnect.
 */
void app_network_forget(void);

/**
 * @brief Scan for available APs (blocking ~2-4s).
 */
esp_err_t app_network_scan(wifi_c6_ap_t *list, int max, int *n);

/**
 * @brief Get esp-hosted firmware version running on ESP32-C6.
 */
const char *app_network_get_cp_version(void);

#ifdef __cplusplus
}
#endif
