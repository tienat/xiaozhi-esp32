#include "app_gateway.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "app_config.h"
#include "app_network.h"
#include "app_gateway_http.h"
#include "app_gateway_mqtt.h"
#include "app_gateway_parser.h"
#include "esp_heap_caps.h"

static const char *TAG = "app_gateway";

static app_gw_state_t s_gw_state = GW_STATE_DISCONNECTED;
static app_snapshot_t *s_snapshot = NULL;
static app_pairing_session_t s_pairing;
static app_gw_mqtt_config_t s_mqtt_cfg;
static char s_access_token[APP_GW_ACCESS_TOKEN_SIZE] = {0};
static int64_t s_token_acquired_time_s = 0;
static uint32_t s_token_expires_in_s = 600;

static SemaphoreHandle_t s_mutex = NULL;
static TaskHandle_t s_task_handle = NULL;
#define MAX_UPDATE_CBS 4
static app_gateway_update_cb_t s_update_cbs[MAX_UPDATE_CBS];
static int s_num_update_cbs = 0;

static bool s_net_connected = false;
static bool s_needs_sync = false;
static bool s_needs_pairing_create = false;
static bool s_needs_unlink = false;
static bool s_pairing_in_flight = false;
static bool s_pairing_wait_for_user = false;
static int64_t s_pairing_requested_us = 0;
static int64_t s_pairing_deadline_us = 0;
#define PAIRING_REQUEST_TIMEOUT_US (15LL * 1000000)

typedef enum { SNAP_IDLE, SNAP_WAIT_PUSH, SNAP_NEED_SYNC, SNAP_WAIT_SYNC, SNAP_REST_DONE } snapshot_recovery_t;
static snapshot_recovery_t s_snapshot_recovery = SNAP_IDLE;
static bool s_cloud_linked = false;
static bool s_snapshot_received_for_link = false;
static char s_snapshot_home[64];
static uint32_t s_snapshot_epoch = 0;
static uint32_t s_mqtt_snapshot_serial = 0;
static int64_t s_snapshot_check_us = 0;
#define SNAPSHOT_WAIT_US (5LL * 1000000)

static bool snapshot_has_devices_or_scenes(void)
{
    return s_snapshot && (s_snapshot->device_count > 0 || s_snapshot->scene_count > 0);
}

static void reset_snapshot_recovery(void)
{
    s_cloud_linked = false;
    s_snapshot_received_for_link = false;
    s_snapshot_recovery = SNAP_IDLE;
    s_snapshot_home[0] = '\0';
    s_snapshot_epoch++;
}

/* Rate Limiting */
#define RATE_LIMIT_SYNC_MAX_PER_MIN 6
static int64_t s_sync_calls[RATE_LIMIT_SYNC_MAX_PER_MIN] = {0};
static size_t s_sync_call_idx = 0;
static size_t s_sync_call_count = 0;

static bool rate_limit_check_sync(void)
{
    int64_t now_s = esp_timer_get_time() / 1000000;
    int count = 0;
    for (size_t i = 0; i < s_sync_call_count; i++) {
        if (now_s - s_sync_calls[i] < 60) {
            count++;
        }
    }
    if (count >= RATE_LIMIT_SYNC_MAX_PER_MIN) {
        ESP_LOGW(TAG, "Rate limit reached for sync/pairing.create (max 6/min)");
        return false;
    }
    s_sync_calls[s_sync_call_idx] = now_s;
    s_sync_call_idx = (s_sync_call_idx + 1) % RATE_LIMIT_SYNC_MAX_PER_MIN;
    if (s_sync_call_count < RATE_LIMIT_SYNC_MAX_PER_MIN) s_sync_call_count++;
    return true;
}

#define RATE_LIMIT_CMD_MAX_PER_SEC 10
static int64_t s_cmd_calls[RATE_LIMIT_CMD_MAX_PER_SEC] = {0};
static size_t s_cmd_call_idx = 0;

static bool rate_limit_check_cmd(void)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    int count = 0;
    for (size_t i = 0; i < RATE_LIMIT_CMD_MAX_PER_SEC; i++) {
        if (now_ms - s_cmd_calls[i] < 1000) {
            count++;
        }
    }
    if (count >= RATE_LIMIT_CMD_MAX_PER_SEC) {
        ESP_LOGW(TAG, "Rate limit reached for device.command (max 10/sec)");
        return false;
    }
    s_cmd_calls[s_cmd_call_idx] = now_ms;
    s_cmd_call_idx = (s_cmd_call_idx + 1) % RATE_LIMIT_CMD_MAX_PER_SEC;
    return true;
}

static void notify_update(void)
{
    for (int i = 0; i < s_num_update_cbs; i++) {
        if (s_update_cbs[i]) {
            s_update_cbs[i]();
        }
    }
}

static void on_mqtt_status(bool connected, void *ctx)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (connected) {
        ESP_LOGI(TAG, "MQTT subscription ready, scheduling sync");
        s_needs_sync = true;
    } else {
        ESP_LOGW(TAG, "MQTT disconnected callback received");
        if (s_gw_state != GW_STATE_DISCONNECTED && s_gw_state != GW_STATE_ERROR) {
            s_gw_state = GW_STATE_MQTT_CONNECTING;
        }
    }
    xSemaphoreGive(s_mutex);
}

static void on_mqtt_msg(const char *data, size_t len, void *ctx)
{
    ESP_LOGI(TAG, "<-- MQTT RECV (len=%zu):\n%.*s", len, (int)len, data);
    app_gw_parsed_msg_t parsed;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = app_gateway_parser_parse_envelope(data, len, &parsed, s_snapshot);
    if (err == ESP_OK) {
        switch (parsed.type) {
        case PARSER_EVT_LINK_STATE:
            if (parsed.data.link_state.linked) {
                if (!s_cloud_linked || strcmp(s_snapshot_home, parsed.data.link_state.home_id) != 0) {
                    s_snapshot_epoch++;
                    s_cloud_linked = true;
                    s_snapshot_received_for_link = false;
                    s_snapshot_recovery = SNAP_WAIT_PUSH;
                    s_snapshot_check_us = esp_timer_get_time() + SNAPSHOT_WAIT_US;
                    snprintf(s_snapshot_home, sizeof(s_snapshot_home), "%s", parsed.data.link_state.home_id);
                    if (s_snapshot && s_snapshot->home_id[0] && strcmp(s_snapshot->home_id, s_snapshot_home) != 0) {
                        memset(s_snapshot, 0, sizeof(*s_snapshot));
                        app_gateway_parser_wipe_snapshot_file();
                    }
                }
                s_gw_state = GW_STATE_LINKED_RUNNING;
                s_pairing.active = false;
                s_pairing.retry_available = false;
                s_pairing_in_flight = false;
                s_needs_pairing_create = false;
                s_pairing_wait_for_user = false;
                if (s_snapshot) {
                    snprintf(s_snapshot->home_id, sizeof(s_snapshot->home_id), "%s", parsed.data.link_state.home_id);
                    snprintf(s_snapshot->home_name, sizeof(s_snapshot->home_name), "%s", parsed.data.link_state.home_name);
                    ESP_LOGI(TAG, "Linked to home: %s (%s)", s_snapshot->home_name, s_snapshot->home_id);
                }
            } else {
                if (s_cloud_linked) reset_snapshot_recovery();
                s_gw_state = GW_STATE_UNLINKED_PAIRING;
                if (!s_pairing.active && !s_pairing_in_flight && !s_pairing_wait_for_user) {
                    ESP_LOGI(TAG, "Not linked, requesting initial pairing session");
                    s_needs_pairing_create = true;
                } else {
                    ESP_LOGI(TAG, "Not linked; keeping current pairing lifecycle");
                }
            }
            notify_update();
            break;

        case PARSER_EVT_PAIRING_SESSION:
            if (s_gw_state == GW_STATE_LINKED_RUNNING ||
                (!s_pairing_in_flight && (s_pairing.active || s_pairing_wait_for_user))) {
                ESP_LOGI(TAG, "Ignoring unsolicited/repeated pairing session");
                break;
            }
            if (!parsed.data.pairing_session.token[0] || !parsed.data.pairing_session.pair_url[0] ||
                parsed.data.pairing_session.remaining_sec <= 0) {
                s_pairing_in_flight = false;
                s_needs_pairing_create = false;
                s_pairing_wait_for_user = true;
                s_pairing.retry_available = true;
                ESP_LOGW(TAG, "Invalid or expired MQTT pairing session; waiting for user");
                notify_update();
                break;
            }
            s_pairing_in_flight = false;
            s_needs_pairing_create = false;
            s_pairing_wait_for_user = true;
            s_gw_state = GW_STATE_UNLINKED_PAIRING;
            snprintf(s_pairing.token, sizeof(s_pairing.token), "%s", parsed.data.pairing_session.token);
            snprintf(s_pairing.pair_url, sizeof(s_pairing.pair_url), "%s", parsed.data.pairing_session.pair_url);
            s_pairing.remaining_sec = parsed.data.pairing_session.remaining_sec;
            s_pairing_deadline_us = esp_timer_get_time() + (int64_t)s_pairing.remaining_sec * 1000000;
            s_pairing.active = true;
            s_pairing.denied = false;
            s_pairing.retry_available = false;
            ESP_LOGI(TAG, "Active pairing session: expires_in=%d s", s_pairing.remaining_sec);
            notify_update();
            break;

        case PARSER_EVT_PAIRING_DENIED:
            if (s_gw_state == GW_STATE_LINKED_RUNNING) break;
            s_gw_state = GW_STATE_UNLINKED_PAIRING;
            s_pairing_in_flight = false;
            s_needs_pairing_create = false;
            s_pairing_wait_for_user = true;
            s_pairing.active = false;
            s_pairing.denied = true;
            s_pairing.retry_available = true;
            ESP_LOGW(TAG, "Pairing request was denied on app");
            notify_update();
            break;

        case PARSER_EVT_SNAPSHOT:
            s_gw_state = GW_STATE_LINKED_RUNNING;
            s_pairing.active = false;
            s_pairing.retry_available = false;
            s_pairing_in_flight = false;
            s_needs_pairing_create = false;
            s_pairing_wait_for_user = false;
            s_mqtt_snapshot_serial++;
            if (s_cloud_linked && s_snapshot && strcmp(s_snapshot->home_id, s_snapshot_home) == 0) {
                s_snapshot_received_for_link = true;
                if (snapshot_has_devices_or_scenes()) s_snapshot_recovery = SNAP_IDLE;
            }
            ESP_LOGI(TAG, "Snapshot applied source=MQTT id=%s bytes=%zu rooms=%u devices=%u scenes=%u",
                     parsed.msg_id, len, s_snapshot ? s_snapshot->room_count : 0,
                     s_snapshot ? s_snapshot->device_count : 0, s_snapshot ? s_snapshot->scene_count : 0);
            notify_update();
            break;

        case PARSER_EVT_DEVICE_STATE:
            if (parsed.data.device_state.needs_sync) {
                s_needs_sync = true;
            }
            notify_update();
            break;

        case PARSER_EVT_COMMAND_RESULT:
            ESP_LOGI(TAG, "Command result: id=%s, status=%s, err=%s",
                     parsed.data.command_result.command_id,
                     parsed.data.command_result.status,
                     parsed.data.command_result.err_code);
            break;

        case PARSER_EVT_GRANT_REVOKED:
            reset_snapshot_recovery();
            ESP_LOGW(TAG, "Grant revoked: reason=%s. Cleared data & returning to QR",
                     parsed.data.grant_revoked.reason);
            s_gw_state = GW_STATE_UNLINKED_PAIRING;
            if (s_snapshot) {
                memset(s_snapshot, 0, sizeof(app_snapshot_t));
            }
            memset(&s_pairing, 0, sizeof(s_pairing));
            s_pairing_in_flight = false;
            s_pairing_wait_for_user = false;
            s_needs_pairing_create = true;
            notify_update();
            break;

        default:
            break;
        }
    } else if (parsed.type == PARSER_EVT_SNAPSHOT) {
        ESP_LOGW(TAG, "Snapshot rejected source=MQTT id=%s bytes=%zu err=%s; model retained",
                 parsed.msg_id, len, esp_err_to_name(err));
    }
    xSemaphoreGive(s_mutex);
}

static void on_network_event(app_network_event_t event, void *ctx)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (event == APP_NETWORK_EVENT_CONNECTED || event == APP_NETWORK_EVENT_TIME_SYNCED) {
        ESP_LOGI(TAG, "Network/time event; Cloud waits for SNTP");
        s_net_connected = true;
    } else if (event == APP_NETWORK_EVENT_DISCONNECTED || event == APP_NETWORK_EVENT_FAILED) {
        ESP_LOGW(TAG, "Network disconnected");
        s_net_connected = false;
        reset_snapshot_recovery();
        s_gw_state = GW_STATE_DISCONNECTED;
        app_gateway_mqtt_stop();
    }
    xSemaphoreGive(s_mutex);
}

/* Goi khi dang giu s_mutex; chi mot transport cho moi lan xin phien. */
static esp_err_t request_pairing_locked(bool force_rest)
{
    if (!s_needs_pairing_create || s_pairing_in_flight) return ESP_OK;
    if (!rate_limit_check_sync()) return ESP_OK;

    s_needs_pairing_create = false;
    s_pairing_in_flight = true;
    s_pairing_wait_for_user = true;
    s_pairing.retry_available = false;
    s_pairing_requested_us = esp_timer_get_time();

    if (!force_rest && app_gateway_mqtt_is_connected()) {
        ESP_LOGI(TAG, "Requesting pairing session via MQTT");
        esp_err_t err = app_gateway_mqtt_publish_pairing_create();
        if (err != ESP_OK) {
            s_pairing_in_flight = false;
            s_pairing.retry_available = true;
            ESP_LOGW(TAG, "Pairing publish failed; waiting for user");
            notify_update();
        }
        return err;
    }

    ESP_LOGI(TAG, "Requesting pairing session via REST");
    char token[128] = {0};
    char url[256] = {0};
    int remaining = 0;
    xSemaphoreGive(s_mutex);
    esp_err_t err = app_gateway_http_create_pairing(app_config_get_api_url(), s_access_token,
                                                    token, sizeof(token), url, sizeof(url), &remaining);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_pairing_in_flight = false;
    if (err == APP_GW_ERR_UNAUTHORIZED) {
        s_access_token[0] = '\0';
        s_pairing_wait_for_user = false;
        s_needs_pairing_create = true;
        s_gw_state = GW_STATE_AUTHENTICATING;
    } else if (err == ESP_OK && s_gw_state != GW_STATE_LINKED_RUNNING) {
        snprintf(s_pairing.token, sizeof(s_pairing.token), "%s", token);
        snprintf(s_pairing.pair_url, sizeof(s_pairing.pair_url), "%s", url);
        s_pairing.remaining_sec = remaining;
        s_pairing_deadline_us = esp_timer_get_time() + (int64_t)remaining * 1000000;
        s_pairing.active = remaining > 0;
        s_pairing.denied = false;
        s_pairing.retry_available = !s_pairing.active;
        s_needs_pairing_create = false;
        s_gw_state = GW_STATE_UNLINKED_PAIRING;
        notify_update();
    } else if (err != ESP_OK) {
        s_pairing.retry_available = true;
        ESP_LOGW(TAG, "Pairing REST failed; waiting for user (err=%s)", esp_err_to_name(err));
        if (err == ESP_ERR_INVALID_STATE) s_needs_sync = true;
        notify_update();
    }
    return err;
}

/* Mot lan sync + mot lan REST cho moi scope lien ket, khong poll snapshot vo han. */
static void recover_snapshot_locked(void)
{
    if (!s_cloud_linked || !s_snapshot) return;
    if (s_snapshot_received_for_link && snapshot_has_devices_or_scenes()) {
        s_snapshot_recovery = SNAP_IDLE;
        return;
    }
    int64_t now = esp_timer_get_time();
    if (s_snapshot_recovery == SNAP_WAIT_PUSH && now >= s_snapshot_check_us) {
        ESP_LOGW(TAG, "Snapshot missing/empty; scheduling one diagnostic sync");
        s_snapshot_recovery = SNAP_NEED_SYNC;
        s_needs_sync = true;
    }
    if (s_snapshot_recovery != SNAP_WAIT_SYNC || now < s_snapshot_check_us) return;
    s_snapshot_recovery = SNAP_REST_DONE;
    uint32_t epoch = s_snapshot_epoch;
    uint32_t serial = s_mqtt_snapshot_serial;
    char home[sizeof(s_snapshot_home)];
    snprintf(home, sizeof(home), "%s", s_snapshot_home);
    char *json = NULL;
    size_t len = 0;
    xSemaphoreGive(s_mutex);
    esp_err_t err = app_gateway_http_fetch_snapshot(app_config_get_api_url(), s_access_token, &json, &len);
    app_snapshot_t *candidate = NULL;
    if (err == ESP_OK) {
        candidate = heap_caps_calloc(1, sizeof(*candidate), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!candidate) err = ESP_ERR_NO_MEM;
        else {
            cJSON *root = cJSON_ParseWithLength(json, len);
            if (!root) err = ESP_ERR_INVALID_RESPONSE;
            else {
                cJSON *request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
                if (cJSON_IsString(request_id)) ESP_LOGI(TAG, "REST snapshot request_id=%.96s", request_id->valuestring);
                cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
                err = app_gateway_parser_parse_snapshot_json(data ? data : root, candidate);
                cJSON_Delete(root);
            }
        }
    }
    free(json);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (epoch != s_snapshot_epoch || !s_cloud_linked) {
        ESP_LOGW(TAG, "Discarding REST snapshot from obsolete link scope");
    } else if (err == APP_GW_ERR_UNAUTHORIZED) {
        s_access_token[0] = '\0';
        ESP_LOGW(TAG, "REST snapshot unauthorized; scheduling token refresh");
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "REST snapshot diagnostic failed: %s; model retained", esp_err_to_name(err));
    } else if (strcmp(candidate->home_id, home) != 0) {
        ESP_LOGW(TAG, "REST snapshot home mismatch; model retained");
    } else if (candidate->revision < s_snapshot->revision ||
               (serial != s_mqtt_snapshot_serial && candidate->revision <= s_snapshot->revision)) {
        ESP_LOGI(TAG, "REST snapshot older than current MQTT model; discarded");
    } else {
        memcpy(s_snapshot, candidate, sizeof(*s_snapshot));
        s_snapshot_received_for_link = true;
        ESP_LOGI(TAG, "Snapshot applied source=REST bytes=%zu revision=%lld rooms=%u devices=%u scenes=%u",
                 len, (long long)s_snapshot->revision, s_snapshot->room_count, s_snapshot->device_count, s_snapshot->scene_count);
        if (!snapshot_has_devices_or_scenes()) {
            ESP_LOGW(TAG, "REST snapshot valid but empty; verify NobleX home/grant data. Automatic diagnostic complete");
        }
        esp_err_t save_err = app_gateway_parser_save_snapshot_file(s_snapshot);
        if (save_err != ESP_OK) ESP_LOGW(TAG, "REST snapshot cache write failed: %s", esp_err_to_name(save_err));
        notify_update();
    }
    free(candidate);
}

static void app_gateway_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Gateway background task started");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        xSemaphoreTake(s_mutex, portMAX_DELAY);

        /* 1. Dong ho don dieu khong bi cham boi cac request HTTP. */
        int64_t now_us = esp_timer_get_time();
        if (s_pairing.active) {
            int64_t remaining_us = s_pairing_deadline_us - now_us;
            s_pairing.remaining_sec = remaining_us > 0 ? (int)((remaining_us + 999999) / 1000000) : 0;
            if (s_pairing.remaining_sec == 0) {
                s_pairing.active = false;
                s_pairing_wait_for_user = true;
                s_pairing.retry_available = true;
                ESP_LOGI(TAG, "Pairing session expired; waiting for user");
                notify_update();
            }
        }
        if (s_pairing_in_flight && now_us - s_pairing_requested_us >= PAIRING_REQUEST_TIMEOUT_US) {
            s_pairing_in_flight = false;
            s_needs_pairing_create = false;
            s_pairing_wait_for_user = true;
            s_pairing.retry_available = true;
            ESP_LOGW(TAG, "Pairing request timed out; waiting for user");
            notify_update();
        }

        /* 2. Token refresh check (when remaining <= 60s) */
        int64_t now_s = esp_timer_get_time() / 1000000;
        bool token_valid = (strlen(s_access_token) > 0) &&
                           ((now_s - s_token_acquired_time_s) < (int64_t)(s_token_expires_in_s - 60));

        /* 3. Connection State Machine */
        if (s_net_connected && app_network_is_time_synced()) {
            if (!token_valid) {
                s_gw_state = GW_STATE_AUTHENTICATING;
                xSemaphoreGive(s_mutex);

                const char *base_url = app_config_get_api_url();
                const char *client_id = app_config_get_client_id();
                const char *client_sec = app_config_get_client_secret();

                uint32_t expires_in = 600;
                esp_err_t err = app_gateway_http_fetch_token(base_url, client_id, client_sec,
                                                             s_access_token, sizeof(s_access_token), &expires_in);

                xSemaphoreTake(s_mutex, portMAX_DELAY);
                if (err == ESP_OK) {
                    s_token_acquired_time_s = esp_timer_get_time() / 1000000;
                    s_token_expires_in_s = expires_in;
                    s_gw_state = GW_STATE_FETCHING_CONFIG;
                } else {
                    ESP_LOGE(TAG, "OAuth authentication failed, retry in 5s");
                    s_gw_state = GW_STATE_ERROR;
                    xSemaphoreGive(s_mutex);
                    vTaskDelay(pdMS_TO_TICKS(4000));
                    continue;
                }
            }

            if (s_gw_state == GW_STATE_ERROR && token_valid) {
                s_gw_state = GW_STATE_FETCHING_CONFIG;
            }
            if (s_gw_state == GW_STATE_FETCHING_CONFIG) {
                xSemaphoreGive(s_mutex);
                const char *base_url = app_config_get_api_url();
                app_gw_mqtt_config_t cfg;
                esp_err_t err = app_gateway_http_fetch_config(base_url, s_access_token, &cfg);

                xSemaphoreTake(s_mutex, portMAX_DELAY);
                if (err == ESP_OK) {
                    memcpy(&s_mqtt_cfg, &cfg, sizeof(s_mqtt_cfg));
                    s_gw_state = GW_STATE_MQTT_CONNECTING;

                    /* Bootstrap REST mot lan; refresh config khong thay QR con han. */
                    bool linked = s_snapshot && s_snapshot->home_id[0];
                    if (!linked && !s_pairing.active && !s_pairing_in_flight && !s_pairing_wait_for_user) {
                        s_needs_pairing_create = true;
                    }
                    if (s_needs_pairing_create && !s_pairing_in_flight) {
                        esp_err_t pairing_err = request_pairing_locked(true);
                        if (pairing_err == APP_GW_ERR_UNAUTHORIZED) {
                            xSemaphoreGive(s_mutex);
                            vTaskDelay(pdMS_TO_TICKS(4000));
                            continue;
                        }
                    }

                    /* Init & start MQTT */
                    const char *client_id = app_config_get_client_id();
                    esp_err_t mqtt_err = app_gateway_mqtt_init(&s_mqtt_cfg, client_id, s_access_token,
                                                               on_mqtt_msg, on_mqtt_status, NULL);
                    if (mqtt_err == ESP_OK) mqtt_err = app_gateway_mqtt_start();
                    if (mqtt_err != ESP_OK) {
                        ESP_LOGE(TAG, "MQTT startup failed: %s", esp_err_to_name(mqtt_err));
                        s_gw_state = GW_STATE_ERROR;
                    }
                } else {
                    if (err == APP_GW_ERR_UNAUTHORIZED) {
                        s_access_token[0] = '\0';
                        s_token_acquired_time_s = 0;
                    }
                    ESP_LOGE(TAG, "Fetch MQTT config failed, retry in 5s");
                    s_gw_state = GW_STATE_ERROR;
                    xSemaphoreGive(s_mutex);
                    vTaskDelay(pdMS_TO_TICKS(4000));
                    continue;
                }
            }

            recover_snapshot_locked();

            /* 4. Action triggers */
            if (s_needs_pairing_create && !s_pairing_in_flight) {
                esp_err_t pairing_err = request_pairing_locked(false);
                if (pairing_err == APP_GW_ERR_UNAUTHORIZED) {
                    xSemaphoreGive(s_mutex);
                    vTaskDelay(pdMS_TO_TICKS(4000));
                    continue;
                }
            }

            if (app_gateway_mqtt_is_connected()) {
                if (s_needs_unlink) {
                    s_needs_unlink = false;
                    app_gateway_mqtt_publish_unlink();
                } else if (s_needs_sync) {
                    if (rate_limit_check_sync()) {
                        if (app_gateway_mqtt_publish_sync() == ESP_OK) {
                            s_needs_sync = false;
                            if (s_snapshot_recovery == SNAP_NEED_SYNC) {
                                s_snapshot_recovery = SNAP_WAIT_SYNC;
                                s_snapshot_check_us = esp_timer_get_time() + SNAPSHOT_WAIT_US;
                            }
                        }
                    }
                }
            }
        }

        xSemaphoreGive(s_mutex);
    }
}

esp_err_t app_gateway_init(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (!s_snapshot) {
        s_snapshot = (app_snapshot_t *)heap_caps_calloc(1, sizeof(app_snapshot_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_snapshot) {
            s_snapshot = (app_snapshot_t *)calloc(1, sizeof(app_snapshot_t));
        }
    }
    if (!s_snapshot) {
        ESP_LOGE(TAG, "Failed to allocate snapshot in PSRAM");
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    memset(s_snapshot, 0, sizeof(app_snapshot_t));
    memset(&s_pairing, 0, sizeof(app_pairing_session_t));

    /* Offline-First: Read cache from /spiffs/snapshot.json immediately */
    esp_err_t err = app_gateway_parser_load_snapshot_file(s_snapshot);
    if (err == ESP_OK && strlen(s_snapshot->home_id) > 0) {
        s_gw_state = GW_STATE_LINKED_RUNNING;
        ESP_LOGI(TAG, "Offline-First loaded snapshot: home='%s', %d devices ready",
                 s_snapshot->home_name, s_snapshot->device_count);
    } else {
        s_gw_state = GW_STATE_DISCONNECTED;
        ESP_LOGI(TAG, "Offline snapshot not found or empty");
    }

    /* Register to network events */
    app_network_register_event_cb(on_network_event, NULL);
    if (app_network_is_connected()) {
        s_net_connected = true;
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t app_gateway_start(void)
{
    if (s_task_handle != NULL) {
        return ESP_OK;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(app_gateway_task,
                                            "app_gateway",
                                            8192,
                                            NULL,
                                            5,
                                            &s_task_handle,
                                            1);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}

app_gw_state_t app_gateway_get_state(void)
{
    app_gw_state_t state;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    state = s_gw_state;
    xSemaphoreGive(s_mutex);
    return state;
}

const app_snapshot_t *app_gateway_get_snapshot(void)
{
    return s_snapshot;
}

bool app_gateway_has_snapshot(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool valid = s_snapshot_received_for_link ||
                 (!s_cloud_linked && s_snapshot && s_snapshot->home_id[0]);
    xSemaphoreGive(s_mutex);
    return valid;
}

const app_pairing_session_t *app_gateway_get_pairing_session(void)
{
    return &s_pairing;
}

esp_err_t app_gateway_create_pairing(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_gw_state == GW_STATE_LINKED_RUNNING) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_pairing_in_flight && !s_needs_pairing_create) {
        s_pairing.active = false;
        s_pairing.denied = false;
        s_pairing.retry_available = false;
        s_pairing_wait_for_user = false;
        s_needs_pairing_create = true;
        notify_update();
    }
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t app_gateway_unlink(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_needs_unlink = true;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t app_gateway_send_command(const char *device_id, const char *endpoint_id, const char *trait, const char *value_json)
{
    if (!device_id || !endpoint_id || !trait || !value_json) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!rate_limit_check_cmd()) {
        return ESP_ERR_INVALID_STATE;
    }

    return app_gateway_mqtt_publish_device_command(device_id, endpoint_id, trait, value_json, 10000);
}

esp_err_t app_gateway_run_scene(const char *scene_id)
{
    if (!scene_id) return ESP_ERR_INVALID_ARG;

    if (!rate_limit_check_cmd()) {
        return ESP_ERR_INVALID_STATE;
    }

    return app_gateway_mqtt_publish_scene_run(scene_id, 5000);
}

void app_gateway_register_update_cb(app_gateway_update_cb_t cb)
{
    if (!cb) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < s_num_update_cbs; i++) {
        if (s_update_cbs[i] == cb) {
            xSemaphoreGive(s_mutex);
            return;
        }
    }
    if (s_num_update_cbs < MAX_UPDATE_CBS) {
        s_update_cbs[s_num_update_cbs++] = cb;
    }
    xSemaphoreGive(s_mutex);
}
