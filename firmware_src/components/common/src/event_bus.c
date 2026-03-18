/**
 * @file event_bus.c
 * @brief 事件总线实现
 */

#include "event_bus.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "event_bus";

/* ============================================================================
 * 配置常量
 * ============================================================================ */

#define MAX_SUBSCRIBERS_PER_EVENT   8   ///< 每种事件的最大订阅者数
#define MAX_TOTAL_SUBSCRIPTIONS     32  ///< 总订阅数上限

/* ============================================================================
 * 内部数据结构
 * ============================================================================ */

/**
 * @brief 订阅记录
 */
typedef struct {
    uint32_t id;                    ///< 订阅 ID (0 表示空槽)
    event_type_t type;              ///< 事件类型
    event_callback_t callback;      ///< 回调函数
    void* user_data;                ///< 用户数据
} subscription_t;

/**
 * @brief 事件总线上下文
 */
typedef struct {
    bool initialized;                           ///< 初始化标志
    SemaphoreHandle_t mutex;                    ///< 互斥锁
    subscription_t subscriptions[MAX_TOTAL_SUBSCRIPTIONS]; ///< 订阅数组
    uint32_t next_id;                           ///< 下一个订阅 ID
} event_bus_context_t;

/* ============================================================================
 * 全局上下文
 * ============================================================================ */

static event_bus_context_t g_event_bus = {0};

/* ============================================================================
 * 内部函数
 * ============================================================================ */

/**
 * @brief 获取当前时间戳 (ms)
 */
static uint32_t get_timestamp_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief 查找空闲订阅槽
 */
static int find_free_slot(void) {
    for (int i = 0; i < MAX_TOTAL_SUBSCRIPTIONS; i++) {
        if (g_event_bus.subscriptions[i].id == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief 查找订阅槽
 */
static int find_subscription(uint32_t id) {
    for (int i = 0; i < MAX_TOTAL_SUBSCRIPTIONS; i++) {
        if (g_event_bus.subscriptions[i].id == id) {
            return i;
        }
    }
    return -1;
}

/* ============================================================================
 * 公共 API 实现
 * ============================================================================ */

midi_error_t event_bus_init(void) {
    if (g_event_bus.initialized) {
        return MIDI_ERR_ALREADY_INITIALIZED;
    }
    
    memset(&g_event_bus, 0, sizeof(g_event_bus));
    
    g_event_bus.mutex = xSemaphoreCreateMutex();
    if (!g_event_bus.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return MIDI_ERR_NO_MEM;
    }
    
    g_event_bus.next_id = 1;
    g_event_bus.initialized = true;
    
    ESP_LOGI(TAG, "Event bus initialized");
    return MIDI_OK;
}

midi_error_t event_bus_deinit(void) {
    if (!g_event_bus.initialized) {
        return MIDI_ERR_NOT_INITIALIZED;
    }
    
    if (xSemaphoreTake(g_event_bus.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(g_event_bus.subscriptions, 0, sizeof(g_event_bus.subscriptions));
        xSemaphoreGive(g_event_bus.mutex);
    }
    
    vSemaphoreDelete(g_event_bus.mutex);
    memset(&g_event_bus, 0, sizeof(g_event_bus));
    
    ESP_LOGI(TAG, "Event bus deinitialized");
    return MIDI_OK;
}

bool event_bus_is_initialized(void) {
    return g_event_bus.initialized;
}

uint32_t event_bus_subscribe(event_type_t type, event_callback_t callback, void* user_data) {
    if (!g_event_bus.initialized) {
        ESP_LOGE(TAG, "Event bus not initialized");
        return 0;
    }
    
    if (!callback) {
        ESP_LOGE(TAG, "Callback is NULL");
        return 0;
    }
    
    uint32_t id = 0;
    
    if (xSemaphoreTake(g_event_bus.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // 检查该事件的订阅者数量
        int count = 0;
        for (int i = 0; i < MAX_TOTAL_SUBSCRIPTIONS; i++) {
            if (g_event_bus.subscriptions[i].id != 0 && 
                g_event_bus.subscriptions[i].type == type) {
                count++;
            }
        }
        
        if (count >= MAX_SUBSCRIBERS_PER_EVENT) {
            ESP_LOGW(TAG, "Max subscribers reached for event type %d", type);
            xSemaphoreGive(g_event_bus.mutex);
            return 0;
        }
        
        // 查找空闲槽
        int slot = find_free_slot();
        if (slot < 0) {
            ESP_LOGW(TAG, "No free subscription slots");
            xSemaphoreGive(g_event_bus.mutex);
            return 0;
        }
        
        // 分配订阅
        id = g_event_bus.next_id++;
        g_event_bus.subscriptions[slot].id = id;
        g_event_bus.subscriptions[slot].type = type;
        g_event_bus.subscriptions[slot].callback = callback;
        g_event_bus.subscriptions[slot].user_data = user_data;
        
        xSemaphoreGive(g_event_bus.mutex);
        
        ESP_LOGD(TAG, "Subscribed to event %d, id=%lu", type, (unsigned long)id);
    }
    
    return id;
}

midi_error_t event_bus_unsubscribe(uint32_t subscription_id) {
    if (!g_event_bus.initialized) {
        return MIDI_ERR_NOT_INITIALIZED;
    }
    
    if (subscription_id == 0) {
        return MIDI_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_event_bus.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int slot = find_subscription(subscription_id);
        if (slot < 0) {
            xSemaphoreGive(g_event_bus.mutex);
            return MIDI_ERR_NOT_FOUND;
        }
        
        g_event_bus.subscriptions[slot].id = 0;
        g_event_bus.subscriptions[slot].type = 0;
        g_event_bus.subscriptions[slot].callback = NULL;
        g_event_bus.subscriptions[slot].user_data = NULL;
        
        xSemaphoreGive(g_event_bus.mutex);
        
        ESP_LOGD(TAG, "Unsubscribed id=%lu", (unsigned long)subscription_id);
        return MIDI_OK;
    }
    
    return MIDI_ERR_TIMEOUT;
}

midi_error_t event_bus_publish(const event_t* event) {
    if (!g_event_bus.initialized) {
        return MIDI_ERR_NOT_INITIALIZED;
    }
    
    if (!event) {
        return MIDI_ERR_INVALID_ARG;
    }
    
    // 复制订阅者列表（避免在回调中持有锁）
    event_callback_t callbacks[MAX_SUBSCRIBERS_PER_EVENT] = {0};
    void* user_datas[MAX_SUBSCRIBERS_PER_EVENT] = {0};
    int callback_count = 0;
    
    if (xSemaphoreTake(g_event_bus.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < MAX_TOTAL_SUBSCRIPTIONS && callback_count < MAX_SUBSCRIBERS_PER_EVENT; i++) {
            if (g_event_bus.subscriptions[i].id != 0 && 
                g_event_bus.subscriptions[i].type == event->type) {
                callbacks[callback_count] = g_event_bus.subscriptions[i].callback;
                user_datas[callback_count] = g_event_bus.subscriptions[i].user_data;
                callback_count++;
            }
        }
        xSemaphoreGive(g_event_bus.mutex);
    } else {
        return MIDI_ERR_TIMEOUT;
    }
    
    // 调用所有订阅者
    for (int i = 0; i < callback_count; i++) {
        if (callbacks[i]) {
            callbacks[i](event, user_datas[i]);
        }
    }
    
    return MIDI_OK;
}

/* ============================================================================
 * 便捷发布函数
 * ============================================================================ */

midi_error_t event_bus_publish_midi_data(uint8_t source, uint8_t device_index,
                                          const uint8_t* data, uint16_t length) {
    event_t event = {
        .type = EVENT_MIDI_DATA_RECEIVED,
        .timestamp = get_timestamp_ms(),
        .data.midi = {
            .source = source,
            .device_index = device_index,
            .data = data,
            .length = length,
        },
    };
    return event_bus_publish(&event);
}

midi_error_t event_bus_publish_ump_data(const uint8_t* data, uint16_t length) {
    event_t event = {
        .type = EVENT_UMP_DATA_RECEIVED,
        .timestamp = get_timestamp_ms(),
        .data.ump = {
            .data = data,
            .length = length,
        },
    };
    return event_bus_publish(&event);
}

midi_error_t event_bus_publish_usb_connected(uint8_t device_index, uint16_t vid, uint16_t pid,
                                              const char* manufacturer, const char* product_name) {
    event_t event = {
        .type = EVENT_USB_DEVICE_CONNECTED,
        .timestamp = get_timestamp_ms(),
    };
    event.data.usb_device.device_index = device_index;
    event.data.usb_device.vendor_id = vid;
    event.data.usb_device.product_id = pid;
    if (manufacturer) {
        strncpy(event.data.usb_device.manufacturer, manufacturer, sizeof(event.data.usb_device.manufacturer) - 1);
    }
    if (product_name) {
        strncpy(event.data.usb_device.product_name, product_name, sizeof(event.data.usb_device.product_name) - 1);
    }
    return event_bus_publish(&event);
}

midi_error_t event_bus_publish_usb_disconnected(uint8_t device_index) {
    event_t event = {
        .type = EVENT_USB_DEVICE_DISCONNECTED,
        .timestamp = get_timestamp_ms(),
    };
    event.data.usb_device.device_index = device_index;
    return event_bus_publish(&event);
}

midi_error_t event_bus_publish_session_established(uint32_t remote_ssrc, const char* remote_name) {
    event_t event = {
        .type = EVENT_SESSION_ESTABLISHED,
        .timestamp = get_timestamp_ms(),
    };
    event.data.session.remote_ssrc = remote_ssrc;
    if (remote_name) {
        strncpy(event.data.session.remote_name, remote_name, sizeof(event.data.session.remote_name) - 1);
    }
    return event_bus_publish(&event);
}

midi_error_t event_bus_publish_session_terminated(void) {
    event_t event = {
        .type = EVENT_SESSION_TERMINATED,
        .timestamp = get_timestamp_ms(),
    };
    return event_bus_publish(&event);
}

midi_error_t event_bus_publish_wifi_connected(void) {
    event_t event = {
        .type = EVENT_WIFI_CONNECTED,
        .timestamp = get_timestamp_ms(),
    };
    return event_bus_publish(&event);
}

midi_error_t event_bus_publish_wifi_disconnected(uint8_t reason) {
    event_t event = {
        .type = EVENT_WIFI_DISCONNECTED,
        .timestamp = get_timestamp_ms(),
    };
    event.data.wifi.reason = reason;
    return event_bus_publish(&event);
}

midi_error_t event_bus_publish_device_discovered(const char* name, const char* product_id,
                                                  uint32_t ip_addr, uint16_t port) {
    event_t event = {
        .type = EVENT_DEVICE_DISCOVERED,
        .timestamp = get_timestamp_ms(),
    };
    if (name) {
        strncpy(event.data.device.name, name, sizeof(event.data.device.name) - 1);
    }
    if (product_id) {
        strncpy(event.data.device.product_id, product_id, sizeof(event.data.device.product_id) - 1);
    }
    event.data.device.ip_addr = ip_addr;
    event.data.device.port = port;
    return event_bus_publish(&event);
}

midi_error_t event_bus_publish_error(event_type_t source, midi_error_t code, const char* message) {
    event_t event = {
        .type = EVENT_SYSTEM_ERROR,
        .timestamp = get_timestamp_ms(),
    };
    event.data.error.source_event = source;
    event.data.error.error_code = code;
    event.data.error.message = message;
    return event_bus_publish(&event);
}