#include "app_mcp.h"
#include <esp_log.h>
#include <string.h>
#include <strings.h>
#include <algorithm>
#include <cctype>

#define TAG "app_mcp"

extern "C" void app_mcp_init(void)
{
    AppMcpBridge::GetInstance().Initialize();
}

AppMcpBridge& AppMcpBridge::GetInstance()
{
    static AppMcpBridge instance;
    return instance;
}

AppMcpBridge::AppMcpBridge()
    : tools_registered_(false)
{
}

void AppMcpBridge::Initialize()
{
    if (tools_registered_) {
        ESP_LOGW(TAG, "AppMcpBridge da khoi tao truoc do");
        return;
    }

    /* Dang ky callback khi Gateway cap nhat snapshot/trang thai thiet bi */
    app_gateway_register_update_cb([]() {
        ESP_LOGI(TAG, "Gateway snapshot cap nhat, san sang cho cac lenh MCP tiep theo");
    });

    RegisterDynamicTools();
    tools_registered_ = true;
    ESP_LOGI(TAG, "AppMcpBridge khoi tao thanh cong va da dang ky 7 cong cu MCP");
}

/* Helper tim thiet bi theo ID hoac ten (khong phan biet hoa thuong, ho tro chuoi con) */
static const app_device_t* FindDevice(const app_snapshot_t* snap, const std::string& query)
{
    if (!snap || query.empty()) return nullptr;

    /* 1. Tim theo ID chinh xac */
    for (uint16_t i = 0; i < snap->device_count; i++) {
        if (query == snap->devices[i].id) {
            return &snap->devices[i];
        }
    }

    /* 2. Tim theo ten chinh xac (khong phan biet hoa thuong) */
    for (uint16_t i = 0; i < snap->device_count; i++) {
        if (strcasecmp(query.c_str(), snap->devices[i].name) == 0) {
            return &snap->devices[i];
        }
    }

    /* 3. Tim theo ten chua tu khoa */
    for (uint16_t i = 0; i < snap->device_count; i++) {
        if (strcasestr(snap->devices[i].name, query.c_str()) != nullptr) {
            return &snap->devices[i];
        }
    }

    return nullptr;
}

/* Helper tim kich ban theo ID hoac ten */
static const app_scene_t* FindScene(const app_snapshot_t* snap, const std::string& query)
{
    if (!snap || query.empty()) return nullptr;

    /* 1. Tim theo ID chinh xac */
    for (uint16_t i = 0; i < snap->scene_count; i++) {
        if (query == snap->scenes[i].id) {
            return &snap->scenes[i];
        }
    }

    /* 2. Tim theo ten chinh xac hoac chuoi con */
    for (uint16_t i = 0; i < snap->scene_count; i++) {
        if (strcasecmp(query.c_str(), snap->scenes[i].name) == 0 ||
            strcasestr(snap->scenes[i].name, query.c_str()) != nullptr) {
            return &snap->scenes[i];
        }
    }

    return nullptr;
}

/* Helper lay ten phong theo room_id */
static const char* GetRoomName(const app_snapshot_t* snap, const char* room_id)
{
    if (!snap || !room_id) return "";
    for (uint16_t i = 0; i < snap->room_count; i++) {
        if (strcmp(snap->rooms[i].id, room_id) == 0) {
            return snap->rooms[i].name;
        }
    }
    return "";
}

void AppMcpBridge::RegisterDynamicTools()
{
    auto& mcp = McpServer::GetInstance();

    /* 1. home.get_device_list */
    mcp.AddTool(
        "home.get_device_list",
        "Lay danh sach toan bo thiet bi, phong va ngu canh (scenes) trong nha tu NobleX Gateway.\n"
        "Tra ve JSON chua device_id, ten, phong, category va cac traits co the dieu khien.\n"
        "Goi cong cu nay khi can tra cuu ID, vi tri hoac danh muc thiet bi trong nha.",
        PropertyList(),
        [this](const PropertyList& props) -> ToolResult {
            return HandleGetDeviceList(props);
        }
    );

    /* 2. home.control_switch */
    mcp.AddTool(
        "home.control_switch",
        "Dieu khien bat hoac tat cong tac, o cam hoac thiet bi on_off.\n"
        "Tham so:\n"
        "- device_id: ID thiet bi (dv_...) hoac ten thiet bi (vi du 'Cong tac den', 'Binh nong lanh')\n"
        "- state: true de bat, false de tat\n"
        "- endpoint_id: Ma endpoint (mac dinh '1')",
        PropertyList({
            Property("device_id", kPropertyTypeString),
            Property("state", kPropertyTypeBoolean),
            Property("endpoint_id", kPropertyTypeString, std::string("1"))
        }),
        [this](const PropertyList& props) -> ToolResult {
            return HandleSwitchControl(props);
        }
    );

    /* 3. home.control_light */
    mcp.AddTool(
        "home.control_light",
        "Dieu khien den chieu sang thong minh: bat/tat, do sang (0-100%), hoac nhiet do mau (1000-10000K).\n"
        "Tham so:\n"
        "- device_id: ID den hoac ten den\n"
        "- on_off: true (bat) hoac false (tat), mac dinh true\n"
        "- brightness: Do sang phan tram tu 0 den 100 (-1 neu khong thay doi)\n"
        "- color_temp: Nhiet do mau Kelvin (-1 neu khong thay doi)\n"
        "- endpoint_id: Ma endpoint (mac dinh '1')",
        PropertyList({
            Property("device_id", kPropertyTypeString),
            Property("on_off", kPropertyTypeBoolean, true),
            Property("brightness", kPropertyTypeInteger, -1, -1, 100),
            Property("color_temp", kPropertyTypeInteger, -1, -1, 10000),
            Property("endpoint_id", kPropertyTypeString, std::string("1"))
        }),
        [this](const PropertyList& props) -> ToolResult {
            return HandleLightControl(props);
        }
    );

    /* 4. home.control_curtain */
    mcp.AddTool(
        "home.control_curtain",
        "Dieu khien rem cua, cong cuon: mo, dong, dung hoac dat vi tri phan tram.\n"
        "Tham so:\n"
        "- device_id: ID hoac ten rem cua\n"
        "- action: 'open' (mo), 'close' (dong), hoac 'stop' (dung)\n"
        "- position: Vi tri tu 0 (dong het) den 100 (mo het), dat -1 neu dung action\n"
        "- endpoint_id: Ma endpoint (mac dinh '1')",
        PropertyList({
            Property("device_id", kPropertyTypeString),
            Property("action", kPropertyTypeString, std::string("")),
            Property("position", kPropertyTypeInteger, -1, -1, 100),
            Property("endpoint_id", kPropertyTypeString, std::string("1"))
        }),
        [this](const PropertyList& props) -> ToolResult {
            return HandleCurtainControl(props);
        }
    );

    /* 5. home.control_ac */
    mcp.AddTool(
        "home.control_ac",
        "Dieu khien may dieu hoa nhiet do hoac HVAC: bat/tat nguon, cai dat nhiet do hoac che do hoat dong.\n"
        "Tham so:\n"
        "- device_id: ID hoac ten dieu hoa\n"
        "- power: true (bat) hoac false (tat)\n"
        "- setpoint: Nhiet do cai dat tu 16 den 35 do C (-1 neu khong thay doi)\n"
        "- mode: Che do ('cool', 'heat', 'fan', 'dry', 'auto', de trong neu khong thay doi)\n"
        "- endpoint_id: Ma endpoint (mac dinh '1')",
        PropertyList({
            Property("device_id", kPropertyTypeString),
            Property("power", kPropertyTypeBoolean, true),
            Property("setpoint", kPropertyTypeInteger, -1, -1, 35),
            Property("mode", kPropertyTypeString, std::string("")),
            Property("endpoint_id", kPropertyTypeString, std::string("1"))
        }),
        [this](const PropertyList& props) -> ToolResult {
            return HandleAcControl(props);
        }
    );

    /* 6. home.query_sensor */
    mcp.AddTool(
        "home.query_sensor",
        "Doc thong tin trang thai cam bien thoi gian thuc (nhiet do, do am, anh sang, cua, chuyen dong, pin).\n"
        "Tham so:\n"
        "- device_id: ID hoac ten cam bien\n"
        "- endpoint_id: Ma endpoint (mac dinh '1')",
        PropertyList({
            Property("device_id", kPropertyTypeString),
            Property("endpoint_id", kPropertyTypeString, std::string("1"))
        }),
        [this](const PropertyList& props) -> ToolResult {
            return HandleQuerySensor(props);
        }
    );

    /* 7. home.run_scene */
    mcp.AddTool(
        "home.run_scene",
        "Kich hoat ngu canh / kich ban thong minh (Scene) theo ID hoac ten (vi du: 'Ve nha', 'Di ngu').\n"
        "Tham so:\n"
        "- scene_id: ID kich ban (sc_...) hoac ten kich ban",
        PropertyList({
            Property("scene_id", kPropertyTypeString)
        }),
        [this](const PropertyList& props) -> ToolResult {
            return HandleRunScene(props);
        }
    );
}

ToolResult AppMcpBridge::HandleGetDeviceList(const PropertyList& props)
{
    (void)props;
    const app_snapshot_t* snap = app_gateway_get_snapshot();
    if (!snap) {
        return std::unexpected("Chua nap duoc snapshot tu Gateway");
    }

    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return std::unexpected("Khong du bo nho tao json");
    }

    cJSON_AddNumberToObject(root, "revision", (double)snap->revision);
    cJSON_AddStringToObject(root, "home_id", snap->home_id);
    cJSON_AddStringToObject(root, "home_name", snap->home_name);

    /* Danh sach phong */
    cJSON* rooms_arr = cJSON_CreateArray();
    for (uint16_t i = 0; i < snap->room_count; i++) {
        cJSON* r_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(r_obj, "id", snap->rooms[i].id);
        cJSON_AddStringToObject(r_obj, "name", snap->rooms[i].name);
        cJSON_AddItemToArray(rooms_arr, r_obj);
    }
    cJSON_AddItemToObject(root, "rooms", rooms_arr);

    /* Danh sach thiet bi */
    cJSON* devs_arr = cJSON_CreateArray();
    for (uint16_t i = 0; i < snap->device_count; i++) {
        const app_device_t* dev = &snap->devices[i];
        cJSON* d_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(d_obj, "id", dev->id);
        cJSON_AddStringToObject(d_obj, "name", dev->name);
        cJSON_AddStringToObject(d_obj, "room_id", dev->room_id);
        cJSON_AddStringToObject(d_obj, "room_name", GetRoomName(snap, dev->room_id));
        cJSON_AddStringToObject(d_obj, "category", dev->category);
        cJSON_AddBoolToObject(d_obj, "online", dev->online);

        cJSON* eps_arr = cJSON_CreateArray();
        for (uint8_t ep = 0; ep < dev->endpoint_count; ep++) {
            const app_endpoint_t* endpoint = &dev->endpoints[ep];
            cJSON* ep_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(ep_obj, "id", endpoint->id);
            cJSON_AddStringToObject(ep_obj, "name", endpoint->name);

            cJSON* traits_arr = cJSON_CreateArray();
            for (uint8_t t = 0; t < endpoint->trait_count; t++) {
                const app_trait_t* tr = &endpoint->traits[t];
                cJSON* tr_obj = cJSON_CreateObject();
                cJSON_AddStringToObject(tr_obj, "name", tr->name);
                cJSON_AddBoolToObject(tr_obj, "writable", tr->writable);
                if (tr->state_valid) {
                    if (tr->type == TRAIT_TYPE_ON_OFF || tr->type == TRAIT_TYPE_CONTACT || tr->type == TRAIT_TYPE_OCCUPANCY) {
                        cJSON_AddBoolToObject(tr_obj, "state", tr->state.b_val);
                    } else if (tr->type == TRAIT_TYPE_BRIGHTNESS || tr->type == TRAIT_TYPE_COLOR_TEMP ||
                               tr->type == TRAIT_TYPE_POSITION || tr->type == TRAIT_TYPE_THERMOSTAT_SETPOINT ||
                               tr->type == TRAIT_TYPE_FAN_SPEED || tr->type == TRAIT_TYPE_TEMPERATURE ||
                               tr->type == TRAIT_TYPE_HUMIDITY || tr->type == TRAIT_TYPE_ILLUMINANCE ||
                               tr->type == TRAIT_TYPE_BATTERY || tr->type == TRAIT_TYPE_POWER_METER) {
                        cJSON_AddNumberToObject(tr_obj, "state", tr->state.num_val);
                    } else {
                        cJSON_AddStringToObject(tr_obj, "state", tr->state.str_val);
                    }
                }
                cJSON_AddItemToArray(traits_arr, tr_obj);
            }
            cJSON_AddItemToObject(ep_obj, "traits", traits_arr);
            cJSON_AddItemToArray(eps_arr, ep_obj);
        }
        cJSON_AddItemToObject(d_obj, "endpoints", eps_arr);
        cJSON_AddItemToArray(devs_arr, d_obj);
    }
    cJSON_AddItemToObject(root, "devices", devs_arr);

    /* Danh sach kich ban */
    cJSON* scenes_arr = cJSON_CreateArray();
    for (uint16_t i = 0; i < snap->scene_count; i++) {
        cJSON* s_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(s_obj, "id", snap->scenes[i].id);
        cJSON_AddStringToObject(s_obj, "name", snap->scenes[i].name);
        cJSON_AddItemToArray(scenes_arr, s_obj);
    }
    cJSON_AddItemToObject(root, "scenes", scenes_arr);

    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) {
        return std::unexpected("Loi serialize danh sach thiet bi");
    }
    std::string res(json_str);
    cJSON_free(json_str);
    return res;
}

ToolResult AppMcpBridge::HandleSwitchControl(const PropertyList& props)
{
    const app_snapshot_t* snap = app_gateway_get_snapshot();
    std::string dev_query = props["device_id"].value<std::string>();
    bool state = props["state"].value<bool>();
    std::string ep_id = props["endpoint_id"].value<std::string>();

    const app_device_t* dev = FindDevice(snap, dev_query);
    if (!dev) {
        return std::unexpected("Khong tim thay thiet bi: " + dev_query);
    }

    esp_err_t err = app_gateway_send_command(dev->id, ep_id.c_str(), "on_off", state ? "true" : "false");
    if (err != ESP_OK) {
        return std::unexpected("Khong the gui lenh toi Gateway (loi " + std::string(esp_err_to_name(err)) + ")");
    }

    std::string msg = "Da gui lenh " + std::string(state ? "bat" : "tat") + " cho thiet bi '" + dev->name + "'";
    return msg;
}

ToolResult AppMcpBridge::HandleLightControl(const PropertyList& props)
{
    const app_snapshot_t* snap = app_gateway_get_snapshot();
    std::string dev_query = props["device_id"].value<std::string>();
    bool on_off = props["on_off"].value<bool>();
    int brightness = props["brightness"].value<int>();
    int color_temp = props["color_temp"].value<int>();
    std::string ep_id = props["endpoint_id"].value<std::string>();

    const app_device_t* dev = FindDevice(snap, dev_query);
    if (!dev) {
        return std::unexpected("Khong tim thay den chieu sang: " + dev_query);
    }

    if (brightness >= 0) {
        if (brightness > 100) brightness = 100;
        esp_err_t err = app_gateway_send_command(dev->id, ep_id.c_str(), "brightness", std::to_string(brightness).c_str());
        if (err != ESP_OK) return std::unexpected("Loi khi dieu chinh do sang den");
        return "Da chinh do sang den '" + std::string(dev->name) + "' thanh " + std::to_string(brightness) + "%";
    }

    if (color_temp >= 0) {
        esp_err_t err = app_gateway_send_command(dev->id, ep_id.c_str(), "color_temperature", std::to_string(color_temp).c_str());
        if (err != ESP_OK) return std::unexpected("Loi khi dieu chinh nhiet do mau den");
        return "Da chinh nhiet do mau den '" + std::string(dev->name) + "' thanh " + std::to_string(color_temp) + "K";
    }

    esp_err_t err = app_gateway_send_command(dev->id, ep_id.c_str(), "on_off", on_off ? "true" : "false");
    if (err != ESP_OK) return std::unexpected("Loi khi bat/tat den");
    return "Da " + std::string(on_off ? "bat" : "tat") + " den '" + dev->name + "'";
}

ToolResult AppMcpBridge::HandleCurtainControl(const PropertyList& props)
{
    const app_snapshot_t* snap = app_gateway_get_snapshot();
    std::string dev_query = props["device_id"].value<std::string>();
    std::string action = props["action"].value<std::string>();
    int position = props["position"].value<int>();
    std::string ep_id = props["endpoint_id"].value<std::string>();

    const app_device_t* dev = FindDevice(snap, dev_query);
    if (!dev) {
        return std::unexpected("Khong tim thay rem cua: " + dev_query);
    }

    if (position >= 0) {
        if (position > 100) position = 100;
        esp_err_t err = app_gateway_send_command(dev->id, ep_id.c_str(), "position", std::to_string(position).c_str());
        if (err != ESP_OK) return std::unexpected("Loi khi dat vi tri rem");
        return "Da dat vi tri rem '" + std::string(dev->name) + "' thanh " + std::to_string(position) + "%";
    }

    if (action == "open" || action == "close" || action == "stop") {
        char val_buf[32];
        snprintf(val_buf, sizeof(val_buf), "\"%s\"", action.c_str());
        esp_err_t err = app_gateway_send_command(dev->id, ep_id.c_str(), "cover_motion", val_buf);
        if (err != ESP_OK) return std::unexpected("Loi khi dieu khien rem: " + action);
        return "Da gui lenh " + action + " cho rem '" + dev->name + "'";
    }

    return std::unexpected("Can chi dinh action ('open', 'close', 'stop') hoac position (0-100)");
}

ToolResult AppMcpBridge::HandleAcControl(const PropertyList& props)
{
    const app_snapshot_t* snap = app_gateway_get_snapshot();
    std::string dev_query = props["device_id"].value<std::string>();
    bool power = props["power"].value<bool>();
    int setpoint = props["setpoint"].value<int>();
    std::string mode = props["mode"].value<std::string>();
    std::string ep_id = props["endpoint_id"].value<std::string>();

    const app_device_t* dev = FindDevice(snap, dev_query);
    if (!dev) {
        return std::unexpected("Khong tim thay dieu hoa: " + dev_query);
    }

    if (setpoint >= 16 && setpoint <= 35) {
        esp_err_t err = app_gateway_send_command(dev->id, ep_id.c_str(), "thermostat_setpoint", std::to_string(setpoint).c_str());
        if (err != ESP_OK) return std::unexpected("Loi khi dat nhiet do dieu hoa");
        return "Da dat nhiet do dieu hoa '" + std::string(dev->name) + "' thanh " + std::to_string(setpoint) + " do C";
    }

    if (!mode.empty()) {
        char val_buf[32];
        snprintf(val_buf, sizeof(val_buf), "\"%s\"", mode.c_str());
        esp_err_t err = app_gateway_send_command(dev->id, ep_id.c_str(), "thermostat_mode", val_buf);
        if (err != ESP_OK) return std::unexpected("Loi khi dat che do dieu hoa");
        return "Da chuyen che do dieu hoa '" + std::string(dev->name) + "' sang " + mode;
    }

    esp_err_t err = app_gateway_send_command(dev->id, ep_id.c_str(), "on_off", power ? "true" : "false");
    if (err != ESP_OK) return std::unexpected("Loi khi bat/tat nguon dieu hoa");
    return "Da " + std::string(power ? "bat" : "tat") + " nguon dieu hoa '" + dev->name + "'";
}

ToolResult AppMcpBridge::HandleQuerySensor(const PropertyList& props)
{
    const app_snapshot_t* snap = app_gateway_get_snapshot();
    std::string dev_query = props["device_id"].value<std::string>();
    std::string ep_id = props["endpoint_id"].value<std::string>();

    const app_device_t* dev = FindDevice(snap, dev_query);
    if (!dev) {
        return std::unexpected("Khong tim thay cam bien: " + dev_query);
    }

    std::string info = "Thong tin cam bien '" + std::string(dev->name) + "': ";
    bool found_trait = false;

    for (uint8_t ep = 0; ep < dev->endpoint_count; ep++) {
        const app_endpoint_t* endpoint = &dev->endpoints[ep];
        if (!ep_id.empty() && endpoint->id[0] != '\0' && ep_id != endpoint->id && dev->endpoint_count > 1) {
            continue;
        }

        for (uint8_t t = 0; t < endpoint->trait_count; t++) {
            const app_trait_t* tr = &endpoint->traits[t];
            if (!tr->state_valid) continue;

            found_trait = true;
            char val_buf[64] = {0};
            switch (tr->type) {
                case TRAIT_TYPE_TEMPERATURE:
                    snprintf(val_buf, sizeof(val_buf), "[Nhiet do: %.1f do C] ", tr->state.num_val);
                    break;
                case TRAIT_TYPE_HUMIDITY:
                    snprintf(val_buf, sizeof(val_buf), "[Do am: %.1f%%] ", tr->state.num_val);
                    break;
                case TRAIT_TYPE_ILLUMINANCE:
                    snprintf(val_buf, sizeof(val_buf), "[Anh sang: %.0f lux] ", tr->state.num_val);
                    break;
                case TRAIT_TYPE_CONTACT:
                    snprintf(val_buf, sizeof(val_buf), "[Cua: %s] ", tr->state.b_val ? "Mo" : "Dong");
                    break;
                case TRAIT_TYPE_OCCUPANCY:
                    snprintf(val_buf, sizeof(val_buf), "[Hien dien: %s] ", tr->state.b_val ? "Co nguoi" : "Khong");
                    break;
                case TRAIT_TYPE_BATTERY:
                    snprintf(val_buf, sizeof(val_buf), "[Pin: %.0f%%] ", tr->state.num_val);
                    break;
                case TRAIT_TYPE_POWER_METER:
                    snprintf(val_buf, sizeof(val_buf), "[Cong suat: %.1f W] ", tr->state.num_val);
                    break;
                case TRAIT_TYPE_ON_OFF:
                    snprintf(val_buf, sizeof(val_buf), "[Trang thai: %s] ", tr->state.b_val ? "Bat" : "Tat");
                    break;
                default:
                    if (tr->state.str_val[0] != '\0') {
                        snprintf(val_buf, sizeof(val_buf), "[%s: %s] ", tr->name, tr->state.str_val);
                    }
                    break;
            }
            info += val_buf;
        }
    }

    if (!found_trait) {
        info += "Chua co du lieu do hoac thiet bi dang offline.";
    }

    return info;
}

ToolResult AppMcpBridge::HandleRunScene(const PropertyList& props)
{
    const app_snapshot_t* snap = app_gateway_get_snapshot();
    std::string scene_query = props["scene_id"].value<std::string>();

    const app_scene_t* sc = FindScene(snap, scene_query);
    if (!sc) {
        return std::unexpected("Khong tim thay kich ban: " + scene_query);
    }

    esp_err_t err = app_gateway_run_scene(sc->id);
    if (err != ESP_OK) {
        return std::unexpected("Khong the kich hoat kich ban toi Gateway (loi " + std::string(esp_err_to_name(err)) + ")");
    }

    return "Da kich hoat kich ban '" + std::string(sc->name) + "'";
}
