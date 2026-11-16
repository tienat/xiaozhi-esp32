#include "app_gateway_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "app_spiffs.h"

static const char *TAG = "app_gw_parser";

#define DEDUP_CACHE_SIZE 32
static char s_recent_msg_ids[DEDUP_CACHE_SIZE][64];
static size_t s_dedup_idx = 0;

static bool is_duplicate_msg(const char *msg_id)
{
    if (!msg_id || strlen(msg_id) == 0) return false;
    for (size_t i = 0; i < DEDUP_CACHE_SIZE; i++) {
        if (strcmp(s_recent_msg_ids[i], msg_id) == 0) {
            return true;
        }
    }
    snprintf(s_recent_msg_ids[s_dedup_idx], sizeof(s_recent_msg_ids[0]), "%s", msg_id);
    s_dedup_idx = (s_dedup_idx + 1) % DEDUP_CACHE_SIZE;
    return false;
}

app_trait_type_t app_gateway_trait_type_from_str(const char *name)
{
    if (!name) return TRAIT_TYPE_UNKNOWN;
    if (strcmp(name, "on_off") == 0) return TRAIT_TYPE_ON_OFF;
    if (strcmp(name, "brightness") == 0) return TRAIT_TYPE_BRIGHTNESS;
    if (strcmp(name, "color_temperature") == 0) return TRAIT_TYPE_COLOR_TEMP;
    if (strcmp(name, "position") == 0) return TRAIT_TYPE_POSITION;
    if (strcmp(name, "cover_motion") == 0) return TRAIT_TYPE_COVER_MOTION;
    if (strcmp(name, "thermostat_setpoint") == 0) return TRAIT_TYPE_THERMOSTAT_SETPOINT;
    if (strcmp(name, "thermostat_mode") == 0) return TRAIT_TYPE_THERMOSTAT_MODE;
    if (strcmp(name, "fan_speed") == 0) return TRAIT_TYPE_FAN_SPEED;
    if (strcmp(name, "temperature") == 0) return TRAIT_TYPE_TEMPERATURE;
    if (strcmp(name, "humidity") == 0) return TRAIT_TYPE_HUMIDITY;
    if (strcmp(name, "illuminance") == 0) return TRAIT_TYPE_ILLUMINANCE;
    if (strcmp(name, "contact") == 0) return TRAIT_TYPE_CONTACT;
    if (strcmp(name, "occupancy") == 0) return TRAIT_TYPE_OCCUPANCY;
    if (strcmp(name, "battery") == 0) return TRAIT_TYPE_BATTERY;
    if (strcmp(name, "power_meter") == 0) return TRAIT_TYPE_POWER_METER;
    return TRAIT_TYPE_UNKNOWN;
}

const char *app_gateway_trait_type_to_str(app_trait_type_t type)
{
    switch (type) {
    case TRAIT_TYPE_ON_OFF: return "on_off";
    case TRAIT_TYPE_BRIGHTNESS: return "brightness";
    case TRAIT_TYPE_COLOR_TEMP: return "color_temperature";
    case TRAIT_TYPE_POSITION: return "position";
    case TRAIT_TYPE_COVER_MOTION: return "cover_motion";
    case TRAIT_TYPE_THERMOSTAT_SETPOINT: return "thermostat_setpoint";
    case TRAIT_TYPE_THERMOSTAT_MODE: return "thermostat_mode";
    case TRAIT_TYPE_FAN_SPEED: return "fan_speed";
    case TRAIT_TYPE_TEMPERATURE: return "temperature";
    case TRAIT_TYPE_HUMIDITY: return "humidity";
    case TRAIT_TYPE_ILLUMINANCE: return "illuminance";
    case TRAIT_TYPE_CONTACT: return "contact";
    case TRAIT_TYPE_OCCUPANCY: return "occupancy";
    case TRAIT_TYPE_BATTERY: return "battery";
    case TRAIT_TYPE_POWER_METER: return "power_meter";
    default: return "unknown";
    }
}

static void parse_trait_state(app_trait_t *trait, cJSON *state_val)
{
    if (!state_val) {
        trait->state_valid = false;
        return;
    }
    trait->state_valid = true;
    if (cJSON_IsBool(state_val)) {
        trait->state.b_val = cJSON_IsTrue(state_val);
    } else if (cJSON_IsNumber(state_val)) {
        trait->state.num_val = (float)state_val->valuedouble;
    } else if (cJSON_IsString(state_val) && state_val->valuestring) {
        strncpy(trait->state.str_val, state_val->valuestring, sizeof(trait->state.str_val) - 1);
    } else {
        trait->state_valid = false;
    }
}

static const char *json_kind(const cJSON *item)
{
    if (!item) return "missing";
    if (cJSON_IsArray(item)) return "array";
    if (cJSON_IsObject(item)) return "object";
    if (cJSON_IsString(item)) return "string";
    if (cJSON_IsNumber(item)) return "number";
    if (cJSON_IsBool(item)) return "boolean";
    if (cJSON_IsNull(item)) return "null";
    return "other";
}

static bool string_field(const cJSON *obj, const char *key, size_t capacity)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(value) && value->valuestring &&
           (!capacity || (value->valuestring[0] && strlen(value->valuestring) < capacity));
}

static esp_err_t validate_snapshot(cJSON *data)
{
    cJSON *rooms = cJSON_GetObjectItemCaseSensitive(data, "rooms");
    cJSON *devices = cJSON_GetObjectItemCaseSensitive(data, "devices");
    cJSON *scenes = cJSON_GetObjectItemCaseSensitive(data, "scenes");
    cJSON *home = cJSON_GetObjectItemCaseSensitive(data, "home");
    cJSON *rev = cJSON_GetObjectItemCaseSensitive(data, "revision");
    cJSON *weather = cJSON_GetObjectItemCaseSensitive(data, "weather");
    ESP_LOGI(TAG, "Snapshot input: rooms=%s/%d devices=%s/%d scenes=%s/%d revision=%s home=%s weather=%s",
             json_kind(rooms), cJSON_IsArray(rooms) ? cJSON_GetArraySize(rooms) : -1,
             json_kind(devices), cJSON_IsArray(devices) ? cJSON_GetArraySize(devices) : -1,
             json_kind(scenes), cJSON_IsArray(scenes) ? cJSON_GetArraySize(scenes) : -1,
             json_kind(rev), json_kind(home), json_kind(weather));
    if (!cJSON_IsObject(data) || !cJSON_IsNumber(rev) || !isfinite(rev->valuedouble) ||
        rev->valuedouble < 0 || rev->valuedouble >= 9223372036854775808.0 ||
        floor(rev->valuedouble) != rev->valuedouble || !cJSON_IsObject(home) ||
        !string_field(home, "id", sizeof(((app_snapshot_t *)0)->home_id)) || !string_field(home, "name", 0) ||
        !cJSON_IsArray(rooms) || !cJSON_IsArray(devices) || !cJSON_IsArray(scenes) ||
        (!cJSON_IsNull(weather) && !cJSON_IsObject(weather))) return ESP_ERR_INVALID_RESPONSE;
    if (cJSON_IsObject(weather) &&
        (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(weather, "temp_c")) ||
         !string_field(weather, "condition", 0) || !string_field(weather, "updated_at", 0))) return ESP_ERR_INVALID_RESPONSE;
    if (cJSON_GetArraySize(rooms) > ROOM_MAX || cJSON_GetArraySize(devices) > DEV_MAX ||
        cJSON_GetArraySize(scenes) > SCENE_MAX) return ESP_ERR_INVALID_SIZE;
    cJSON *node;
    cJSON_ArrayForEach(node, rooms) {
        if (!cJSON_IsObject(node) || !string_field(node, "id", sizeof(((app_room_t *)0)->id)) ||
            !string_field(node, "name", 0)) return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON_ArrayForEach(node, scenes) {
        if (!cJSON_IsObject(node) || !string_field(node, "id", sizeof(((app_scene_t *)0)->id)) ||
            !string_field(node, "name", 0)) return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON_ArrayForEach(node, devices) {
        cJSON *room = cJSON_GetObjectItemCaseSensitive(node, "room_id");
        cJSON *endpoints = cJSON_GetObjectItemCaseSensitive(node, "endpoints");
        if (!cJSON_IsObject(node) || !string_field(node, "id", sizeof(((app_device_t *)0)->id)) ||
            !string_field(node, "name", 0) || !string_field(node, "category", 0) ||
            (!cJSON_IsNull(room) && !(cJSON_IsString(room) && room->valuestring &&
              strlen(room->valuestring) < sizeof(((app_device_t *)0)->room_id))) ||
            !cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(node, "online")) ||
            !cJSON_IsArray(endpoints) || cJSON_GetArraySize(endpoints) < 1) return ESP_ERR_INVALID_RESPONSE;
        if (cJSON_GetArraySize(endpoints) > ENDPOINT_MAX) return ESP_ERR_INVALID_SIZE;
        cJSON *ep;
        cJSON_ArrayForEach(ep, endpoints) {
            cJSON *traits = cJSON_GetObjectItemCaseSensitive(ep, "traits");
            if (!cJSON_IsObject(ep) || !string_field(ep, "id", sizeof(((app_endpoint_t *)0)->id)) ||
                !string_field(ep, "name", 0) || !cJSON_IsObject(traits) ||
                !cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(ep, "state"))) return ESP_ERR_INVALID_RESPONSE;
            int known = 0;
            cJSON *trait;
            cJSON_ArrayForEach(trait, traits) {
                if (app_gateway_trait_type_from_str(trait->string) == TRAIT_TYPE_UNKNOWN) continue;
                if (++known > TRAIT_MAX) return ESP_ERR_INVALID_SIZE;
                if (!cJSON_IsObject(trait) || !cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(trait, "writable")))
                    return ESP_ERR_INVALID_RESPONSE;
                const char *numbers[] = {"min", "max", "step"};
                for (size_t i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++) {
                    cJSON *n = cJSON_GetObjectItemCaseSensitive(trait, numbers[i]);
                    if (n && (!cJSON_IsNumber(n) || !isfinite(n->valuedouble))) return ESP_ERR_INVALID_RESPONSE;
                }
                cJSON *unit = cJSON_GetObjectItemCaseSensitive(trait, "unit");
                if (unit && !cJSON_IsString(unit)) return ESP_ERR_INVALID_RESPONSE;
                cJSON *options = cJSON_GetObjectItemCaseSensitive(trait, "options");
                if (options && !cJSON_IsArray(options)) return ESP_ERR_INVALID_RESPONSE;
                cJSON *option;
                cJSON_ArrayForEach(option, options) {
                    if (!cJSON_IsString(option)) return ESP_ERR_INVALID_RESPONSE;
                }
            }
        }
    }
    return ESP_OK;
}

static esp_err_t decode_snapshot_json(cJSON *data_obj, app_snapshot_t *snapshot)
{
    if (!data_obj || !snapshot) return ESP_ERR_INVALID_ARG;

    memset(snapshot, 0, sizeof(app_snapshot_t));

    /* Revision */
    cJSON *rev = cJSON_GetObjectItem(data_obj, "revision");
    if (cJSON_IsNumber(rev)) {
        snapshot->revision = (int64_t)rev->valuedouble;
    }

    /* Home */
    cJSON *home = cJSON_GetObjectItem(data_obj, "home");
    if (home) {
        cJSON *hid = cJSON_GetObjectItem(home, "id");
        if (cJSON_IsString(hid) && hid->valuestring) {
            strncpy(snapshot->home_id, hid->valuestring, sizeof(snapshot->home_id) - 1);
        }
        cJSON *hname = cJSON_GetObjectItem(home, "name");
        if (cJSON_IsString(hname) && hname->valuestring) {
            strncpy(snapshot->home_name, hname->valuestring, sizeof(snapshot->home_name) - 1);
        }
    }

    /* Rooms */
    cJSON *rooms = cJSON_GetObjectItem(data_obj, "rooms");
    if (cJSON_IsArray(rooms)) {
        int r_count = cJSON_GetArraySize(rooms);
        if (r_count > ROOM_MAX) r_count = ROOM_MAX;
        snapshot->room_count = r_count;
        for (int i = 0; i < r_count; i++) {
            cJSON *r = cJSON_GetArrayItem(rooms, i);
            cJSON *rid = cJSON_GetObjectItem(r, "id");
            cJSON *rname = cJSON_GetObjectItem(r, "name");
            if (cJSON_IsString(rid) && rid->valuestring) {
                strncpy(snapshot->rooms[i].id, rid->valuestring, sizeof(snapshot->rooms[i].id) - 1);
            }
            if (cJSON_IsString(rname) && rname->valuestring) {
                strncpy(snapshot->rooms[i].name, rname->valuestring, sizeof(snapshot->rooms[i].name) - 1);
            }
        }
    }

    /* Scenes */
    cJSON *scenes = cJSON_GetObjectItem(data_obj, "scenes");
    if (cJSON_IsArray(scenes)) {
        int s_count = cJSON_GetArraySize(scenes);
        if (s_count > SCENE_MAX) s_count = SCENE_MAX;
        snapshot->scene_count = s_count;
        for (int i = 0; i < s_count; i++) {
            cJSON *s = cJSON_GetArrayItem(scenes, i);
            cJSON *sid = cJSON_GetObjectItem(s, "id");
            cJSON *sname = cJSON_GetObjectItem(s, "name");
            if (cJSON_IsString(sid) && sid->valuestring) {
                strncpy(snapshot->scenes[i].id, sid->valuestring, sizeof(snapshot->scenes[i].id) - 1);
            }
            if (cJSON_IsString(sname) && sname->valuestring) {
                strncpy(snapshot->scenes[i].name, sname->valuestring, sizeof(snapshot->scenes[i].name) - 1);
            }
        }
    }

    /* Devices */
    cJSON *devices = cJSON_GetObjectItem(data_obj, "devices");
    if (cJSON_IsArray(devices)) {
        int d_count = cJSON_GetArraySize(devices);
        if (d_count > DEV_MAX) d_count = DEV_MAX;
        snapshot->device_count = d_count;

        for (int i = 0; i < d_count; i++) {
            cJSON *dev_node = cJSON_GetArrayItem(devices, i);
            app_device_t *dev = &snapshot->devices[i];

            cJSON *did = cJSON_GetObjectItem(dev_node, "id");
            cJSON *dname = cJSON_GetObjectItem(dev_node, "name");
            cJSON *droom = cJSON_GetObjectItem(dev_node, "room_id");
            cJSON *dcat = cJSON_GetObjectItem(dev_node, "category");
            cJSON *donline = cJSON_GetObjectItem(dev_node, "online");

            if (cJSON_IsString(did) && did->valuestring) {
                strncpy(dev->id, did->valuestring, sizeof(dev->id) - 1);
            }
            if (cJSON_IsString(dname) && dname->valuestring) {
                strncpy(dev->name, dname->valuestring, sizeof(dev->name) - 1);
            }
            if (cJSON_IsString(droom) && droom->valuestring) {
                strncpy(dev->room_id, droom->valuestring, sizeof(dev->room_id) - 1);
            }
            if (cJSON_IsString(dcat) && dcat->valuestring) {
                strncpy(dev->category, dcat->valuestring, sizeof(dev->category) - 1);
            }
            dev->online = cJSON_IsTrue(donline);

            /* Endpoints */
            cJSON *endpoints = cJSON_GetObjectItem(dev_node, "endpoints");
            if (cJSON_IsArray(endpoints)) {
                int ep_count = cJSON_GetArraySize(endpoints);
                if (ep_count > ENDPOINT_MAX) ep_count = ENDPOINT_MAX;
                dev->endpoint_count = ep_count;

                for (int j = 0; j < ep_count; j++) {
                    cJSON *ep_node = cJSON_GetArrayItem(endpoints, j);
                    app_endpoint_t *ep = &dev->endpoints[j];

                    cJSON *epid = cJSON_GetObjectItem(ep_node, "id");
                    cJSON *epname = cJSON_GetObjectItem(ep_node, "name");
                    if (cJSON_IsString(epid) && epid->valuestring) {
                        strncpy(ep->id, epid->valuestring, sizeof(ep->id) - 1);
                    }
                    if (cJSON_IsString(epname) && epname->valuestring) {
                        strncpy(ep->name, epname->valuestring, sizeof(ep->name) - 1);
                    }

                    cJSON *traits = cJSON_GetObjectItem(ep_node, "traits");
                    cJSON *states = cJSON_GetObjectItem(ep_node, "state");

                    if (cJSON_IsObject(traits)) {
                        cJSON *trait_item = traits->child;
                        uint8_t t_idx = 0;
                        while (trait_item && t_idx < TRAIT_MAX) {
                            if (app_gateway_trait_type_from_str(trait_item->string) == TRAIT_TYPE_UNKNOWN) {
                                trait_item = trait_item->next;
                                continue;
                            }
                            app_trait_t *t = &ep->traits[t_idx];
                            strncpy(t->name, trait_item->string, sizeof(t->name) - 1);
                            t->type = app_gateway_trait_type_from_str(trait_item->string);

                            cJSON *w = cJSON_GetObjectItem(trait_item, "writable");
                            t->writable = cJSON_IsTrue(w);
                            cJSON *min_v = cJSON_GetObjectItem(trait_item, "min");
                            if (cJSON_IsNumber(min_v)) t->min_val = (float)min_v->valuedouble;
                            cJSON *max_v = cJSON_GetObjectItem(trait_item, "max");
                            if (cJSON_IsNumber(max_v)) t->max_val = (float)max_v->valuedouble;
                            cJSON *st = cJSON_GetObjectItem(trait_item, "step");
                            if (cJSON_IsNumber(st)) t->step = (float)st->valuedouble;
                            cJSON *u = cJSON_GetObjectItem(trait_item, "unit");
                            if (cJSON_IsString(u) && u->valuestring) {
                                strncpy(t->unit, u->valuestring, sizeof(t->unit) - 1);
                            }

                            /* Match current state if present */
                            if (cJSON_IsObject(states)) {
                                cJSON *s_val = cJSON_GetObjectItem(states, trait_item->string);
                                parse_trait_state(t, s_val);
                            } else {
                                t->state_valid = false;
                            }

                            t_idx++;
                            trait_item = trait_item->next;
                        }
                        ep->trait_count = t_idx;
                    }
                }
            }
        }
    }

    ESP_LOGI(TAG, "Parsed snapshot: rev=%lld, home='%s', rooms=%d, devices=%d, scenes=%d",
             (long long)snapshot->revision, snapshot->home_name,
             snapshot->room_count, snapshot->device_count, snapshot->scene_count);
    return ESP_OK;
}

esp_err_t app_gateway_parser_parse_snapshot_json(cJSON *data_obj, app_snapshot_t *snapshot)
{
    if (!data_obj || !snapshot) return ESP_ERR_INVALID_ARG;
    esp_err_t err = validate_snapshot(data_obj);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Snapshot rejected before model update: %s", esp_err_to_name(err));
        return err;
    }
    app_snapshot_t *next = heap_caps_calloc(1, sizeof(*next), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!next) return ESP_ERR_NO_MEM;
    err = decode_snapshot_json(data_obj, next);
    if (err == ESP_OK) memcpy(snapshot, next, sizeof(*snapshot));
    free(next);
    return err;
}

static cJSON *serialize_snapshot_json(const app_snapshot_t *snapshot)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddNumberToObject(root, "revision", (double)snapshot->revision);
    cJSON_AddNullToObject(root, "weather");

    cJSON *home = cJSON_CreateObject();
    cJSON_AddStringToObject(home, "id", snapshot->home_id);
    cJSON_AddStringToObject(home, "name", snapshot->home_name);
    cJSON_AddItemToObject(root, "home", home);

    cJSON *rooms = cJSON_CreateArray();
    for (int i = 0; i < snapshot->room_count; i++) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "id", snapshot->rooms[i].id);
        cJSON_AddStringToObject(r, "name", snapshot->rooms[i].name);
        cJSON_AddItemToArray(rooms, r);
    }
    cJSON_AddItemToObject(root, "rooms", rooms);

    cJSON *scenes = cJSON_CreateArray();
    for (int i = 0; i < snapshot->scene_count; i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "id", snapshot->scenes[i].id);
        cJSON_AddStringToObject(s, "name", snapshot->scenes[i].name);
        cJSON_AddItemToArray(scenes, s);
    }
    cJSON_AddItemToObject(root, "scenes", scenes);

    cJSON *devices = cJSON_CreateArray();
    for (int i = 0; i < snapshot->device_count; i++) {
        const app_device_t *dev = &snapshot->devices[i];
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "id", dev->id);
        cJSON_AddStringToObject(d, "name", dev->name);
        cJSON_AddStringToObject(d, "room_id", dev->room_id);
        cJSON_AddStringToObject(d, "category", dev->category);
        cJSON_AddBoolToObject(d, "online", dev->online);

        cJSON *endpoints = cJSON_CreateArray();
        for (int j = 0; j < dev->endpoint_count; j++) {
            const app_endpoint_t *ep = &dev->endpoints[j];
            cJSON *e = cJSON_CreateObject();
            cJSON_AddStringToObject(e, "id", ep->id);
            cJSON_AddStringToObject(e, "name", ep->name);

            cJSON *traits = cJSON_CreateObject();
            cJSON *state = cJSON_CreateObject();
            for (int k = 0; k < ep->trait_count; k++) {
                const app_trait_t *t = &ep->traits[k];
                cJSON *tr = cJSON_CreateObject();
                cJSON_AddBoolToObject(tr, "writable", t->writable);
                if (t->min_val != 0 || t->max_val != 0) {
                    cJSON_AddNumberToObject(tr, "min", t->min_val);
                    cJSON_AddNumberToObject(tr, "max", t->max_val);
                    cJSON_AddNumberToObject(tr, "step", t->step);
                }
                if (strlen(t->unit) > 0) {
                    cJSON_AddStringToObject(tr, "unit", t->unit);
                }
                cJSON_AddItemToObject(traits, t->name, tr);

                if (t->state_valid) {
                    if (t->type == TRAIT_TYPE_ON_OFF) {
                        cJSON_AddBoolToObject(state, t->name, t->state.b_val);
                    } else if (t->type == TRAIT_TYPE_COVER_MOTION ||
                               t->type == TRAIT_TYPE_THERMOSTAT_MODE ||
                               t->type == TRAIT_TYPE_CONTACT ||
                               t->type == TRAIT_TYPE_OCCUPANCY) {
                        cJSON_AddStringToObject(state, t->name, t->state.str_val);
                    } else {
                        cJSON_AddNumberToObject(state, t->name, t->state.num_val);
                    }
                }
            }
            cJSON_AddItemToObject(e, "traits", traits);
            cJSON_AddItemToObject(e, "state", state);
            cJSON_AddItemToArray(endpoints, e);
        }
        cJSON_AddItemToObject(d, "endpoints", endpoints);
        cJSON_AddItemToArray(devices, d);
    }
    cJSON_AddItemToObject(root, "devices", devices);

    return root;
}

esp_err_t app_gateway_parser_load_snapshot_file(app_snapshot_t *snapshot)
{
    if (!app_spiffs_file_exists(SNAPSHOT_FILE_PATH)) {
        return ESP_ERR_NOT_FOUND;
    }

    size_t len = 0;
    char *json_str = app_spiffs_read_file(SNAPSHOT_FILE_PATH, &len);
    if (!json_str || len == 0) {
        if (json_str) free(json_str);
        return ESP_ERR_INVALID_SIZE;
    }

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse %s", SNAPSHOT_FILE_PATH);
        return ESP_FAIL;
    }

    /* Cache cu khong co weather; chi bo sung cho cache local, khong cho wire. */
    if (cJSON_IsObject(root) && !cJSON_GetObjectItemCaseSensitive(root, "weather")) {
        cJSON_AddNullToObject(root, "weather");
    }
    esp_err_t err = app_gateway_parser_parse_snapshot_json(root, snapshot);
    cJSON_Delete(root);
    return err;
}

esp_err_t app_gateway_parser_save_snapshot_file(const app_snapshot_t *snapshot)
{
    if (!app_spiffs_is_mounted() || !snapshot) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = serialize_snapshot_json(snapshot);
    if (!root) return ESP_ERR_NO_MEM;

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return ESP_ERR_NO_MEM;

    esp_err_t err = app_spiffs_write_file(SNAPSHOT_FILE_PATH, json_str, strlen(json_str));
    free(json_str);
    return err;
}

esp_err_t app_gateway_parser_wipe_snapshot_file(void)
{
    if (!app_spiffs_is_mounted()) return ESP_OK;
    const char *empty_json = "{}";
    return app_spiffs_write_file(SNAPSHOT_FILE_PATH, empty_json, strlen(empty_json));
}

esp_err_t app_gateway_parser_parse_envelope(const char *json_str,
                                            size_t len,
                                            app_gw_parsed_msg_t *out_msg,
                                            app_snapshot_t *inout_snapshot)
{
    if (!json_str || !out_msg) return ESP_ERR_INVALID_ARG;

    memset(out_msg, 0, sizeof(app_gw_parsed_msg_t));

    cJSON *root = cJSON_ParseWithLength(json_str, len);
    if (!root) {
        ESP_LOGE(TAG, "Invalid envelope JSON");
        return ESP_FAIL;
    }

    /* 1. Protocol Version (must be 1) */
    cJSON *v = cJSON_GetObjectItem(root, "v");
    if (!cJSON_IsNumber(v) || v->valueint != 1) {
        ESP_LOGW(TAG, "Ignored unsupported protocol version: %d", v ? v->valueint : -1);
        cJSON_Delete(root);
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* 2. Message ID & Deduplication */
    cJSON *id = cJSON_GetObjectItem(root, "id");
    if (cJSON_IsString(id) && id->valuestring) {
        strncpy(out_msg->msg_id, id->valuestring, sizeof(out_msg->msg_id) - 1);
        if (is_duplicate_msg(out_msg->msg_id)) {
            ESP_LOGD(TAG, "Duplicate message ignored: %s", out_msg->msg_id);
            cJSON_Delete(root);
            return ESP_ERR_INVALID_STATE; /* Indicator of duplicate */
        }
    }

    /* 3. Timestamp */
    cJSON *ts = cJSON_GetObjectItem(root, "ts");
    if (cJSON_IsString(ts) && ts->valuestring) {
        strncpy(out_msg->ts, ts->valuestring, sizeof(out_msg->ts) - 1);
    }

    /* 4. Type & Data */
    cJSON *type = cJSON_GetObjectItem(root, "type");
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsString(type) || !type->valuestring || !data) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    const char *type_str = type->valuestring;
    ESP_LOGI(TAG, "Handling envelope type: %s", type_str);

    if (strcmp(type_str, "link.state") == 0) {
        out_msg->type = PARSER_EVT_LINK_STATE;
        cJSON *linked = cJSON_GetObjectItem(data, "linked");
        out_msg->data.link_state.linked = cJSON_IsTrue(linked);
        cJSON *home = cJSON_GetObjectItem(data, "home");
        if (home) {
            cJSON *hid = cJSON_GetObjectItem(home, "id");
            cJSON *hname = cJSON_GetObjectItem(home, "name");
            if (cJSON_IsString(hid) && hid->valuestring) {
                strncpy(out_msg->data.link_state.home_id, hid->valuestring,
                        sizeof(out_msg->data.link_state.home_id) - 1);
            }
            if (cJSON_IsString(hname) && hname->valuestring) {
                strncpy(out_msg->data.link_state.home_name, hname->valuestring,
                        sizeof(out_msg->data.link_state.home_name) - 1);
            }
        }
    } else if (strcmp(type_str, "pairing.session") == 0) {
        out_msg->type = PARSER_EVT_PAIRING_SESSION;
        cJSON *token = cJSON_GetObjectItem(data, "token");
        cJSON *p_url = cJSON_GetObjectItem(data, "pair_url");
        if (cJSON_IsString(token) && token->valuestring) {
            strncpy(out_msg->data.pairing_session.token, token->valuestring,
                    sizeof(out_msg->data.pairing_session.token) - 1);
        }
        if (cJSON_IsString(p_url) && p_url->valuestring) {
            strncpy(out_msg->data.pairing_session.pair_url, p_url->valuestring,
                    sizeof(out_msg->data.pairing_session.pair_url) - 1);
        }
        out_msg->data.pairing_session.remaining_sec = 120; /* 2 min monotonic countdown */
    } else if (strcmp(type_str, "pairing.denied") == 0) {
        out_msg->type = PARSER_EVT_PAIRING_DENIED;
    } else if (strcmp(type_str, "snapshot") == 0) {
        out_msg->type = PARSER_EVT_SNAPSHOT;
        if (inout_snapshot) {
            cJSON *home = cJSON_GetObjectItemCaseSensitive(data, "home");
            cJSON *hid = cJSON_GetObjectItemCaseSensitive(home, "id");
            cJSON *revision = cJSON_GetObjectItemCaseSensitive(data, "revision");
            if (cJSON_IsString(hid) && inout_snapshot->home_id[0] &&
                strcmp(hid->valuestring, inout_snapshot->home_id) != 0) {
                ESP_LOGW(TAG, "MQTT snapshot home mismatch");
                cJSON_Delete(root);
                return ESP_ERR_INVALID_RESPONSE;
            }
            if (cJSON_IsNumber(revision) && revision->valuedouble < (double)inout_snapshot->revision) {
                ESP_LOGW(TAG, "MQTT snapshot older than current model");
                cJSON_Delete(root);
                return ESP_ERR_INVALID_STATE;
            }
            esp_err_t err = app_gateway_parser_parse_snapshot_json(data, inout_snapshot);
            if (err != ESP_OK) {
                cJSON_Delete(root);
                return err;
            }
            err = app_gateway_parser_save_snapshot_file(inout_snapshot);
            if (err != ESP_OK) ESP_LOGW(TAG, "Snapshot applied but cache write failed: %s", esp_err_to_name(err));
        }
    } else if (strcmp(type_str, "device.state") == 0) {
        out_msg->type = PARSER_EVT_DEVICE_STATE;
        cJSON *did = cJSON_GetObjectItem(data, "device_id");
        cJSON *rev = cJSON_GetObjectItem(data, "revision");
        cJSON *online = cJSON_GetObjectItem(data, "online");
        cJSON *endpoints = cJSON_GetObjectItem(data, "endpoints");

        if (cJSON_IsString(did) && did->valuestring) {
            strncpy(out_msg->data.device_state.device_id, did->valuestring,
                    sizeof(out_msg->data.device_state.device_id) - 1);
        }
        if (cJSON_IsNumber(rev)) {
            out_msg->data.device_state.revision = (int64_t)rev->valuedouble;
        }
        out_msg->data.device_state.online = cJSON_IsTrue(online);

        if (inout_snapshot) {
            /* Drop message if revision <= current snapshot revision */
            if (out_msg->data.device_state.revision <= inout_snapshot->revision &&
                inout_snapshot->revision > 0) {
                ESP_LOGD(TAG, "Dropped older device.state (rev %lld <= %lld)",
                         (long long)out_msg->data.device_state.revision,
                         (long long)inout_snapshot->revision);
                cJSON_Delete(root);
                return ESP_OK;
            }

            /* Find target device */
            app_device_t *target_dev = NULL;
            for (int i = 0; i < inout_snapshot->device_count; i++) {
                if (strcmp(inout_snapshot->devices[i].id, out_msg->data.device_state.device_id) == 0) {
                    target_dev = &inout_snapshot->devices[i];
                    break;
                }
            }

            if (!target_dev) {
                ESP_LOGW(TAG, "Device %s not found in snapshot; needs sync",
                         out_msg->data.device_state.device_id);
                out_msg->data.device_state.needs_sync = true;
            } else {
                target_dev->online = out_msg->data.device_state.online;
                inout_snapshot->revision = out_msg->data.device_state.revision;

                /* Update endpoints state */
                if (cJSON_IsArray(endpoints)) {
                    int num_eps = cJSON_GetArraySize(endpoints);
                    for (int e = 0; e < num_eps; e++) {
                        cJSON *ep_node = cJSON_GetArrayItem(endpoints, e);
                        cJSON *epid = cJSON_GetObjectItem(ep_node, "id");
                        cJSON *state = cJSON_GetObjectItem(ep_node, "state");
                        if (!cJSON_IsString(epid) || !state) continue;

                        for (int j = 0; j < target_dev->endpoint_count; j++) {
                            if (strcmp(target_dev->endpoints[j].id, epid->valuestring) == 0) {
                                for (int k = 0; k < target_dev->endpoints[j].trait_count; k++) {
                                    app_trait_t *tr = &target_dev->endpoints[j].traits[k];
                                    cJSON *val = cJSON_GetObjectItem(state, tr->name);
                                    if (val) {
                                        parse_trait_state(tr, val);
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    } else if (strcmp(type_str, "command.result") == 0) {
        out_msg->type = PARSER_EVT_COMMAND_RESULT;
        cJSON *cid = cJSON_GetObjectItem(data, "command_id");
        cJSON *st = cJSON_GetObjectItem(data, "status");
        if (cJSON_IsString(cid) && cid->valuestring) {
            strncpy(out_msg->data.command_result.command_id, cid->valuestring,
                    sizeof(out_msg->data.command_result.command_id) - 1);
        }
        if (cJSON_IsString(st) && st->valuestring) {
            strncpy(out_msg->data.command_result.status, st->valuestring,
                    sizeof(out_msg->data.command_result.status) - 1);
        }
        cJSON *err_obj = cJSON_GetObjectItem(data, "error");
        if (err_obj) {
            cJSON *code = cJSON_GetObjectItem(err_obj, "code");
            if (cJSON_IsString(code) && code->valuestring) {
                strncpy(out_msg->data.command_result.err_code, code->valuestring,
                        sizeof(out_msg->data.command_result.err_code) - 1);
            }
        }
    } else if (strcmp(type_str, "grant.revoked") == 0) {
        out_msg->type = PARSER_EVT_GRANT_REVOKED;
        cJSON *reason = cJSON_GetObjectItem(data, "reason");
        if (cJSON_IsString(reason) && reason->valuestring) {
            strncpy(out_msg->data.grant_revoked.reason, reason->valuestring,
                    sizeof(out_msg->data.grant_revoked.reason) - 1);
        }
        if (inout_snapshot) {
            memset(inout_snapshot, 0, sizeof(app_snapshot_t));
            app_gateway_parser_wipe_snapshot_file();
        }
    } else {
        out_msg->type = PARSER_EVT_UNKNOWN;
        ESP_LOGD(TAG, "Ignored unknown envelope type: %s", type_str);
    }

    cJSON_Delete(root);
    return ESP_OK;
}
