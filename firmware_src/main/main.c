/**
 * @file main.c
 * @brief 应用入口
 * 
 * 使用 app_core 模块管理应用生命周期，通过事件总线解耦模块通信。
 */

#include <stdio.h>
#include "esp_log.h"
#include "app_core.h"
#include "app_config.h"

static const char* TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "\n\n===== LinkMIDI - Network MIDI 2.0 Bridge =====");
    
    // 应用配置
    app_config_t config = {
        .device_name = APP_DEVICE_NAME,
        .product_id = APP_PRODUCT_ID,
        .listen_port = APP_LISTEN_PORT,
        .enable_test_sender = true,  // 启用测试发送器
    };
    
    // 初始化应用核心
    midi_error_t err = app_core_init(&config);
    if (err != MIDI_OK) {
        ESP_LOGE(TAG, "Failed to initialize app: %s", midi_error_str(err));
        return;
    }
    
    // 启动应用
    err = app_core_start();
    if (err != MIDI_OK) {
        ESP_LOGE(TAG, "Failed to start app: %s", midi_error_str(err));
        app_core_deinit();
        return;
    }
    
    ESP_LOGI(TAG, "Application started successfully");
    ESP_LOGI(TAG, "Connect a USB MIDI keyboard or wait for network connection");
}
