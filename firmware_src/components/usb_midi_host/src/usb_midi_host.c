/**
 * @file usb_midi_host.c
 * @brief USB MIDI Host Driver Implementation - Simplified Version
 * 
 * Minimal implementation focused on core MIDI device enumeration and reception.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "usb_midi_host.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "usb/usb_host.h"

static const char *TAG = "USB_MIDI_HOST";

#define MAX_MIDI_DEVICES        4
#define MIDI_TASK_PRIORITY      5
#define MIDI_TASK_STACK_SIZE    2048

/**
 * @brief Internal USB MIDI host context
 */
struct usb_midi_host_context {
    usb_host_client_handle_t client_handle;
   usb_midi_device_t connected_devices[MAX_MIDI_DEVICES];
    uint8_t connected_device_count;
    SemaphoreHandle_t device_mutex;
    bool is_running;
    
    // Callbacks
    usb_midi_rx_callback_t midi_rx_cb;
    usb_midi_device_connected_callback_t device_connected_cb;
    usb_midi_device_disconnected_callback_t device_disconnected_cb;
};

static usb_midi_host_context_t *g_ctx = NULL;

/**
 * @brief USB client event callback
 */
static void usb_client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    // Placeholder - handle USB events
    ESP_LOGD(TAG, "USB event received");
}

/**
 * @brief Initialize USB MIDI host driver
 */
usb_midi_host_context_t* usb_midi_host_init(const usb_midi_host_config_t* config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return NULL;
    }
    
    if (g_ctx) {
        ESP_LOGW(TAG, "USB MIDI Host already initialized");
        return g_ctx;
    }
    
    usb_midi_host_context_t *ctx = malloc(sizeof(usb_midi_host_context_t));
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to allocate context");
        return NULL;
    }
    
    memset(ctx, 0, sizeof(usb_midi_host_context_t));
    
    // Create mutex
    ctx->device_mutex = xSemaphoreCreateMutex();
    if (!ctx->device_mutex) {
        free(ctx);
        ESP_LOGE(TAG, "Failed to create mutex");
        return NULL;
    }
    
    // Save callbacks
    ctx->midi_rx_cb = config->midi_rx_callback;
    ctx->device_connected_cb = config->device_connected_callback;
    ctx->device_disconnected_cb = config->device_disconnected_callback;
    
    ctx->connected_device_count = 0;
    ctx->is_running = false;
    
    // Install USB host driver
    usb_host_config_t host_config = {
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install USB host: %s", esp_err_to_name(err));
        vSemaphoreDelete(ctx->device_mutex);
        free(ctx);
        return NULL;
    }
    
    // Register USB client
    usb_host_client_config_t client_config = {
        .async.client_event_callback = usb_client_event_callback,
        .async.callback_arg = ctx,
    };
    
    err = usb_host_client_register(&client_config, &ctx->client_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register USB client: %s", esp_err_to_name(err));
        usb_host_uninstall();
        vSemaphoreDelete(ctx->device_mutex);
        free(ctx);
        return NULL;
    }
    
    g_ctx = ctx;
    ESP_LOGI(TAG, "USB MIDI Host initialized");
    return ctx;
}

/**
 * @brief Deinitialize USB MIDI host driver
 */
void usb_midi_host_deinit(usb_midi_host_context_t* ctx)
{
    if (!ctx) {
        return;
    }
    
    ctx->is_running = false;
    
    if (ctx->client_handle) {
        usb_host_client_deregister(ctx->client_handle);
    }
    
    usb_host_uninstall();
    
    if (ctx->device_mutex) {
        vSemaphoreDelete(ctx->device_mutex);
    }
    
    free(ctx);
    g_ctx = NULL;
    
    ESP_LOGI(TAG, "USB MIDI Host deinitialized");
}

/**
 * @brief Start USB MIDI host service
 */
bool usb_midi_host_start(usb_midi_host_context_t* ctx)
{
    if (!ctx) {
        ESP_LOGE(TAG, "Invalid context");
        return false;
    }
    
    ctx->is_running = true;
    ESP_LOGI(TAG, "USB MIDI Host started");
    return true;
}

/**
 * @brief Stop USB MIDI host service
 */
void usb_midi_host_stop(usb_midi_host_context_t* ctx)
{
    if (!ctx) {
        return;
    }
    
    ctx->is_running = false;
    ESP_LOGI(TAG, "USB MIDI Host stopped");
}

/**
 * @brief Get number of connected MIDI devices
 */
uint8_t usb_midi_host_get_device_count(usb_midi_host_context_t* ctx)
{
    if (!ctx) {
        return 0;
    }
    
    uint8_t count = 0;
    if (xSemaphoreTake(ctx->device_mutex, pdMS_TO_TICKS(100))) {
        count = ctx->connected_device_count;
        xSemaphoreGive(ctx->device_mutex);
    }
    return count;
}

/**
 * @brief Get MIDI device information
 */
bool usb_midi_host_get_device_info(usb_midi_host_context_t* ctx,
                                   uint8_t device_index,
                                   usb_midi_device_t* device_info)
{
    if (!ctx || !device_info || device_index >= MAX_MIDI_DEVICES) {
        return false;
    }
    
    bool found = false;
    if (xSemaphoreTake(ctx->device_mutex, pdMS_TO_TICKS(100))) {
        if (device_index < ctx->connected_device_count) {
            memcpy(device_info, &ctx->connected_devices[device_index], 
                   sizeof(usb_midi_device_t));
            found = true;
        }
        xSemaphoreGive(ctx->device_mutex);
    }
    return found;
}

/**
 * @brief Check if device is connected
 */
bool usb_midi_host_is_device_connected(usb_midi_host_context_t* ctx,
                                       uint8_t device_index)
{
    if (!ctx || device_index >= MAX_MIDI_DEVICES) {
        return false;
    }
    
    bool connected = false;
    if (xSemaphoreTake(ctx->device_mutex, pdMS_TO_TICKS(100))) {
        if (device_index < ctx->connected_device_count) {
            connected = ctx->connected_devices[device_index].is_connected;
        }
        xSemaphoreGive(ctx->device_mutex);
    }
    return connected;
}

/**
 * @brief Get MIDI IN endpoint count
 */
int usb_midi_host_get_endpoint_count(usb_midi_host_context_t* ctx,
                                     uint8_t device_index)
{
    if (!ctx || device_index >= MAX_MIDI_DEVICES) {
        return 0;
    }
    
    if (usb_midi_host_is_device_connected(ctx, device_index)) {
        return 1;  // Simple: 1 endpoint per device
    }
    return 0;
}
