/**
 * @file mdns_discovery.c
 * @brief mDNS Discovery Module using ESP-IDF esp_mdns component
 */

#include "mdns_discovery.h"
#include "esp_log.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* TAG = "MDNS_DISCOVERY";

#define MAX_DISCOVERED_DEVICES 16

/**
 * @brief Discovered device structure
 */
typedef struct {
    char instance_name[64];
    char hostname[64];
    uint32_t ip_address;
    uint16_t port;
} discovered_device_t;

/**
 * @brief mDNS discovery context
 */
struct mdns_discovery_context {
    char device_name[64];
    char product_id[64];
    uint16_t listen_port;
    
    bool is_running;
    
    discovered_device_t discovered_devices[MAX_DISCOVERED_DEVICES];
    int discovered_count;
    
    SemaphoreHandle_t device_list_mutex;
};

/* ============================================================================
 * Public API
 * ============================================================================ */

mdns_discovery_context_t* mdns_discovery_init(
    const char* device_name,
    const char* product_id,
    uint16_t port)
{
    if (!device_name || !product_id) {
        ESP_LOGE(TAG, "Invalid parameters");
        return NULL;
    }
    
    mdns_discovery_context_t* ctx = calloc(1, sizeof(mdns_discovery_context_t));
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to allocate context");
        return NULL;
    }
    
    strncpy(ctx->device_name, device_name, sizeof(ctx->device_name) - 1);
    strncpy(ctx->product_id, product_id, sizeof(ctx->product_id) - 1);
    ctx->listen_port = port;
    ctx->is_running = false;
    ctx->discovered_count = 0;
    
    ctx->device_list_mutex = xSemaphoreCreateMutex();
    if (!ctx->device_list_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(ctx);
        return NULL;
    }
    
    ESP_LOGI(TAG, "Initialized mDNS discovery for device: %s", device_name);
    return ctx;
}

void mdns_discovery_deinit(mdns_discovery_context_t* ctx) {
    if (!ctx) return;
    
    if (ctx->is_running) {
        mdns_discovery_stop(ctx);
    }
    
    if (ctx->device_list_mutex) {
        vSemaphoreDelete(ctx->device_list_mutex);
    }
    
    free(ctx);
}

bool mdns_discovery_start(mdns_discovery_context_t* ctx) {
    if (!ctx || ctx->is_running) {
        return false;
    }
    
    // Initialize mDNS
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mDNS: %s", esp_err_to_name(err));
        return false;
    }
    
    // Set hostname
    err = mdns_hostname_set(ctx->device_name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set hostname: %s", esp_err_to_name(err));
        mdns_free();
        return false;
    }
    
    // Set instance name
    err = mdns_instance_name_set(ctx->product_id);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set instance name: %s", esp_err_to_name(err));
        // Not critical, continue
    }
    
    // Add MIDI 2.0 service (_midi2._udp)
    err = mdns_service_add(NULL, "_midi2", "_udp", ctx->listen_port, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add MIDI 2.0 service: %s", esp_err_to_name(err));
        mdns_free();
        return false;
    }
    
    ctx->is_running = true;
    ESP_LOGI(TAG, "mDNS discovery started (hostname: %s, port: %d)", 
             ctx->device_name, ctx->listen_port);
    
    return true;
}

void mdns_discovery_stop(mdns_discovery_context_t* ctx) {
    if (!ctx || !ctx->is_running) {
        return;
    }
    
    // Remove service
    mdns_service_remove("_midi2", "_udp");
    
    // Free mDNS
    mdns_free();
    
    ctx->is_running = false;
    ESP_LOGI(TAG, "mDNS discovery stopped");
}

bool mdns_discovery_send_query(mdns_discovery_context_t* ctx) {
    if (!ctx || !ctx->is_running) {
        return false;
    }
    
    // Query for MIDI 2.0 devices
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr("_midi2", "_udp", 5000, MAX_DISCOVERED_DEVICES, &results);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS query failed: %s", esp_err_to_name(err));
        return false;
    }
    
    if (!results) {
        ESP_LOGD(TAG, "No MIDI 2.0 devices found");
        return true;
    }
    
    // Process results
    if (xSemaphoreTake(ctx->device_list_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        ctx->discovered_count = 0;
        
        mdns_result_t *r = results;
        while (r && ctx->discovered_count < MAX_DISCOVERED_DEVICES) {
            discovered_device_t* dev = &ctx->discovered_devices[ctx->discovered_count];
            
            if (r->hostname) {
                strncpy(dev->hostname, r->hostname, sizeof(dev->hostname) - 1);
            }
            
            if (r->instance_name) {
                strncpy(dev->instance_name, r->instance_name, sizeof(dev->instance_name) - 1);
            }
            
            dev->port = r->port;
            
            // Get IP address (first one)
            if (r->addr) {
                dev->ip_address = r->addr->addr.u_addr.ip4.addr;
            }
            
            ctx->discovered_count++;
            r = r->next;
        }
        
        xSemaphoreGive(ctx->device_list_mutex);
        ESP_LOGI(TAG, "Discovered %d MIDI 2.0 devices", ctx->discovered_count);
    }
    
    // Free results
    mdns_query_results_free(results);
    
    return true;
}

int mdns_discovery_get_device_count(mdns_discovery_context_t* ctx) {
    if (!ctx) return 0;
    
    int count = 0;
    if (xSemaphoreTake(ctx->device_list_mutex, pdMS_TO_TICKS(100))) {
        count = ctx->discovered_count;
        xSemaphoreGive(ctx->device_list_mutex);
    }
    return count;
}

bool mdns_discovery_get_device_info(mdns_discovery_context_t* ctx, int index,
                                     uint32_t* ip_address, uint16_t* port,
                                     char* device_name, size_t name_len) {
    if (!ctx || index < 0) return false;
    
    bool success = false;
    if (xSemaphoreTake(ctx->device_list_mutex, pdMS_TO_TICKS(100))) {
        if (index < ctx->discovered_count) {
            discovered_device_t* dev = &ctx->discovered_devices[index];
            
            if (ip_address) *ip_address = dev->ip_address;
            if (port) *port = dev->port;
            if (device_name && name_len > 0) {
                strncpy(device_name, dev->hostname, name_len - 1);
                device_name[name_len - 1] = '\0';
            }
            success = true;
        }
        xSemaphoreGive(ctx->device_list_mutex);
    }
    return success;
}