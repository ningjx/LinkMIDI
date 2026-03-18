#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "spi_flash_mmap.h"
#include "esp_log.h"
#include "network_midi2.h"
#include "mdns_discovery.h"
#include "wifi_manager.h"
#include "usb_midi_host.h"

static const char* TAG = "MIDI2_TEST";

static network_midi2_context_t* g_midi2_ctx = NULL;
static mdns_discovery_context_t* g_mdns_ctx = NULL;
static usb_midi_host_context_t* g_usb_midi_ctx = NULL;
static bool g_session_active = false;

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
 * USB MIDI Host Callbacks
 * ============================================================================ */

static void usb_midi_rx_callback(uint8_t device_index, const uint8_t* data, uint16_t length) {
    if (length >= 3) {
        ESP_LOGI(TAG, "[USB_MIDI_RX] Device %d: Status=0x%02X, Data1=%d, Data2=%d",
                device_index, data[0], data[1], data[2]);
        
        // TODO: Forward this MIDI data to network_midi2 for transmission over network
        // This will be implemented in the next phase
    }
}

static void usb_midi_device_connected_callback(uint8_t device_index, 
                                                const usb_midi_device_t* device_info) {
    ESP_LOGI(TAG, "[USB_MIDI] Device %d connected: %s %s (VID:0x%04X PID:0x%04X)",
            device_index,
            device_info->manufacturer,
            device_info->product_name,
            device_info->vendor_id,
            device_info->product_id);
}

static void usb_midi_device_disconnected_callback(uint8_t device_index) {
    ESP_LOGI(TAG, "[USB_MIDI] Device %d disconnected", device_index);
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
 * App Main
 * ============================================================================ */

void app_main(void) {
    ESP_LOGI(TAG, "\n\n===== Network MIDI 2.0 Service Test =====");
    ESP_LOGI(TAG, "Starting WiFi initialization...");
    
    // Initialize WiFi with NVS
    if (!wifi_manager_init()) {
        ESP_LOGE(TAG, "Failed to initialize WiFi manager");
        return;
    }
    
    // Connect to WiFi using configured credentials
    if (!wifi_manager_connect()) {
        ESP_LOGE(TAG, "Failed to configure WiFi connection");
        return;
    }
    
    // Wait for WiFi connection (timeout: 10 seconds)
    if (!wifi_manager_wait_for_connection(10000)) {
        ESP_LOGE(TAG, "Failed to connect to WiFi within timeout");
        // Continue anyway - local operations may still work
    }
    
    // Initialize MIDI 2.0 as SERVER (accepts connections from other devices)
    ESP_LOGI(TAG, "Initializing Network MIDI 2.0 Service...");
    
    network_midi2_config_t config = {
        .device_name = CONFIG_MIDI_DEVICE_NAME,
        .product_id = CONFIG_MIDI_PRODUCT_ID,
        .listen_port = CONFIG_MIDI_LISTEN_PORT,
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
    
    // Initialize mDNS discovery module
    ESP_LOGI(TAG, "Initializing mDNS discovery module...");
    g_mdns_ctx = mdns_discovery_init(CONFIG_MIDI_DEVICE_NAME, 
                                      CONFIG_MIDI_PRODUCT_ID,
                                      CONFIG_MIDI_LISTEN_PORT);
    if (!g_mdns_ctx) {
        ESP_LOGE(TAG, "Failed to initialize mDNS discovery");
        network_midi2_deinit(g_midi2_ctx);
        return;
    }
    
    // Start mDNS discovery
    if (!mdns_discovery_start(g_mdns_ctx)) {
        ESP_LOGE(TAG, "Failed to start mDNS discovery");
        network_midi2_deinit(g_midi2_ctx);
        mdns_discovery_deinit(g_mdns_ctx);
        return;
    }
    
    // Start the MIDI 2.0 service
    if (!network_midi2_start(g_midi2_ctx)) {
        ESP_LOGE(TAG, "Failed to start Network MIDI 2.0");
        mdns_discovery_stop(g_mdns_ctx);
        network_midi2_deinit(g_midi2_ctx);
        mdns_discovery_deinit(g_mdns_ctx);
        return;
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "MIDI 2.0 Service is RUNNING");
    ESP_LOGI(TAG, "Device: %s", CONFIG_MIDI_DEVICE_NAME);
    ESP_LOGI(TAG, "Port: %d", CONFIG_MIDI_LISTEN_PORT);
    ESP_LOGI(TAG, "Other devices can now discover and connect");
    ESP_LOGI(TAG, "Service will send C4 Note ON/OFF every 1s when connected");
    ESP_LOGI(TAG, "========================================\n");
    
    // Initialize USB MIDI Host
    ESP_LOGI(TAG, "Initializing USB MIDI Host...");
    usb_midi_host_config_t usb_midi_config = {
        .midi_rx_callback = usb_midi_rx_callback,
        .device_connected_callback = usb_midi_device_connected_callback,
        .device_disconnected_callback = usb_midi_device_disconnected_callback,
    };
    
    g_usb_midi_ctx = usb_midi_host_init(&usb_midi_config);
    if (!g_usb_midi_ctx) {
        ESP_LOGE(TAG, "Failed to initialize USB MIDI host");
    } else {
        // Start USB MIDI host
        if (usb_midi_host_start(g_usb_midi_ctx)) {
            ESP_LOGI(TAG, "USB MIDI Host started successfully");
            ESP_LOGI(TAG, "Connect a USB MIDI keyboard to the device");
        } else {
            ESP_LOGE(TAG, "Failed to start USB MIDI host");
            usb_midi_host_deinit(g_usb_midi_ctx);
            g_usb_midi_ctx = NULL;
        }
    }
    
    // Create monitoring tasks
    xTaskCreate(session_monitor_task, "session_monitor", 2048, NULL, 5, NULL);
    xTaskCreate(midi_send_task, "midi_send", 2048, NULL, 5, NULL);
}
