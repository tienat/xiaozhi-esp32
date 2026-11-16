#pragma once

#include "app_gateway.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SNAPSHOT_FILE_PATH "/spiffs/snapshot.json"

typedef enum {
    PARSER_EVT_NONE = 0,
    PARSER_EVT_LINK_STATE,
    PARSER_EVT_PAIRING_SESSION,
    PARSER_EVT_PAIRING_DENIED,
    PARSER_EVT_SNAPSHOT,
    PARSER_EVT_DEVICE_STATE,
    PARSER_EVT_COMMAND_RESULT,
    PARSER_EVT_GRANT_REVOKED,
    PARSER_EVT_UNKNOWN
} app_gw_parser_event_type_t;

typedef struct {
    app_gw_parser_event_type_t type;
    char msg_id[64];
    char ts[32];
    union {
        struct {
            bool linked;
            char home_id[64];
            char home_name[48];
        } link_state;
        struct {
            char token[128];
            char pair_url[256];
            int64_t expires_at_ms;
            int remaining_sec;
        } pairing_session;
        struct {
            char device_id[64];
            int64_t revision;
            bool online;
            bool needs_sync; /* True if device is unknown to current snapshot */
        } device_state;
        struct {
            char command_id[64];
            char status[16]; /* "accepted", "rejected", "failed" */
            char err_code[32];
        } command_result;
        struct {
            char reason[32];
        } grant_revoked;
    } data;
} app_gw_parsed_msg_t;

/**
 * @brief Parse an incoming JSON envelope from MQTT down topic.
 *        Checks version (v == 1) and deduplicates message ID.
 */
esp_err_t app_gateway_parser_parse_envelope(const char *json_str,
                                            size_t len,
                                            app_gw_parsed_msg_t *out_msg,
                                            app_snapshot_t *inout_snapshot);

/**
 * @brief Parse snapshot JSON object and populate app_snapshot_t struct.
 */
esp_err_t app_gateway_parser_parse_snapshot_json(cJSON *data_obj, app_snapshot_t *snapshot);

/**
 * @brief Load snapshot from /spiffs/snapshot.json (Offline-First).
 */
esp_err_t app_gateway_parser_load_snapshot_file(app_snapshot_t *snapshot);

/**
 * @brief Save snapshot atomically to /spiffs/snapshot.json.
 */
esp_err_t app_gateway_parser_save_snapshot_file(const app_snapshot_t *snapshot);

/**
 * @brief Wipe snapshot file from SPIFFS (e.g. upon grant.revoked or unlink).
 */
esp_err_t app_gateway_parser_wipe_snapshot_file(void);

/**
 * @brief Convert trait name string to app_trait_type_t enum.
 */
app_trait_type_t app_gateway_trait_type_from_str(const char *name);

/**
 * @brief Convert app_trait_type_t enum to trait name string.
 */
const char *app_gateway_trait_type_to_str(app_trait_type_t type);

#ifdef __cplusplus
}
#endif
