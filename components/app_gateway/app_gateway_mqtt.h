#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "app_gateway_http.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*app_gw_mqtt_msg_cb_t)(const char *data, size_t len, void *ctx);
typedef void (*app_gw_mqtt_status_cb_t)(bool connected, void *ctx);

/**
 * @brief Initialize MQTT client subsystem.
 */
esp_err_t app_gateway_mqtt_init(const app_gw_mqtt_config_t *cfg,
                                const char *client_id,
                                const char *access_token,
                                app_gw_mqtt_msg_cb_t msg_cb,
                                app_gw_mqtt_status_cb_t status_cb,
                                void *ctx);

/**
 * @brief Start MQTT client connection.
 */
esp_err_t app_gateway_mqtt_start(void);

/**
 * @brief Stop and disconnect MQTT client.
 */
esp_err_t app_gateway_mqtt_stop(void);

/**
 * @brief Check if MQTT is connected and topic_down subscription has been acknowledged.
 */
bool app_gateway_mqtt_is_connected(void);

/**
 * @brief Publish 'sync' envelope to topic_up.
 */
esp_err_t app_gateway_mqtt_publish_sync(void);

/**
 * @brief Publish 'pairing.create' envelope to topic_up.
 */
esp_err_t app_gateway_mqtt_publish_pairing_create(void);

/**
 * @brief Publish 'unlink' envelope to topic_up.
 */
esp_err_t app_gateway_mqtt_publish_unlink(void);

/**
 * @brief Publish 'device.command' envelope to topic_up.
 */
esp_err_t app_gateway_mqtt_publish_device_command(const char *device_id,
                                                  const char *endpoint_id,
                                                  const char *trait,
                                                  const char *value_json,
                                                  uint32_t ttl_ms);

/**
 * @brief Publish 'scene.run' envelope to topic_up.
 */
esp_err_t app_gateway_mqtt_publish_scene_run(const char *scene_id, uint32_t ttl_ms);

#ifdef __cplusplus
}
#endif
