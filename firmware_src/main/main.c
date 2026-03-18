#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "spi_flash_mmap.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "network_midi2.h"

static const char* TAG = "MIDI2_DEMO";

static network_midi2_context_t* g_midi2_ctx = NULL;

/**
 * @brief Logging callback
 */
static void midi2_log_callback(const char* message) {
    ESP_LOGI(TAG, "%s", message);
}

/**
 * @brief MIDI reception callback
 */
static void midi2_midi_rx_callback(const uint8_t* data, uint16_t length) {
    if (length >= 3) {
        ESP_LOGI(TAG, "[RX_MIDI] Status: 0x%02X, Data1: 0x%02X, Data2: 0x%02X",
                 data[0], data[1], data[2]);
        
        uint8_t status = data[0];
        uint8_t data1 = data[1];
        uint8_t data2 = data[2];
        
        char buf[128];
        network_midi2_midi_to_string(status, data1, data2, buf, sizeof(buf));
        ESP_LOGI(TAG, "  -> %s", buf);
    }
}

/**
 * @brief UMP reception callback
 */
static void midi2_ump_rx_callback(const uint8_t* data, uint16_t length) {
    ESP_LOGI(TAG, "[RX_UMP] %d bytes: %02X %02X %02X %02X",
             length, data[0], data[1], data[2], data[3]);
}

/**
 * @brief Perform discovery of available MIDI 2.0 devices
 */
static void discover_devices(void) {
    ESP_LOGI(TAG, "\n=== Discovery Mode ===");
    ESP_LOGI(TAG, "Sending mDNS query...");
    
    network_midi2_send_discovery_query(g_midi2_ctx);
    
    // Wait for responses
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    int device_count = network_midi2_get_device_count(g_midi2_ctx);
    ESP_LOGI(TAG, "Found %d device(s)\n", device_count);
    
    for (int i = 0; i < device_count; i++) {
        char device_name[64];
        uint32_t ip_addr;
        uint16_t port;
        
        if (network_midi2_get_discovered_device(g_midi2_ctx, i, device_name, &ip_addr, &port)) {
            ESP_LOGI(TAG, "Device %d: %s", i+1, device_name);
            ESP_LOGI(TAG, "  IP: %d.%d.%d.%d:%d",
                     (ip_addr >> 0) & 0xFF,
                     (ip_addr >> 8) & 0xFF,
                     (ip_addr >> 16) & 0xFF,
                     (ip_addr >> 24) & 0xFF,
                     port);
        }
    }
}

/**
 * @brief Connect to a remote device and send test MIDI messages
 */
static void test_session_and_send(void) {
    // Check if any devices are discovered
    int device_count = network_midi2_get_device_count(g_midi2_ctx);
    if (device_count == 0) {
        ESP_LOGW(TAG, "No devices discovered. Please run discovery first.");
        return;
    }
    
    // Connect to first device
    char device_name[64];
    uint32_t ip_addr;
    uint16_t port;
    
    if (!network_midi2_get_discovered_device(g_midi2_ctx, 0, device_name, &ip_addr, &port)) {
        ESP_LOGE(TAG, "Failed to get device info");
        return;
    }
    
    ESP_LOGI(TAG, "\n=== Session Test ===");
    ESP_LOGI(TAG, "Connecting to: %s", device_name);
    
    // Initiate session
    if (!network_midi2_session_initiate(g_midi2_ctx, ip_addr, port, device_name)) {
        ESP_LOGE(TAG, "Failed to initiate session");
        return;
    }
    
    // Wait for acceptance
    for (int i = 0; i < 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
        
        if (network_midi2_is_session_active(g_midi2_ctx)) {
            ESP_LOGI(TAG, "Session established!");
            break;
        }
    }
    
    if (!network_midi2_is_session_active(g_midi2_ctx)) {
        ESP_LOGW(TAG, "Session not established (timeout)");
        return;
    }
    
    ESP_LOGI(TAG, "\n=== Sending MIDI Messages ===");
    
    // Send Note On
    network_midi2_send_note_on(g_midi2_ctx, 60, 100, 0);  // Middle C, velocity 100, channel 0
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Send Pitch Bend
    network_midi2_send_pitch_bend(g_midi2_ctx, 2000, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Send Control Change (Volume)
    network_midi2_send_control_change(g_midi2_ctx, 7, 100, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Send Note Off
    network_midi2_send_note_off(g_midi2_ctx, 60, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Keep sending ping to keep session alive
    ESP_LOGI(TAG, "\n=== Keeping Session Alive ===");
    for (int i = 0; i < 5; i++) {
        network_midi2_send_ping(g_midi2_ctx);
        ESP_LOGI(TAG, "Ping #%d sent", i+1);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
    // Terminate session
    ESP_LOGI(TAG, "\nTerminating session...");
    network_midi2_session_terminate(g_midi2_ctx);
    
    vTaskDelay(pdMS_TO_TICKS(500));
}

/**
 * @brief Interactive menu for testing
 */
static void app_menu_task(void* arg) {
    vTaskDelay(pdMS_TO_TICKS(2000));  // Wait for system to stabilize
    
    while (1) {
        ESP_LOGI(TAG, "\n========================================");
        ESP_LOGI(TAG, "Network MIDI 2.0 Demo - Interactive Menu");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "1. Run device discovery");
        ESP_LOGI(TAG, "2. Connect and send MIDI data");
        ESP_LOGI(TAG, "3. Show device info");
        ESP_LOGI(TAG, "4. Send Note On (middle C)");
        ESP_LOGI(TAG, "5. Send Note Off");
        ESP_LOGI(TAG, "6. Send Program Change");
        ESP_LOGI(TAG, "7. Keep session alive (ping test)");
        
        // For now, just run the demo sequence
        ESP_LOGI(TAG, "\n>>> Starting demo sequence...");
        
        discover_devices();
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        test_session_and_send();
        
        ESP_LOGI(TAG, "\n>>> Demo completed. Restarting in 30 seconds...");
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/**
 * @brief Initialize network (WiFi)
 */
static void init_wifi(void) {
    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create WiFi netif
    esp_netif_create_default_wifi_sta();
    
    // Initialize WiFi driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Configure WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "YourSSID",           // CHANGE THIS
            .password = "YourPassword",   // CHANGE THIS
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    ESP_LOGI(TAG, "WiFi configured. Connecting...");
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Wait for connection
    vTaskDelay(pdMS_TO_TICKS(5000));
}

/**
 * @brief App entry point
 */
void app_main(void) {
    // Log startup
    ESP_LOGI(TAG, "\n\n=== Network MIDI 2.0 Library Demo ===");
    ESP_LOGI(TAG, "Library version: %s", network_midi2_get_version());
    
    // Initialize network
    ESP_LOGI(TAG, "Initializing network...");
    init_wifi();
    
    // Initialize MIDI 2.0 context
    ESP_LOGI(TAG, "Initializing Network MIDI 2.0...");
    
    network_midi2_config_t config = {
        .device_name = "ESP32-MIDI2-Device",
        .product_id = "ESP32S3",
        .listen_port = 5507,
        .mode = MODE_PEER,                      // Both client and server
        .enable_discovery = true,
        .log_callback = midi2_log_callback,
        .midi_rx_callback = midi2_midi_rx_callback,
        .ump_rx_callback = midi2_ump_rx_callback,
    };
    
    g_midi2_ctx = network_midi2_init_with_config(&config);
    if (!g_midi2_ctx) {
        ESP_LOGE(TAG, "Failed to initialize Network MIDI 2.0");
        return;
    }
    
    // Start the device
    if (!network_midi2_start(g_midi2_ctx)) {
        ESP_LOGE(TAG, "Failed to start Network MIDI 2.0");
        return;
    }
    
    ESP_LOGI(TAG, "Network MIDI 2.0 initialized and started");
    
    // Create menu task
    xTaskCreate(app_menu_task, "menu_task", 2048, NULL, 5, NULL);
}
