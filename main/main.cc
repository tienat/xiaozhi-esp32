#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"
#include "app_spiffs.h"
#include "app_config.h"
#include "app_network.h"
#include "app_gateway.h"
#include "app_ui.h"

#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize HCP Storage, Config, Network, and Gateway
    app_spiffs_init();
    app_config_init();
    app_gateway_init();

    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();

    // Register callback for AI Widget to toggle chat
    app_ui_register_toggle_voice_cb([]() {
        Application::GetInstance().ToggleChatState();
    });

    // Initialize HCP Home Gateway UI (LVGL 9)
    app_ui_init();

    // Logo va UI da san sang truoc khi goi RPC mang co the cham.
    app_network_init();
    app_gateway_start();

    app.Run();  // This function runs the main event loop and never returns
}
