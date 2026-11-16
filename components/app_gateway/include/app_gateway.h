#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEV_MAX        64
#define ROOM_MAX       16
#define SCENE_MAX      24
#define ENDPOINT_MAX   8
#define TRAIT_MAX      12

/* Trang thai vong doi cua Home Center */
typedef enum {
    GW_STATE_DISCONNECTED = 0,
    GW_STATE_AUTHENTICATING,      /* Dang lay access token qua REST */
    GW_STATE_FETCHING_CONFIG,     /* Dang lay config broker MQTT qua REST */
    GW_STATE_MQTT_CONNECTING,     /* Dang ket noi MQTT broker (TLS 443 ALPN) */
    GW_STATE_UNLINKED_PAIRING,    /* Chua lien ket: dang mo phien QR pairing */
    GW_STATE_LINKED_RUNNING,      /* Da lien ket: san sang dieu khien, giam sat */
    GW_STATE_ERROR
} app_gw_state_t;

/* Mo hinh Trait theo AsyncAPI NobleX */
typedef enum {
    TRAIT_TYPE_ON_OFF = 0,
    TRAIT_TYPE_BRIGHTNESS,
    TRAIT_TYPE_COLOR_TEMP,
    TRAIT_TYPE_POSITION,
    TRAIT_TYPE_COVER_MOTION,
    TRAIT_TYPE_THERMOSTAT_SETPOINT,
    TRAIT_TYPE_THERMOSTAT_MODE,
    TRAIT_TYPE_FAN_SPEED,
    /* Cac trait cam bien chi doc */
    TRAIT_TYPE_TEMPERATURE,
    TRAIT_TYPE_HUMIDITY,
    TRAIT_TYPE_ILLUMINANCE,
    TRAIT_TYPE_CONTACT,
    TRAIT_TYPE_OCCUPANCY,
    TRAIT_TYPE_BATTERY,
    TRAIT_TYPE_POWER_METER,
    TRAIT_TYPE_UNKNOWN
} app_trait_type_t;

typedef struct {
    app_trait_type_t type;
    char name[32];
    bool writable;
    float min_val;
    float max_val;
    float step;
    char unit[16];
    /* Gia tri hien tai */
    union {
        bool b_val;
        float num_val;
        char str_val[32];
    } state;
    bool state_valid;
} app_trait_t;

typedef struct {
    char id[16];                  /* "1", "2", ... */
    char name[32];
    uint8_t trait_count;
    app_trait_t traits[TRAIT_MAX];
} app_endpoint_t;

typedef struct {
    char id[64];                  /* "dv_..." */
    char name[48];
    char room_id[64];             /* "rm_..." */
    char category[32];            /* "switch", "light", "cover", "climate", "sensor" */
    bool online;
    uint8_t endpoint_count;
    app_endpoint_t endpoints[ENDPOINT_MAX];
} app_device_t;

typedef struct {
    char id[64];                  /* "rm_..." */
    char name[48];
} app_room_t;

typedef struct {
    char id[64];                  /* "sc_..." */
    char name[48];
} app_scene_t;

typedef struct {
    int64_t revision;
    char home_id[64];             /* "hm_..." */
    char home_name[48];
    uint16_t room_count;
    app_room_t rooms[ROOM_MAX];
    uint16_t device_count;
    app_device_t devices[DEV_MAX];
    uint16_t scene_count;
    app_scene_t scenes[SCENE_MAX];
} app_snapshot_t;

/* Thong tin phien QR Pairing */
typedef struct {
    char token[128];
    char pair_url[256];
    int64_t expires_at_ms;
    int remaining_sec;
    bool active;
    bool denied;
    bool retry_available;       /* Trang thai runtime, khong luu NVS */
} app_pairing_session_t;

/**
 * @brief Khoi tao Gateway Subsystem & doc cache tu /spiffs/snapshot.json (Offline-First)
 */
esp_err_t app_gateway_init(void);

/**
 * @brief Bat dau vong doi ket noi Cloud (goi sau khi mang Wi-Fi co IP)
 */
esp_err_t app_gateway_start(void);

/**
 * @brief Lay trang thai vong doi hien tai
 */
app_gw_state_t app_gateway_get_state(void);

/**
 * @brief Lay con tro Snapshot nha (luu tren PSRAM)
 */
const app_snapshot_t *app_gateway_get_snapshot(void);
bool app_gateway_has_snapshot(void);

/**
 * @brief Lay thong tin phien QR hien tai
 */
const app_pairing_session_t *app_gateway_get_pairing_session(void);

/**
 * @brief Yeu cau tao phien QR moi (pairing.create)
 */
esp_err_t app_gateway_create_pairing(void);

/**
 * @brief Tu go lien ket (unlink)
 */
esp_err_t app_gateway_unlink(void);

/**
 * @brief Gui lenh dieu khien thiet bi theo Trait (device.command)
 */
esp_err_t app_gateway_send_command(const char *device_id, const char *endpoint_id, const char *trait, const char *value_json);

/**
 * @brief Kich hoat ngu canh (scene.run)
 */
esp_err_t app_gateway_run_scene(const char *scene_id);

/**
 * @brief Dang ky callback khi Snapshot hoac trang thai thiet bi thay doi
 */
typedef void (*app_gateway_update_cb_t)(void);
void app_gateway_register_update_cb(app_gateway_update_cb_t cb);

#ifdef __cplusplus
}
#endif
