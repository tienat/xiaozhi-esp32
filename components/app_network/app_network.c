#include "app_network.h"
#include <string.h>
#include <stdatomic.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef HCP_HAS_LOCAL_DEVICE_CONFIG
#include "hcp_device_local.h"
#endif

#if defined(HCP_LOCAL_WIFI_SSID) != defined(HCP_LOCAL_WIFI_PASSWORD)
#error "Local Wi-Fi configuration requires both SSID and password"
#endif

static const char *TAG = "app_network";

static void log_wifi_request(const char *source, const char *ssid, const char *pass)
{
    if (!ssid) return;
    (void)pass;
    ESP_LOGI(TAG, "Wi-Fi request [%s]: ssid=\"%s\" (len=%zu), password hidden",
             source, ssid, strlen(ssid));
}

#define MAX_EVENT_LISTENERS 4

typedef struct {
    app_network_event_cb_t cb;
    void *ctx;
} listener_t;

static listener_t s_listeners[MAX_EVENT_LISTENERS];
static size_t s_listener_count = 0;
static atomic_bool s_initialized = false;
static atomic_bool s_time_confirmed = false;
static atomic_bool s_retry_time = false;
static TaskHandle_t s_monitor_task = NULL;

static void dispatch_event(app_network_event_t event)
{
    for (size_t i = 0; i < s_listener_count; i++) {
        if (s_listeners[i].cb) {
            s_listeners[i].cb(event, s_listeners[i].ctx);
        }
    }
}

static void network_monitor_task(void *arg)
{
    wifi_c6_state_t prev_state = wifi_c6_state();
    bool prev_time_synced = false;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));

        wifi_c6_state_t current_state = wifi_c6_state();
        if (current_state != prev_state) {
            ESP_LOGI(TAG, "Network state transition: %d -> %d", prev_state, current_state);
            switch (current_state) {
            case WIFI_C6_CONNECTED: {
                char ip_buf[32] = {0};
                wifi_c6_ip(ip_buf, sizeof(ip_buf));
                ESP_LOGI(TAG, "Wi-Fi Connected! IP: %s, RSSI: %d dBm", ip_buf, wifi_c6_rssi());
                dispatch_event(APP_NETWORK_EVENT_CONNECTED);
                break;
            }
            case WIFI_C6_CONNECTING:
                dispatch_event(APP_NETWORK_EVENT_CONNECTING);
                break;
            case WIFI_C6_FAILED:
                ESP_LOGW(TAG, "Wi-Fi Connection Failed (err: %d)", wifi_c6_last_error());
                dispatch_event(APP_NETWORK_EVENT_FAILED);
                break;
            case WIFI_C6_OFF:
            case WIFI_C6_IDLE:
            default:
                dispatch_event(APP_NETWORK_EVENT_DISCONNECTED);
                break;
            }
            prev_state = current_state;
        }

        if (atomic_exchange(&s_retry_time, false)) esp_netif_sntp_start();
        /* Xac nhan SNTP thuc trong lan boot nay, khong chi kiem nguong epoch. */
        if (!atomic_load(&s_time_confirmed) && esp_netif_sntp_sync_wait(0) == ESP_OK) {
            atomic_store(&s_time_confirmed, true);
        }
        bool current_time_synced = atomic_load(&s_time_confirmed);
        if (current_time_synced && !prev_time_synced) {
            ESP_LOGI(TAG, "SNTP time synchronized");
            dispatch_event(APP_NETWORK_EVENT_TIME_SYNCED);
            prev_time_synced = true;
        } else if (!current_time_synced && prev_time_synced) {
            prev_time_synced = false;
        }
    }
}

esp_err_t app_network_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing C6 Wi-Fi network via SDIO...");
    esp_err_t ret = wifi_c6_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi_c6_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    BaseType_t task_ret = xTaskCreate(network_monitor_task,
                                      "app_net_mon",
                                      3072,
                                      NULL,
                                      tskIDLE_PRIORITY + 2,
                                      &s_monitor_task);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create network monitor task");
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Network initialized, C6 firmware version: %s", wifi_c6_cp_version());

#if defined(CONFIG_HCP_WIFI_LOCAL_OVERRIDE) && CONFIG_HCP_WIFI_LOCAL_OVERRIDE && defined(HCP_LOCAL_WIFI_SSID)
    _Static_assert(sizeof(HCP_LOCAL_WIFI_SSID) > 1 && sizeof(HCP_LOCAL_WIFI_SSID) <= 33,
                   "Local Wi-Fi SSID has invalid length");
    _Static_assert(sizeof(HCP_LOCAL_WIFI_PASSWORD) <= 65,
                   "Local Wi-Fi password is too long");
    /* Nap lai ca mat khau khi SSID da ton tai trong NVS. */
    ESP_LOGI(TAG, "Ket noi Wi-Fi theo cau hinh local");
    log_wifi_request("local", HCP_LOCAL_WIFI_SSID, HCP_LOCAL_WIFI_PASSWORD);
    ret = wifi_c6_connect(HCP_LOCAL_WIFI_SSID, HCP_LOCAL_WIFI_PASSWORD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Local Wi-Fi connection request failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Local Wi-Fi request accepted; waiting for connection/IP");
#endif

    return ESP_OK;
}

bool app_network_is_initialized(void)
{
    return atomic_load(&s_initialized);
}

esp_err_t app_network_register_event_cb(app_network_event_cb_t cb, void *ctx)
{
    if (!cb) return ESP_ERR_INVALID_ARG;
    if (s_listener_count >= MAX_EVENT_LISTENERS) {
        return ESP_ERR_NO_MEM;
    }

    s_listeners[s_listener_count].cb = cb;
    s_listeners[s_listener_count].ctx = ctx;
    s_listener_count++;
    return ESP_OK;
}

wifi_c6_state_t app_network_get_state(void)
{
    return wifi_c6_state();
}

bool app_network_is_connected(void)
{
    return (wifi_c6_state() == WIFI_C6_CONNECTED);
}

bool app_network_get_ip(char *buf, size_t len)
{
    return wifi_c6_ip(buf, (int)len);
}

int app_network_get_rssi(void)
{
    return wifi_c6_rssi();
}

bool app_network_get_ssid(char *buf, size_t len)
{
    return wifi_c6_saved_ssid(buf, (int)len);
}

bool app_network_is_time_synced(void)
{
    return atomic_load(&s_time_confirmed);
}

void app_network_retry_time_sync(void)
{
    atomic_store(&s_retry_time, true);
}

esp_err_t app_network_connect(const char *ssid, const char *pass)
{
    log_wifi_request("runtime", ssid, pass);
    esp_err_t ret = wifi_c6_connect(ssid, pass);
    ESP_LOGI(TAG, "Runtime Wi-Fi request result: %s", esp_err_to_name(ret));
    return ret;
}

void app_network_forget(void)
{
    wifi_c6_forget();
}

esp_err_t app_network_scan(wifi_c6_ap_t *list, int max, int *n)
{
    return wifi_c6_scan(list, max, n);
}

const char *app_network_get_cp_version(void)
{
    return wifi_c6_cp_version();
}
