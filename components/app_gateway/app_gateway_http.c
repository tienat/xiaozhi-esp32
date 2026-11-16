#include "app_gateway_http.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "mbedtls/base64.h"
#include "cJSON.h"
#include "esp_heap_caps.h"

static const char *TAG = "app_gw_http";

#define HTTP_RESP_BUF_SIZE 4096

typedef struct {
    char *buffer;
    int buffer_len;
    int max_len;
    bool overflow;
} http_response_ctx_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_ctx_t *ctx = (http_response_ctx_t *)evt->user_data;
    if (!ctx) return ESP_OK;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (ctx->buffer && evt->data_len > 0) {
            int to_copy = evt->data_len;
            if (to_copy > ctx->max_len - 1 - ctx->buffer_len) {
                ctx->overflow = true;
                to_copy = ctx->max_len - 1 - ctx->buffer_len;
            }
            if (to_copy > 0) {
                memcpy(ctx->buffer + ctx->buffer_len, evt->data, to_copy);
                ctx->buffer_len += to_copy;
                ctx->buffer[ctx->buffer_len] = '\0';
            }
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void urlencode(const char *src, char *dst, size_t dst_len)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t d_idx = 0;
    while (*src && d_idx + 4 < dst_len) {
        unsigned char c = (unsigned char)*src;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[d_idx++] = c;
        } else {
            dst[d_idx++] = '%';
            dst[d_idx++] = hex[(c >> 4) & 0x0F];
            dst[d_idx++] = hex[c & 0x0F];
        }
        src++;
    }
    dst[d_idx] = '\0';
}

esp_err_t app_gateway_http_fetch_token(const char *base_url,
                                       const char *client_id,
                                       const char *secret,
                                       char *out_token,
                                       size_t max_len,
                                       uint32_t *out_expires_in)
{
    if (!base_url || !client_id || !out_token || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    out_token[0] = '\0';

    if (!secret || strlen(secret) == 0) {
        ESP_LOGE(TAG, "Cannot fetch token: client_secret is empty for client_id '%s'!", client_id);
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Fetching OAuth token: client_id='%s', secret_len=%zu", client_id, strlen(secret));

    /* 1. Build Basic Auth string: base64(urlencode(client_id):urlencode(secret)) */
    char enc_cid[128] = {0};
    char enc_sec[256] = {0};
    urlencode(client_id, enc_cid, sizeof(enc_cid));
    urlencode(secret ? secret : "", enc_sec, sizeof(enc_sec));

    char raw_cred[400] = {0};
    snprintf(raw_cred, sizeof(raw_cred), "%s:%s", enc_cid, enc_sec);

    unsigned char b64_cred[512] = {0};
    size_t b64_len = 0;
    int ret = mbedtls_base64_encode(b64_cred, sizeof(b64_cred) - 1, &b64_len,
                                    (const unsigned char *)raw_cred, strlen(raw_cred));
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 encode credentials failed (%d)", ret);
        return ESP_FAIL;
    }
    b64_cred[b64_len] = '\0';

    char auth_header[600] = {0};
    snprintf(auth_header, sizeof(auth_header), "Basic %s", (char *)b64_cred);

    /* 2. Configure HTTP Client */
    char full_url[256] = {0};
    snprintf(full_url, sizeof(full_url), "%s/v1/hc/oauth/token", base_url);

    char *resp_buf = (char *)malloc(HTTP_RESP_BUF_SIZE);
    if (!resp_buf) {
        return ESP_ERR_NO_MEM;
    }
    memset(resp_buf, 0, HTTP_RESP_BUF_SIZE);

    http_response_ctx_t resp_ctx = {
        .buffer = resp_buf,
        .buffer_len = 0,
        .max_len = HTTP_RESP_BUF_SIZE,
    };

    esp_http_client_config_t config = {
        .url = full_url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp_ctx,
        .timeout_ms = 10000,
        .buffer_size = 4096,
        .buffer_size_tx = APP_GW_ACCESS_TOKEN_SIZE + 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(resp_buf);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");

    const char *post_data = "grant_type=client_credentials";
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    ESP_LOGI(TAG, "--> POST %s | Body: %s", full_url, post_data);

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (resp_ctx.overflow && status_code >= 200 && status_code < 300) {
        ESP_LOGE(TAG, "HTTP response exceeded buffer capacity");
        free(resp_buf);
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI(TAG, "<-- [%d] %s", status_code, full_url);

    if (err != ESP_OK || status_code != 200) {
        ESP_LOGE(TAG, "Fetch token failed: err=%s, status=%d",
                 esp_err_to_name(err), status_code);
        free(resp_buf);
        return (status_code == 401) ? APP_GW_ERR_UNAUTHORIZED : ESP_FAIL;
    }

    /* 3. Parse JSON response: { "access_token": "...", "expires_in": 600 } */
    cJSON *root = cJSON_Parse(resp_buf);
    free(resp_buf);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse token response JSON");
        return ESP_FAIL;
    }

    cJSON *item = cJSON_GetObjectItem(root, "access_token");
    if (!cJSON_IsString(item) || !item->valuestring) {
        ESP_LOGE(TAG, "Missing access_token in response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    size_t token_len = strlen(item->valuestring);
    if (token_len == 0 || token_len >= max_len || token_len >= APP_GW_ACCESS_TOKEN_SIZE) {
        ESP_LOGE(TAG, "OAuth token length invalid: %zu (capacity %zu)", token_len, max_len);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(out_token, item->valuestring, token_len + 1);

    item = cJSON_GetObjectItem(root, "expires_in");
    if (out_expires_in) {
        *out_expires_in = (cJSON_IsNumber(item)) ? (uint32_t)item->valueint : 600;
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "OAuth token fetched successfully, expires_in=%lu s",
             out_expires_in ? (unsigned long)*out_expires_in : 600);
    return ESP_OK;
}

esp_err_t app_gateway_http_fetch_config(const char *base_url,
                                        const char *access_token,
                                        app_gw_mqtt_config_t *out_cfg)
{
    if (!base_url || !access_token || !out_cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    char full_url[256] = {0};
    snprintf(full_url, sizeof(full_url), "%s/v1/hc/config", base_url);

    char *resp_buf = (char *)malloc(HTTP_RESP_BUF_SIZE);
    if (!resp_buf) {
        return ESP_ERR_NO_MEM;
    }
    memset(resp_buf, 0, HTTP_RESP_BUF_SIZE);

    http_response_ctx_t resp_ctx = {
        .buffer = resp_buf,
        .buffer_len = 0,
        .max_len = HTTP_RESP_BUF_SIZE,
    };

    esp_http_client_config_t config = {
        .url = full_url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .user_data = &resp_ctx,
        .timeout_ms = 10000,
        .buffer_size = 4096,
        .buffer_size_tx = APP_GW_ACCESS_TOKEN_SIZE + 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(resp_buf);
        return ESP_FAIL;
    }

    char auth_header[APP_GW_ACCESS_TOKEN_SIZE + sizeof("Bearer ")] = {0};
    int header_len = snprintf(auth_header, sizeof(auth_header), "Bearer %s", access_token);
    if (!access_token[0] || strlen(access_token) >= APP_GW_ACCESS_TOKEN_SIZE ||
        header_len < 0 || (size_t)header_len >= sizeof(auth_header)) {
        esp_http_client_cleanup(client);
        free(resp_buf);
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t header_err = esp_http_client_set_header(client, "Authorization", auth_header);
    if (header_err != ESP_OK) {
        esp_http_client_cleanup(client);
        free(resp_buf);
        return header_err;
    }

    ESP_LOGI(TAG, "--> GET %s", full_url);

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (resp_ctx.overflow && status_code >= 200 && status_code < 300) {
        ESP_LOGE(TAG, "HTTP response exceeded buffer capacity");
        free(resp_buf);
        return ESP_ERR_INVALID_SIZE;
    }
    if (status_code == 200 || status_code == 201) {
        ESP_LOGI(TAG, "<-- [%d] %s", status_code, full_url);
    } else {
        ESP_LOGI(TAG, "<-- [%d] %s: %s", status_code, full_url, resp_buf ? resp_buf : "");
    }

    if (err != ESP_OK || status_code != 200) {
        ESP_LOGE(TAG, "Fetch config failed: err=%s, status=%d",
                 esp_err_to_name(err), status_code);
        free(resp_buf);
        return (status_code == 401) ? APP_GW_ERR_UNAUTHORIZED : ESP_FAIL;
    }

    /* 3. Parse JSON response: { "data": { "mqtt": { ... }, "limits": { ... } } } */
    cJSON *root = cJSON_Parse(resp_buf);
    free(resp_buf);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse config response JSON");
        return ESP_FAIL;
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) data = root;

    cJSON *mqtt = cJSON_GetObjectItem(data, "mqtt");
    if (!mqtt) {
        ESP_LOGE(TAG, "Missing mqtt block in config response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    memset(out_cfg, 0, sizeof(app_gw_mqtt_config_t));

    cJSON *item = cJSON_GetObjectItem(mqtt, "broker");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(out_cfg->broker, item->valuestring, sizeof(out_cfg->broker) - 1);
    }

    item = cJSON_GetObjectItem(mqtt, "url_tls");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(out_cfg->url_tls, item->valuestring, sizeof(out_cfg->url_tls) - 1);
    }

    item = cJSON_GetObjectItem(mqtt, "url_wss");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(out_cfg->url_wss, item->valuestring, sizeof(out_cfg->url_wss) - 1);
    }

    item = cJSON_GetObjectItem(mqtt, "username");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(out_cfg->username, item->valuestring, sizeof(out_cfg->username) - 1);
    }

    item = cJSON_GetObjectItem(mqtt, "topic_down");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(out_cfg->topic_down, item->valuestring, sizeof(out_cfg->topic_down) - 1);
    }

    item = cJSON_GetObjectItem(mqtt, "topic_up");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(out_cfg->topic_up, item->valuestring, sizeof(out_cfg->topic_up) - 1);
    }

    cJSON *limits = cJSON_GetObjectItem(data, "limits");
    if (limits) {
        item = cJSON_GetObjectItem(limits, "command_ttl_max_ms");
        out_cfg->command_ttl_max_ms = (cJSON_IsNumber(item)) ? item->valueint : 30000;
        item = cJSON_GetObjectItem(limits, "token_ttl_s");
        out_cfg->token_ttl_s = (cJSON_IsNumber(item)) ? item->valueint : 600;
    } else {
        out_cfg->command_ttl_max_ms = 30000;
        out_cfg->token_ttl_s = 600;
    }

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Config fetched: url_tls='%s', user='%s', down='%s', up='%s'",
             out_cfg->url_tls, out_cfg->username, out_cfg->topic_down, out_cfg->topic_up);
    return ESP_OK;
}

esp_err_t app_gateway_http_create_pairing(const char *base_url,
                                          const char *access_token,
                                          char *out_token,
                                          size_t max_token_len,
                                          char *out_pair_url,
                                          size_t max_url_len,
                                          int *out_remaining_sec)
{
    if (!base_url || !access_token || !out_token || !out_pair_url ||
        max_token_len == 0 || max_url_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    out_token[0] = '\0';
    out_pair_url[0] = '\0';
    if (out_remaining_sec) *out_remaining_sec = 0;

    char full_url[256] = {0};
    snprintf(full_url, sizeof(full_url), "%s/v1/hc/pairings", base_url);

    char *resp_buf = (char *)malloc(HTTP_RESP_BUF_SIZE);
    if (!resp_buf) return ESP_ERR_NO_MEM;
    memset(resp_buf, 0, HTTP_RESP_BUF_SIZE);

    http_response_ctx_t resp_ctx = {
        .buffer = resp_buf,
        .buffer_len = 0,
        .max_len = HTTP_RESP_BUF_SIZE,
    };

    esp_http_client_config_t config = {
        .url = full_url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp_ctx,
        .timeout_ms = 10000,
        .buffer_size = 4096,
        .buffer_size_tx = APP_GW_ACCESS_TOKEN_SIZE + 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(resp_buf);
        return ESP_FAIL;
    }

    char auth_header[APP_GW_ACCESS_TOKEN_SIZE + sizeof("Bearer ")] = {0};
    int header_len = snprintf(auth_header, sizeof(auth_header), "Bearer %s", access_token);
    if (!access_token[0] || strlen(access_token) >= APP_GW_ACCESS_TOKEN_SIZE ||
        header_len < 0 || (size_t)header_len >= sizeof(auth_header)) {
        esp_http_client_cleanup(client);
        free(resp_buf);
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t header_err = esp_http_client_set_header(client, "Authorization", auth_header);
    if (header_err != ESP_OK) {
        esp_http_client_cleanup(client);
        free(resp_buf);
        return header_err;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");

    const char *post_data = "{}";
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    ESP_LOGI(TAG, "--> POST %s | Body: %s", full_url, post_data);

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (resp_ctx.overflow && status_code >= 200 && status_code < 300) {
        ESP_LOGE(TAG, "HTTP response exceeded buffer capacity");
        free(resp_buf);
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI(TAG, "<-- [%d] %s", status_code, full_url);

    if (err != ESP_OK || (status_code != 200 && status_code != 201)) {
        ESP_LOGE(TAG, "Create pairing failed: err=%s, status=%d",
                 esp_err_to_name(err), status_code);
        free(resp_buf);
        return (status_code == 401) ? APP_GW_ERR_UNAUTHORIZED :
               (status_code == 409) ? ESP_ERR_INVALID_STATE : ESP_FAIL;
    }

    /* Parse JSON: { "data": { "token": "...", "pair_url": "...", "expires_at": "..." } } */
    cJSON *root = cJSON_Parse(resp_buf);
    free(resp_buf);
    if (!root) return ESP_FAIL;

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) data = root;

    cJSON *tok_item = cJSON_GetObjectItem(data, "token");
    cJSON *url_item = cJSON_GetObjectItem(data, "pair_url");

    if (!cJSON_IsString(tok_item) || !tok_item->valuestring || !tok_item->valuestring[0] ||
        !cJSON_IsString(url_item) || !url_item->valuestring || !url_item->valuestring[0]) {
        ESP_LOGE(TAG, "Pairing response missing token or pair_url");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    size_t token_len = strlen(tok_item->valuestring);
    size_t url_len = strlen(url_item->valuestring);
    if (token_len >= max_token_len || url_len >= max_url_len) {
        ESP_LOGE(TAG, "Pairing response exceeds output capacity (token=%zu, URL=%zu)",
                 token_len, url_len);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(out_token, tok_item->valuestring, token_len + 1);
    memcpy(out_pair_url, url_item->valuestring, url_len + 1);
    if (out_remaining_sec) {
        *out_remaining_sec = 120;
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Created pairing session");
    return ESP_OK;
}

esp_err_t app_gateway_http_fetch_snapshot(const char *base_url, const char *access_token,
                                          char **out_json, size_t *out_len)
{
    if (!base_url || !access_token || !out_json || !out_len) return ESP_ERR_INVALID_ARG;
    *out_json = NULL;
    *out_len = 0;
    if (!access_token[0] || strlen(access_token) >= APP_GW_ACCESS_TOKEN_SIZE) return ESP_ERR_INVALID_SIZE;
    char url[256];
    int n = snprintf(url, sizeof(url), "%s/v1/hc/snapshot", base_url);
    if (n < 0 || (size_t)n >= sizeof(url)) return ESP_ERR_INVALID_SIZE;
    char *buffer = heap_caps_malloc(APP_GW_MAX_PAYLOAD_SIZE + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) return ESP_ERR_NO_MEM;
    buffer[0] = '\0';
    http_response_ctx_t response = {.buffer = buffer, .max_len = APP_GW_MAX_PAYLOAD_SIZE + 1};
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .user_data = &response,
        .timeout_ms = 10000,
        .buffer_size = 4096,
        .buffer_size_tx = APP_GW_ACCESS_TOKEN_SIZE + 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { free(buffer); return ESP_FAIL; }
    char auth[APP_GW_ACCESS_TOKEN_SIZE + sizeof("Bearer ")];
    snprintf(auth, sizeof(auth), "Bearer %s", access_token);
    esp_err_t err = esp_http_client_set_header(client, "Authorization", auth);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "--> GET %s (snapshot diagnostic)", url);
        err = esp_http_client_perform(client);
    }
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "REST snapshot response: status=%d bytes=%d overflow=%d\n%.*s",
             status, response.buffer_len, response.overflow, response.buffer_len, buffer ? buffer : "");
    if (status == 401) err = APP_GW_ERR_UNAUTHORIZED;
    else if (status == 409) err = ESP_ERR_INVALID_STATE;
    else if (err == ESP_OK && status != 200) err = ESP_FAIL;
    if (err == ESP_OK && response.overflow) err = ESP_ERR_INVALID_SIZE;
    if (err != ESP_OK) { free(buffer); return err; }
    *out_json = buffer;
    *out_len = (size_t)response.buffer_len;
    return ESP_OK;
}
