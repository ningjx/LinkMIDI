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

static const char* TAG = "MIDI2_TEST";

static network_midi2_context_t* g_midi2_ctx = NULL;
static bool g_session_active = false;

/* ============================================================================
 * WiFi and Network Callbacks
 * ============================================================================ */

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "WiFi station started, attempting to connect...");
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGI(TAG, "WiFi disconnected, attempting reconnection...");
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "WiFi connected! IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        }
    }
}

/* ============================================================================
 * MIDI2 Callbacks
 * ============================================================================ */

static void midi2_log_callback(const char* message) {
    ESP_LOGI(TAG, "[MIDI2] %s", message);
}

static void midi2_midi_rx_callback(const uint8_t* data, uint16_t length) {
    if (length >= 3) {
        ESP_LOGI(TAG, "[RX_MIDI] Status: 0x%02X, Note: %d, Velocity: %d",
                 data[0], data[1], data[2]);
    }
}

static void midi2_ump_rx_callback(const uint8_t* data, uint16_t length) {
    ESP_LOGI(TAG, "[RX_UMP] %d bytes", length);
}

/* ============================================================================
 * MIDI Sending Task
 * ============================================================================ */

static void midi_send_task(void* arg) {
    ESP_LOGI(TAG, "MIDI send task started, waiting for session...");
    
    uint8_t note = 60;  // C4 (Middle C)
    uint8_t velocity = 100;
    uint8_t channel = 0;
    bool note_on = true;
    uint32_t tick_count = 0;
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));  // Check every 100ms
        tick_count++;
        
        // Every 10 ticks (1 second), toggle note on/off
        if (tick_count >= 10) {
            tick_count = 0;
            
            if (g_session_active && network_midi2_is_session_active(g_midi2_ctx)) {
                if (note_on) {
                    ESP_LOGI(TAG, "Sending Note ON - C4 (Note:%d, Velocity:%d)", note, velocity);
                    network_midi2_send_note_on(g_midi2_ctx, note, velocity, channel);
                    note_on = false;
                } else {
                    ESP_LOGI(TAG, "Sending Note OFF - C4 (Note:%d)", note);
                    network_midi2_send_note_off(g_midi2_ctx, note, 0, channel);
                    note_on = true;
                }
            }
        }
    }
}

/* ============================================================================
 * Session Monitoring Task
 * ============================================================================ */

static void session_monitor_task(void* arg) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        bool is_active = network_midi2_is_session_active(g_midi2_ctx);
        
        if (is_active && !g_session_active) {
            ESP_LOGI(TAG, "=== Session ESTABLISHED ===");
            g_session_active = true;
        } else if (!is_active && g_session_active) {
            ESP_LOGI(TAG, "=== Session CLOSED ===");
            g_session_active = false;
        }
    }
}

/* ============================================================================
 * WiFi Initialization
 * ============================================================================ */

static void init_wifi(void) {
    // Initialize network stack
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default WiFi STA netif
    esp_netif_create_default_wifi_sta();
    
    // Create event loop
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                               &wifi_event_handler, NULL));
    
    // Initialize WiFi driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Configure WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "YourSSID",           // CHANGE THIS!
            .password = "YourPassword",   // CHANGE THIS!
            .scan_method = WIFI_FAST_SCAN,
            .bssid_set = false,
            .channel = 0,
            .listen_interval = 0,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", (char*)wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* ============================================================================
 * App Main
 * ============================================================================ */

void app_main(void) {
    ESP_LOGI(TAG, "\n\n===== Network MIDI 2.0 Service Test =====");
    ESP_LOGI(TAG, "Initializing WiFi...");
    
    // Initialize WiFi
    init_wifi();
    
    // Wait for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // Initialize MIDI 2.0 as SERVER (accepts connections from other devices)
    ESP_LOGI(TAG, "Initializing Network MIDI 2.0 Service...");
    
    network_midi2_config_t config = {
        .device_name = "ESP32-MIDI2-Server",
        .product_id = "ESP32S3",
        .listen_port = 5507,
        .mode = MODE_SERVER,              // Act as server - other devices connect to us
        .enable_discovery = true,         // Announce via mDNS so others can discover
        .log_callback = midi2_log_callback,
        .midi_rx_callback = midi2_midi_rx_callback,
        .ump_rx_callback = midi2_ump_rx_callback,
    };
    
    g_midi2_ctx = network_midi2_init_with_config(&config);
    if (!g_midi2_ctx) {
        ESP_LOGE(TAG, "Failed to initialize Network MIDI 2.0");
        return;
    }
    
    // Start the service
    if (!network_midi2_start(g_midi2_ctx)) {
        ESP_LOGE(TAG, "Failed to start Network MIDI 2.0");
        return;
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "MIDI 2.0 Service is RUNNING");
    ESP_LOGI(TAG, "Other devices can now discover and connect");
    ESP_LOGI(TAG, "Service will send C4 Note ON/OFF every 1s when connected");
    ESP_LOGI(TAG, "========================================\n");
    
    // Create monitoring tasks
    xTaskCreate(session_monitor_task, "session_monitor", 2048, NULL, 5, NULL);
    xTaskCreate(midi_send_task, "midi_send", 2048, NULL, 5, NULL);
}
