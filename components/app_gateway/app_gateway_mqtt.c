#include "app_gateway_mqtt.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "esp_log.h"
#include "esp_random.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"

static const char *TAG = "app_gw_mqtt";

static const char *s_amazon_root_ca_1 =
"-----BEGIN CERTIFICATE-----\n"
"MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n"
"ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n"
"b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n"
"MAkGA1UEBhMCVVMxDzANBgNVBAoTbkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n"
"b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n"
"ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n"
"9O6II8c+6zfQnY8hurZv4bn/mYK8BiHpjfl0024ChhZDbJYPeOX56isO2C62gnf/\n"
"fpfZWQ2869+/2NK6PER45OIWPFNNpljhYWQPAjOTPem5KVpUiV84IxCGWP0hpNyH\n"
"WsJRlvnAjgblfKchNUieva36tgfmZCcBE0kw5j8N53DSKv3TZfMWRgEKfiv63hC4\n"
"iSXYjgo34SItgqprUv98U03xYLFQLO3QKunuhvoYCKeTGmdU4YN9fd1IWWDvBI5b\n"
"JwIDAQABo2MwYTAPBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNV\n"
"HQ4EFgQUTiJAZfdWy6nTLfeRipIGnvwIrqwwhwYDVR0jBBgwFoAUTiJAZfdWy6nT\n"
"LfeRipIGnvwIrqwwDQYJKoZIhvcNAQELBQADggEBAOWOXBZAQPeJAOf6vgPfEQEc\n"
"BfqRZ05VDdKA+55n262usqOSE256806lxqNWJWOyK8nVlf8Pn59U53x2DDUnrw5v\n"
"a69JMKgsioZps++AxGSXUSFTd8BCbd4euaSO29fU7A0ecRFmKW+EcKWpKO3QDWpr\n"
"vJgSVeIQPSQ8oEz6V4xDHcG30+JKSS070VNWDq5Y3ZpuEC8Nzv4PO3vTQGOuzcHu\n"
"Inn4SPHVBP/iC187wfQWzxzy307ENismgPqvTrxBgwq95549mTFaBNYRTVmaJxCX\n"
"mb2ElKCgySLTYU6GiziInPz78DnntNwWBGo8gtqtTtKnNu4d0xdZz0foT7W0l4U=\n"
"-----END CERTIFICATE-----\n";

static const char *s_alpn_protocols[] = {"mqtt", NULL};

static esp_mqtt_client_handle_t s_client = NULL;
static app_gw_mqtt_config_t s_cfg;
static char s_client_id[64] = {0};
static char s_access_token[APP_GW_ACCESS_TOKEN_SIZE] = {0};
static bool s_is_connected = false;
static int s_subscribe_msg_id = -1;

static app_gw_mqtt_msg_cb_t s_msg_cb = NULL;
static app_gw_mqtt_status_cb_t s_status_cb = NULL;
static void *s_user_ctx = NULL;

/* Message buffer for accumulating multi-fragment MQTT packets */
#define MQTT_RECV_BUF_SIZE APP_GW_MAX_PAYLOAD_SIZE
static char *s_recv_buffer = NULL;
static size_t s_recv_len = 0;
static size_t s_total_expected_len = 0;

static void generate_uuid(char *out_str, size_t max_len)
{
    uint32_t r1 = esp_random();
    uint32_t r2 = esp_random();
    uint32_t r3 = esp_random();
    uint32_t r4 = esp_random();
    snprintf(out_str, max_len, "%08lx-%04lx-%04lx-%04lx-%08lx%04lx",
             (unsigned long)r1,
             (unsigned long)(r2 & 0xFFFF),
             (unsigned long)(((r2 >> 16) & 0x0FFF) | 0x4000), /* version 4 */
             (unsigned long)((r3 & 0x3FFF) | 0x8000),         /* variant 1 */
             (unsigned long)r4,
             (unsigned long)(r3 >> 16));
}

static void get_iso8601_utc(char *out_str, size_t max_len)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm_utc;
    gmtime_r(&tv.tv_sec, &tm_utc);
    int ms = (int)(tv.tv_usec / 1000);
    snprintf(out_str, max_len, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             (int)(tm_utc.tm_year + 1900), (int)(tm_utc.tm_mon + 1), (int)tm_utc.tm_mday,
             (int)tm_utc.tm_hour, (int)tm_utc.tm_min, (int)tm_utc.tm_sec, ms);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected to broker!");
        s_is_connected = false;
        s_subscribe_msg_id = -1;
        if (strlen(s_cfg.topic_down) > 0) {
            s_subscribe_msg_id = esp_mqtt_client_subscribe(s_client, s_cfg.topic_down, 1);
            ESP_LOGI(TAG, "Subscribe requested for %s, msg_id=%d", s_cfg.topic_down, s_subscribe_msg_id);
            if (s_subscribe_msg_id < 0) ESP_LOGE(TAG, "MQTT subscribe request failed");
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected from broker");
        s_is_connected = false;
        s_subscribe_msg_id = -1;
        free(s_recv_buffer);
        s_recv_buffer = NULL;
        s_recv_len = s_total_expected_len = 0;
        if (s_status_cb) {
            s_status_cb(false, s_user_ctx);
        }
        break;

    case MQTT_EVENT_SUBSCRIBED:
        if (s_subscribe_msg_id >= 0 && event->msg_id == s_subscribe_msg_id && !s_is_connected) {
            ESP_LOGI(TAG, "MQTT subscribed successfully, msg_id=%d", event->msg_id);
            s_is_connected = true;
            if (s_status_cb) s_status_cb(true, s_user_ctx);
        }
        break;

    case MQTT_EVENT_DATA:
        if (event->current_data_offset == 0) {
            free(s_recv_buffer);
            s_recv_buffer = NULL;
            s_recv_len = s_total_expected_len = 0;
            if (event->total_data_len <= 0 || event->total_data_len > MQTT_RECV_BUF_SIZE) {
                ESP_LOGW(TAG, "MQTT message length rejected: %d", event->total_data_len);
                break;
            }
            s_total_expected_len = (size_t)event->total_data_len;
            s_recv_buffer = heap_caps_malloc(s_total_expected_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!s_recv_buffer) {
                s_total_expected_len = 0;
                ESP_LOGE(TAG, "No PSRAM for MQTT message");
                break;
            }
        }
        if (!s_recv_buffer || event->current_data_offset < 0 || event->data_len < 0 ||
            (size_t)event->current_data_offset != s_recv_len ||
            event->total_data_len <= 0 || (size_t)event->total_data_len != s_total_expected_len ||
            (size_t)event->data_len > s_total_expected_len - s_recv_len ||
            (event->data_len > 0 && !event->data)) {
            ESP_LOGW(TAG, "MQTT fragment rejected: offset=%d length=%d received=%zu total=%zu",
                     event->current_data_offset, event->data_len, s_recv_len, s_total_expected_len);
            free(s_recv_buffer);
            s_recv_buffer = NULL;
            s_recv_len = s_total_expected_len = 0;
            break;
        }
        if (event->data_len > 0) memcpy(s_recv_buffer + s_recv_len, event->data, event->data_len);
        s_recv_len += event->data_len;
        s_recv_buffer[s_recv_len] = '\0';
        if (s_recv_len == s_total_expected_len) {
            if (s_msg_cb) s_msg_cb(s_recv_buffer, s_recv_len, s_user_ctx);
            free(s_recv_buffer);
            s_recv_buffer = NULL;
            s_recv_len = s_total_expected_len = 0;
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error occurred");
        break;

    default:
        break;
    }
}

esp_err_t app_gateway_mqtt_init(const app_gw_mqtt_config_t *cfg,
                                const char *client_id,
                                const char *access_token,
                                app_gw_mqtt_msg_cb_t msg_cb,
                                app_gw_mqtt_status_cb_t status_cb,
                                void *ctx)
{
    if (!cfg || !client_id || !access_token) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t token_len = strlen(access_token);
    if (token_len == 0 || token_len >= sizeof(s_access_token)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (s_client) {
        app_gateway_mqtt_stop();
    }

    memcpy(&s_cfg, cfg, sizeof(app_gw_mqtt_config_t));
    strncpy(s_client_id, client_id, sizeof(s_client_id) - 1);
    memcpy(s_access_token, access_token, token_len + 1);
    s_msg_cb = msg_cb;
    s_status_cb = status_cb;
    s_user_ctx = ctx;

    /* Build URI with mqtts:// scheme if not already present */
    char uri_buf[256] = {0};
    if (strlen(s_cfg.url_tls) > 0) {
        if (strncmp(s_cfg.url_tls, "mqtts://", 8) == 0 || strncmp(s_cfg.url_tls, "ssl://", 6) == 0) {
            strncpy(uri_buf, s_cfg.url_tls, sizeof(uri_buf) - 1);
        } else {
            snprintf(uri_buf, sizeof(uri_buf), "mqtts://%s", s_cfg.url_tls);
        }
    } else if (strlen(s_cfg.url_wss) > 0) {
        /* Extract host from wss://<host>/mqtt (for AWS IoT Core TLS 443 ALPN) */
        const char *p = strstr(s_cfg.url_wss, "://");
        p = p ? p + 3 : s_cfg.url_wss;
        const char *slash = strchr(p, '/');
        size_t hlen = slash ? (size_t)(slash - p) : strlen(p);
        char host[128] = {0};
        if (hlen < sizeof(host)) {
            memcpy(host, p, hlen);
            host[hlen] = '\0';
            snprintf(uri_buf, sizeof(uri_buf), "mqtts://%s", host);
        }
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = uri_buf,
                .port = 443,
            },
            .verification = {
                .certificate = s_amazon_root_ca_1,
                .crt_bundle_attach = esp_crt_bundle_attach,
                .alpn_protos = s_alpn_protocols,
            },
        },
        .credentials = {
            .client_id = s_client_id,
            .username = s_cfg.username,
            .authentication = {
                .password = s_access_token,
            },
        },
        .session = {
            .keepalive = 30,
            .disable_clean_session = false,
        },
        .network = {
            .timeout_ms = 10000,
        },
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
    ESP_LOGI(TAG, "MQTT client initialized: uri=%s, client_id=%s", uri_buf, s_client_id);
    return ESP_OK;
}

esp_err_t app_gateway_mqtt_start(void)
{
    if (!s_client) return ESP_ERR_INVALID_STATE;
    return esp_mqtt_client_start(s_client);
}

esp_err_t app_gateway_mqtt_stop(void)
{
    if (!s_client) return ESP_OK;
    esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    s_is_connected = false;
    s_subscribe_msg_id = -1;
    if (s_recv_buffer) {
        free(s_recv_buffer);
        s_recv_buffer = NULL;
    }
    return ESP_OK;
}

bool app_gateway_mqtt_is_connected(void)
{
    return s_is_connected;
}

static esp_err_t publish_envelope(const char *type, const char *data_json)
{
    if (!s_client || !s_is_connected || strlen(s_cfg.topic_up) == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    char msg_id[64] = {0};
    char ts[64] = {0};
    generate_uuid(msg_id, sizeof(msg_id));
    get_iso8601_utc(ts, sizeof(ts));

    char *buf = (char *)malloc(2048);
    if (!buf) return ESP_ERR_NO_MEM;

    int written = snprintf(buf, 2048,
                           "{\"v\":1,\"id\":\"%s\",\"type\":\"%s\",\"ts\":\"%s\",\"data\":%s}",
                           msg_id, type, ts, data_json ? data_json : "{}");

    if (written <= 0 || written >= 2048) {
        free(buf);
        return ESP_ERR_NO_MEM;
    }

    int res = esp_mqtt_client_publish(s_client, s_cfg.topic_up, buf, written, 1, 0);
    free(buf);

    if (res < 0) {
        ESP_LOGE(TAG, "Failed to publish %s to %s", type, s_cfg.topic_up);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Published '%s' (id=%s) to %s", type, msg_id, s_cfg.topic_up);
    return ESP_OK;
}

esp_err_t app_gateway_mqtt_publish_sync(void)
{
    return publish_envelope("sync", "{}");
}

esp_err_t app_gateway_mqtt_publish_pairing_create(void)
{
    return publish_envelope("pairing.create", "{}");
}

esp_err_t app_gateway_mqtt_publish_unlink(void)
{
    return publish_envelope("unlink", "{}");
}

esp_err_t app_gateway_mqtt_publish_device_command(const char *device_id,
                                                  const char *endpoint_id,
                                                  const char *trait,
                                                  const char *value_json,
                                                  uint32_t ttl_ms)
{
    if (!device_id || !endpoint_id || !trait || !value_json) {
        return ESP_ERR_INVALID_ARG;
    }

    char cmd_id[64] = {0};
    char uuid_part[40] = {0};
    generate_uuid(uuid_part, sizeof(uuid_part));
    snprintf(cmd_id, sizeof(cmd_id), "cmd-%s", uuid_part);

    if (ttl_ms == 0 || ttl_ms > 30000) {
        ttl_ms = 10000;
    }

    char data_json[512] = {0};
    snprintf(data_json, sizeof(data_json),
             "{\"command_id\":\"%s\",\"ttl_ms\":%lu,\"device_id\":\"%s\",\"endpoint_id\":\"%s\",\"trait\":\"%s\",\"value\":%s}",
             cmd_id, (unsigned long)ttl_ms, device_id, endpoint_id, trait, value_json);

    return publish_envelope("device.command", data_json);
}

esp_err_t app_gateway_mqtt_publish_scene_run(const char *scene_id, uint32_t ttl_ms)
{
    if (!scene_id) return ESP_ERR_INVALID_ARG;

    char cmd_id[64] = {0};
    char uuid_part[40] = {0};
    generate_uuid(uuid_part, sizeof(uuid_part));
    snprintf(cmd_id, sizeof(cmd_id), "scene-%s", uuid_part);

    if (ttl_ms == 0 || ttl_ms > 30000) {
        ttl_ms = 5000;
    }

    char data_json[256] = {0};
    snprintf(data_json, sizeof(data_json),
             "{\"command_id\":\"%s\",\"ttl_ms\":%lu,\"scene_id\":\"%s\"}",
             cmd_id, (unsigned long)ttl_ms, scene_id);

    return publish_envelope("scene.run", data_json);
}
