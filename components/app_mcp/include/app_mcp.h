#pragma once

#include "mcp_server.h"
#include "app_gateway.h"

#ifdef __cplusplus
extern "C" {
#endif

void app_mcp_init(void);

#ifdef __cplusplus
}

class AppMcpBridge {
public:
    static AppMcpBridge& GetInstance();
    void Initialize();
    void RegisterDynamicTools();

private:
    AppMcpBridge();
    ~AppMcpBridge() = default;

    ToolResult HandleSwitchControl(const PropertyList& props);
    ToolResult HandleLightControl(const PropertyList& props);
    ToolResult HandleCurtainControl(const PropertyList& props);
    ToolResult HandleAcControl(const PropertyList& props);
    ToolResult HandleRunScene(const PropertyList& props);
    ToolResult HandleGetDeviceList(const PropertyList& props);
    ToolResult HandleQuerySensor(const PropertyList& props);

    bool tools_registered_ = false;
};
#endif
