/**
 * @file nm2_discovery.c
 * @brief MIDI 2.0 设备发现实现
 * 
 * 使用 ESP-IDF 内置 mDNS 组件。
 */

#include "nm2_discovery.h"
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char* TAG = "NM2_DISC";

#define MIDI2_SERVICE_TYPE "_midi2"
#define MIDI2_SERVICE_PROTO "_udp"
#define MAX_DEVICES 16

/* ============================================================================
 * 内部结构
 * ============================================================================ */

struct nm2_discovery {
    char device_name[64];
    char product_id[64];
    uint16_t port;
    bool running;
    
    nm2_discovered_device_t devices[MAX_DEVICES];
    int device_count;
    SemaphoreHandle_t mutex;
    
    nm2_device_found_callback_t callback;
    void* callback_user_data;
};

/* ============================================================================
 * 内部函数
 * ============================================================================ */

static void process_query_results(nm2_discovery_t* discovery, mdns_result_t* results) {
    if (!discovery || !results) return;
    
    if (xSemaphoreTake(discovery->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }
    
    // 清空旧列表
    discovery->device_count = 0;
    
    // 遍历结果
    for (mdns_result_t* r = results; r && discovery->device_count < MAX_DEVICES; r = r->next) {
        if (r->ip_protocol != MDNS_IP_PROTOCOL_V4) continue;
        if (!r->hostname && !r->instance_name) continue;
        
        nm2_discovered_device_t* dev = &discovery->devices[discovery->device_count];
        
        // 复制设备名
        if (r->instance_name) {
            strncpy(dev->device_name, r->instance_name, sizeof(dev->device_name) - 1);
        } else if (r->hostname) {
            strncpy(dev->device_name, r->hostname, sizeof(dev->device_name) - 1);
        }
        
        // IP 地址
        if (r->addr) {
            dev->ip_address = r->addr->addr.u_addr.ip4.addr;
        }
        
        dev->port = r->port;
        
        discovery->device_count++;
        
        // 通知回调
        if (discovery->callback) {
            discovery->callback(dev, discovery->callback_user_data);
        }
    }
    
    xSemaphoreGive(discovery->mutex);
    
    ESP_LOGI(TAG, "Discovered %d devices", discovery->device_count);
}

/* ============================================================================
 * 公共 API
 * ============================================================================ */

nm2_discovery_t* nm2_discovery_create(const char* device_name,
                                       const char* product_id,
                                       uint16_t port) {
    nm2_discovery_t* discovery = calloc(1, sizeof(nm2_discovery_t));
    if (!discovery) return NULL;
    
    discovery->mutex = xSemaphoreCreateMutex();
    if (!discovery->mutex) {
        free(discovery);
        return NULL;
    }
    
    if (device_name) {
        strncpy(discovery->device_name, device_name, sizeof(discovery->device_name) - 1);
    }
    if (product_id) {
        strncpy(discovery->product_id, product_id, sizeof(discovery->product_id) - 1);
    }
    discovery->port = port;
    
    return discovery;
}

void nm2_discovery_destroy(nm2_discovery_t* discovery) {
    if (!discovery) return;
    
    nm2_discovery_stop(discovery);
    
    if (discovery->mutex) {
        vSemaphoreDelete(discovery->mutex);
    }
    free(discovery);
}

midi_error_t nm2_discovery_start(nm2_discovery_t* discovery) {
    if (!discovery) return MIDI_ERR_INVALID_ARG;
    if (discovery->running) return MIDI_ERR_ALREADY_INITIALIZED;
    
    // 初始化 mDNS
    esp_err_t err = mdns_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(err));
        return MIDI_ERR_NET_MDNS_FAILED;
    }
    
    // 设置主机名
    mdns_hostname_set(discovery->device_name);
    
    // 添加服务
    err = mdns_service_add(discovery->device_name, 
                           MIDI2_SERVICE_TYPE, MIDI2_SERVICE_PROTO,
                           discovery->port, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_ARG) {
        ESP_LOGW(TAG, "mDNS service add failed: %s", esp_err_to_name(err));
    }
    
    discovery->running = true;
    ESP_LOGI(TAG, "Discovery started: %s:%d", discovery->device_name, discovery->port);
    
    return MIDI_OK;
}

midi_error_t nm2_discovery_stop(nm2_discovery_t* discovery) {
    if (!discovery) return MIDI_ERR_INVALID_ARG;
    if (!discovery->running) return MIDI_OK;
    
    mdns_service_remove(MIDI2_SERVICE_TYPE, MIDI2_SERVICE_PROTO);
    mdns_free();
    
    discovery->running = false;
    ESP_LOGI(TAG, "Discovery stopped");
    
    return MIDI_OK;
}

void nm2_discovery_set_callback(nm2_discovery_t* discovery,
                                 nm2_device_found_callback_t callback,
                                 void* user_data) {
    if (!discovery) return;
    discovery->callback = callback;
    discovery->callback_user_data = user_data;
}

midi_error_t nm2_discovery_send_query(nm2_discovery_t* discovery) {
    if (!discovery) return MIDI_ERR_INVALID_ARG;
    if (!discovery->running) return MIDI_ERR_NOT_INITIALIZED;
    
    mdns_result_t* results = NULL;
    esp_err_t err = mdns_query_ptr(MIDI2_SERVICE_TYPE, MIDI2_SERVICE_PROTO, 3000, 20, &results);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Query failed: %s", esp_err_to_name(err));
        return MIDI_ERR_NET_MDNS_FAILED;
    }
    
    // 处理结果
    if (results) {
        process_query_results(discovery, results);
        mdns_query_results_free(results);
    }
    
    ESP_LOGI(TAG, "Sent discovery query");
    return MIDI_OK;
}

int nm2_discovery_get_device_count(const nm2_discovery_t* discovery) {
    if (!discovery) return 0;
    return discovery->device_count;
}

bool nm2_discovery_get_device(const nm2_discovery_t* discovery, int index,
                               nm2_discovered_device_t* device) {
    if (!discovery || !device) return false;
    if (index < 0 || index >= discovery->device_count) return false;
    
    *device = discovery->devices[index];
    return true;
}

void nm2_discovery_clear_devices(nm2_discovery_t* discovery) {
    if (!discovery) return;
    
    if (xSemaphoreTake(discovery->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        discovery->device_count = 0;
        xSemaphoreGive(discovery->mutex);
    }
}