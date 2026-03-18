/**
 * @file app_core.c
 * @brief 应用核心模块实现
 * 
 * 使用事件总线解耦各模块通信，移除全局变量。
 */

#include "app_core.h"
#include "app_config.h"
#include "event_bus.h"
#include "network_midi2.h"
#include "mdns_discovery.h"
#include "wifi_manager.h"
#include "usb_midi_host.h"
#include "config_manager.h"
#include "web_config_server.h"
#include "ota_manager.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "app_core";

/* ============================================================================
 * 模块上下文
 * ============================================================================ */

typedef struct {
    bool initialized;
    bool running;
    bool session_active;
    
    /* 同步保护 */
    SemaphoreHandle_t mutex;
    
    /* 任务句柄 */
    TaskHandle_t session_monitor_task;
    TaskHandle_t test_midi_task;
    
    /* 模块句柄 */
    network_midi2_context_t* midi2_ctx;
    mdns_discovery_context_t* mdns_ctx;
    usb_midi_host_context_t* usb_midi_ctx;
    
    /* 事件订阅 */
    uint32_t sub_midi_data;
    uint32_t sub_usb_connected;
    uint32_t sub_usb_disconnected;
    uint32_t sub_session_established;
    uint32_t sub_session_terminated;
    uint32_t sub_wifi_connected;
    uint32_t sub_wifi_disconnected;
    
    /* 配置 */
    app_config_t config;
} app_context_t;

static app_context_t g_app = {0};

/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

/**
 * @brief 安全设置会话状态
 */
static void set_session_active(bool active) {
    if (xSemaphoreTake(g_app.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_app.session_active = active;
        xSemaphoreGive(g_app.mutex);
    }
}

/**
 * @brief 安全获取会话状态
 */
static bool get_session_active(void) {
    bool active = false;
    if (xSemaphoreTake(g_app.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        active = g_app.session_active;
        xSemaphoreGive(g_app.mutex);
    }
    return active;
}

/* ============================================================================
 * 事件处理
 * ============================================================================ */

/**
 * @brief MIDI 数据事件处理 - 转发到网络
 */
static void on_midi_data_event(const event_t* event, void* user_data) {
    const event_midi_data_t* midi = &event->data.midi;
    
    ESP_LOGI(TAG, "[MIDI_RX] Source=%d, Len=%d, Status=0x%02X",
             midi->source, midi->length, midi->length > 0 ? midi->data[0] : 0);
    
    // 如果来自 USB 且会话激活，转发到网络
    if (midi->source == 0 && get_session_active() && g_app.midi2_ctx) {
        if (midi->length >= 3) {
            midi_error_t err = network_midi2_send_midi(
                g_app.midi2_ctx,
                midi->data[0],  // status
                midi->data[1],  // data1
                midi->data[2]   // data2
            );
            if (err != MIDI_OK) {
                ESP_LOGW(TAG, "Failed to forward MIDI: %s", midi_error_str(err));
            } else {
                ESP_LOGI(TAG, "[MIDI_FWD] Forwarded to network");
            }
        }
    }
}

/**
 * @brief USB 设备连接事件处理
 */
static void on_usb_connected_event(const event_t* event, void* user_data) {
    const event_usb_device_t* dev = &event->data.usb_device;
    ESP_LOGI(TAG, "[USB] Device %d connected: %s %s (VID:0x%04X PID:0x%04X)",
             dev->device_index, dev->manufacturer, dev->product_name,
             dev->vendor_id, dev->product_id);
}

/**
 * @brief USB 设备断开事件处理
 */
static void on_usb_disconnected_event(const event_t* event, void* user_data) {
    const event_usb_device_t* dev = &event->data.usb_device;
    ESP_LOGI(TAG, "[USB] Device %d disconnected", dev->device_index);
}

/**
 * @brief 会话建立事件处理
 */
static void on_session_established_event(const event_t* event, void* user_data) {
    const event_session_t* session = &event->data.session;
    ESP_LOGI(TAG, "=== Session ESTABLISHED === (SSRC: 0x%08X, Name: %s)",
             (unsigned int)session->remote_ssrc, session->remote_name);
    set_session_active(true);
}

/**
 * @brief 会话终止事件处理
 */
static void on_session_terminated_event(const event_t* event, void* user_data) {
    ESP_LOGI(TAG, "=== Session CLOSED ===");
    set_session_active(false);
}

/**
 * @brief WiFi 连接事件处理
 */
static void on_wifi_connected_event(const event_t* event, void* user_data) {
    ESP_LOGI(TAG, "[WiFi] Connected");
}

/**
 * @brief WiFi 断开事件处理
 */
static void on_wifi_disconnected_event(const event_t* event, void* user_data) {
    ESP_LOGW(TAG, "[WiFi] Disconnected, reason: %d", event->data.wifi.reason);
}

/* ============================================================================
 * USB MIDI Host 回调 -> 转换为事件
 * ============================================================================ */

static void usb_midi_rx_callback(uint8_t device_index, const uint8_t* data, uint16_t length) {
    // 发布 MIDI 数据事件 (source=0 表示 USB)
    event_bus_publish_midi_data(0, device_index, data, length);
}

static void usb_device_connected_callback(uint8_t device_index, const usb_midi_device_t* info) {
    event_bus_publish_usb_connected(
        device_index,
        info->vendor_id,
        info->product_id,
        info->manufacturer,
        info->product_name
    );
}

static void usb_device_disconnected_callback(uint8_t device_index) {
    event_bus_publish_usb_disconnected(device_index);
}

/* ============================================================================
 * Network MIDI2 回调 -> 转换为事件
 * ============================================================================ */

static void midi2_midi_rx_callback(const uint8_t* data, uint16_t length) {
    // 发布 MIDI 数据事件 (source=1 表示网络)
    event_bus_publish_midi_data(1, 0, data, length);
}

static void midi2_ump_rx_callback(const uint8_t* data, uint16_t length) {
    event_bus_publish_ump_data(data, length);
}

static void midi2_log_callback(const char* message) {
    ESP_LOGI(TAG, "[MIDI2] %s", message);
}

/* ============================================================================
 * 会话监控任务
 * ============================================================================ */

static void session_monitor_task(void* arg) {
    ESP_LOGI(TAG, "Session monitor task started");
    
    while (g_app.running) {
        vTaskDelay(pdMS_TO_TICKS(APP_SESSION_CHECK_INTERVAL_MS));
        
        if (!g_app.midi2_ctx) continue;
        
        bool is_active = network_midi2_is_session_active(g_app.midi2_ctx);
        
        bool session_was_active = get_session_active();
        
        if (is_active && !session_was_active) {
            // 会话新建 - 发布事件
            event_bus_publish_session_established(0, "remote");
        } else if (!is_active && session_was_active) {
            // 会话关闭 - 发布事件
            event_bus_publish_session_terminated();
        }
    }
    
    vTaskDelete(NULL);
}

/* ============================================================================
 * 测试 MIDI 发送任务
 * ============================================================================ */

static void test_midi_send_task(void* arg) {
    ESP_LOGI(TAG, "Test MIDI send task started");
    
    uint8_t note = 60;      // C4
    uint8_t velocity = 100;
    uint8_t channel = 0;
    bool note_on = true;
    
    while (g_app.running) {
        vTaskDelay(pdMS_TO_TICKS(APP_MIDI_SEND_INTERVAL_MS));
        
        if (get_session_active() && g_app.midi2_ctx) {
            if (note_on) {
                ESP_LOGI(TAG, "[TEST] Sending Note ON - C4");
                network_midi2_send_note_on(g_app.midi2_ctx, note, velocity, channel);
                note_on = false;
            } else {
                ESP_LOGI(TAG, "[TEST] Sending Note OFF - C4");
                network_midi2_send_note_off(g_app.midi2_ctx, note, 0, channel);
                note_on = true;
            }
        }
    }
    
    vTaskDelete(NULL);
}

/* ============================================================================
 * 公共 API
 * ============================================================================ */

midi_error_t app_core_init(const app_config_t* config) {
    if (g_app.initialized) {
        return MIDI_ERR_ALREADY_INITIALIZED;
    }
    
    // 初始化配置管理器
    midi_error_t err = config_manager_init();
    if (err != MIDI_OK) {
        ESP_LOGE(TAG, "Failed to init config manager: %s", midi_error_str(err));
        return err;
    }
    
    // 加载配置
    system_config_t sys_config;
    if (config_manager_is_configured()) {
        err = config_manager_load(&sys_config);
        if (err != MIDI_OK) {
            ESP_LOGW(TAG, "Failed to load config, using defaults");
            config_manager_get_defaults(&sys_config);
        }
    } else {
        // 使用传入的配置或默认配置
        if (config) {
            // 从传入的配置创建系统配置
            memset(&sys_config, 0, sizeof(sys_config));
            strncpy(sys_config.wifi.ssid, CONFIG_WIFI_SSID, sizeof(sys_config.wifi.ssid) - 1);
            strncpy(sys_config.wifi.password, CONFIG_WIFI_PASSWORD, sizeof(sys_config.wifi.password) - 1);
            sys_config.wifi.max_retry = CONFIG_WIFI_MAXIMUM_RETRY;
            sys_config.wifi.auto_connect = true;
            
            strncpy(sys_config.midi.device_name, config->device_name, sizeof(sys_config.midi.device_name) - 1);
            strncpy(sys_config.midi.product_id, config->product_id, sizeof(sys_config.midi.product_id) - 1);
            sys_config.midi.listen_port = config->listen_port;
            sys_config.midi.device_mode = 1;  // Server mode
            sys_config.midi.enable_discovery = true;
            
            // 保存配置
            config_manager_save(&sys_config);
        } else {
            config_manager_get_defaults(&sys_config);
        }
    }
    
    memset(&g_app, 0, sizeof(g_app));
    
    // 使用配置管理器加载的配置
    g_app.config.device_name = sys_config.midi.device_name;
    g_app.config.product_id = sys_config.midi.product_id;
    g_app.config.listen_port = sys_config.midi.listen_port;
    g_app.config.enable_test_sender = config ? config->enable_test_sender : false;
    
    // 创建互斥锁
    g_app.mutex = xSemaphoreCreateMutex();
    if (!g_app.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        config_manager_deinit();
        return MIDI_ERR_NO_MEM;
    }
    
    // 初始化事件总线
    err = event_bus_init();
    if (err != MIDI_OK) {
        ESP_LOGE(TAG, "Failed to init event bus: %s", midi_error_str(err));
        config_manager_deinit();
        return err;
    }
    
    // 订阅事件
    g_app.sub_midi_data = event_bus_subscribe(EVENT_MIDI_DATA_RECEIVED, on_midi_data_event, NULL);
    g_app.sub_usb_connected = event_bus_subscribe(EVENT_USB_DEVICE_CONNECTED, on_usb_connected_event, NULL);
    g_app.sub_usb_disconnected = event_bus_subscribe(EVENT_USB_DEVICE_DISCONNECTED, on_usb_disconnected_event, NULL);
    g_app.sub_session_established = event_bus_subscribe(EVENT_SESSION_ESTABLISHED, on_session_established_event, NULL);
    g_app.sub_session_terminated = event_bus_subscribe(EVENT_SESSION_TERMINATED, on_session_terminated_event, NULL);
    g_app.sub_wifi_connected = event_bus_subscribe(EVENT_WIFI_CONNECTED, on_wifi_connected_event, NULL);
    g_app.sub_wifi_disconnected = event_bus_subscribe(EVENT_WIFI_DISCONNECTED, on_wifi_disconnected_event, NULL);
    
    ESP_LOGI(TAG, "App core initialized");
    ESP_LOGI(TAG, "  Device: %s", g_app.config.device_name);
    ESP_LOGI(TAG, "  Port: %d", g_app.config.listen_port);
    g_app.initialized = true;
    
    return MIDI_OK;
}

midi_error_t app_core_start(void) {
    if (!g_app.initialized) {
        return MIDI_ERR_NOT_INITIALIZED;
    }
    
    if (g_app.running) {
        return MIDI_ERR_ALREADY_INITIALIZED;
    }
    
    // 1. 初始化 WiFi
    ESP_LOGI(TAG, "Initializing WiFi...");
    if (!wifi_manager_init()) {
        ESP_LOGE(TAG, "Failed to init WiFi");
        return MIDI_ERR_NET_INIT_FAILED;
    }
    
    if (!wifi_manager_connect()) {
        ESP_LOGE(TAG, "Failed to configure WiFi");
        return MIDI_ERR_CONFIG_INVALID;
    }
    
    if (!wifi_manager_wait_for_connection(10000)) {
        ESP_LOGW(TAG, "WiFi connection timeout, continuing anyway...");
    }
    
    // 发布 WiFi 连接事件
    if (wifi_manager_is_connected()) {
        event_bus_publish_wifi_connected();
    }
    
    // 2. 初始化 OTA 管理器
    ESP_LOGI(TAG, "Initializing OTA manager...");
    midi_error_t err = ota_manager_init();
    if (err != MIDI_OK) {
        ESP_LOGW(TAG, "Failed to init OTA manager, continuing without OTA");
    }
    
    // 检查是否需要验证OTA
    if (ota_manager_needs_validation()) {
        ESP_LOGI(TAG, "Validating OTA update...");
        ota_manager_mark_valid();
    }
    
    // 3. 初始化 Web 配置服务器
    ESP_LOGI(TAG, "Initializing Web config server...");
    web_server_config_t web_config = {
        .port = 80,
        .enable_captive_portal = true,
    };
    err = web_config_server_init(&web_config);
    if (err != MIDI_OK) {
        ESP_LOGW(TAG, "Failed to init Web server, continuing without Web UI");
    } else {
        // 启动Web服务器
        err = web_config_server_start();
        if (err != MIDI_OK) {
            ESP_LOGW(TAG, "Failed to start Web server");
        } else {
            ESP_LOGI(TAG, "Web config server started on port 80");
        }
    }
    
    // 4. 初始化 Network MIDI 2.0
    ESP_LOGI(TAG, "Initializing Network MIDI 2.0...");
    network_midi2_config_t midi2_cfg = {
        .device_name = g_app.config.device_name,
        .product_id = g_app.config.product_id,
        .listen_port = g_app.config.listen_port,
        .mode = MODE_SERVER,
        .enable_discovery = true,
        .log_callback = midi2_log_callback,
        .midi_rx_callback = midi2_midi_rx_callback,
        .ump_rx_callback = midi2_ump_rx_callback,
    };
    
    g_app.midi2_ctx = network_midi2_init_with_config(&midi2_cfg);
    if (!g_app.midi2_ctx) {
        ESP_LOGE(TAG, "Failed to init Network MIDI 2.0");
        return MIDI_ERR_NO_MEM;
    }
    
    // 5. 初始化 mDNS 发现
    ESP_LOGI(TAG, "Initializing mDNS discovery...");
    g_app.mdns_ctx = mdns_discovery_init(
        g_app.config.device_name,
        g_app.config.product_id,
        g_app.config.listen_port
    );
    if (!g_app.mdns_ctx) {
        ESP_LOGE(TAG, "Failed to init mDNS");
        network_midi2_deinit(g_app.midi2_ctx);
        g_app.midi2_ctx = NULL;
        return MIDI_ERR_NET_MDNS_FAILED;
    }
    
    if (!mdns_discovery_start(g_app.mdns_ctx)) {
        ESP_LOGE(TAG, "Failed to start mDNS");
        mdns_discovery_deinit(g_app.mdns_ctx);
        network_midi2_deinit(g_app.midi2_ctx);
        g_app.midi2_ctx = NULL;
        g_app.mdns_ctx = NULL;
        return MIDI_ERR_NET_MDNS_FAILED;
    }
    
    // 6. 启动 MIDI 2.0 服务
    if (!network_midi2_start(g_app.midi2_ctx)) {
        ESP_LOGE(TAG, "Failed to start MIDI 2.0");
        mdns_discovery_stop(g_app.mdns_ctx);
        mdns_discovery_deinit(g_app.mdns_ctx);
        network_midi2_deinit(g_app.midi2_ctx);
        g_app.midi2_ctx = NULL;
        g_app.mdns_ctx = NULL;
        return MIDI_ERR_SESSION_INIT_FAILED;
    }
    
    // 7. 初始化 USB MIDI Host
    ESP_LOGI(TAG, "Initializing USB MIDI Host...");
    usb_midi_host_config_t usb_cfg = {
        .midi_rx_callback = usb_midi_rx_callback,
        .device_connected_callback = usb_device_connected_callback,
        .device_disconnected_callback = usb_device_disconnected_callback,
    };
    
    g_app.usb_midi_ctx = usb_midi_host_init(&usb_cfg);
    if (!g_app.usb_midi_ctx) {
        ESP_LOGW(TAG, "USB MIDI Host init failed, continuing without USB");
    } else if (!usb_midi_host_start(g_app.usb_midi_ctx)) {
        ESP_LOGW(TAG, "USB MIDI Host start failed");
        usb_midi_host_deinit(g_app.usb_midi_ctx);
        g_app.usb_midi_ctx = NULL;
    }
    
    // 8. 创建监控任务
    g_app.running = true;
    
    BaseType_t ret;
    ret = xTaskCreate(session_monitor_task, "session_mon", APP_SESSION_MONITOR_STACK, 
                NULL, APP_SESSION_MONITOR_PRIORITY, &g_app.session_monitor_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create session monitor task");
        g_app.running = false;
        // 清理已初始化的资源
        if (g_app.usb_midi_ctx) {
            usb_midi_host_stop(g_app.usb_midi_ctx);
            usb_midi_host_deinit(g_app.usb_midi_ctx);
            g_app.usb_midi_ctx = NULL;
        }
        network_midi2_stop(g_app.midi2_ctx);
        mdns_discovery_stop(g_app.mdns_ctx);
        mdns_discovery_deinit(g_app.mdns_ctx);
        network_midi2_deinit(g_app.midi2_ctx);
        g_app.midi2_ctx = NULL;
        g_app.mdns_ctx = NULL;
        return MIDI_ERR_NO_MEM;
    }
    
    if (g_app.config.enable_test_sender) {
        ret = xTaskCreate(test_midi_send_task, "test_midi", APP_MIDI_SEND_STACK,
                    NULL, APP_MIDI_SEND_PRIORITY, &g_app.test_midi_task);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create test MIDI task");
            // 非致命错误，继续运行
            g_app.test_midi_task = NULL;
        }
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "LinkMIDI Service RUNNING");
    ESP_LOGI(TAG, "Device: %s", g_app.config.device_name);
    ESP_LOGI(TAG, "Port: %d", g_app.config.listen_port);
    ESP_LOGI(TAG, "========================================");
    
    return MIDI_OK;
}

midi_error_t app_core_stop(void) {
    if (!g_app.running) {
        return MIDI_ERR_NOT_INITIALIZED;
    }
    
    g_app.running = false;
    
    // 等待任务结束
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // 删除任务（如果还在运行）
    if (g_app.session_monitor_task) {
        vTaskDelete(g_app.session_monitor_task);
        g_app.session_monitor_task = NULL;
    }
    
    if (g_app.test_midi_task) {
        vTaskDelete(g_app.test_midi_task);
        g_app.test_midi_task = NULL;
    }
    
    // 停止各模块
    // 停止Web服务器
    web_config_server_stop();
    
    // 停止USB MIDI
    if (g_app.usb_midi_ctx) {
        usb_midi_host_stop(g_app.usb_midi_ctx);
        usb_midi_host_deinit(g_app.usb_midi_ctx);
        g_app.usb_midi_ctx = NULL;
    }
    
    if (g_app.midi2_ctx) {
        network_midi2_stop(g_app.midi2_ctx);
        network_midi2_deinit(g_app.midi2_ctx);
        g_app.midi2_ctx = NULL;
    }
    
    if (g_app.mdns_ctx) {
        mdns_discovery_stop(g_app.mdns_ctx);
        mdns_discovery_deinit(g_app.mdns_ctx);
        g_app.mdns_ctx = NULL;
    }
    
    wifi_manager_deinit();
    
    ESP_LOGI(TAG, "App core stopped");
    return MIDI_OK;
}

midi_error_t app_core_deinit(void) {
    if (g_app.running) {
        app_core_stop();
    }
    
    if (!g_app.initialized) {
        return MIDI_ERR_NOT_INITIALIZED;
    }
    
    // 取消事件订阅
    event_bus_unsubscribe(g_app.sub_midi_data);
    event_bus_unsubscribe(g_app.sub_usb_connected);
    event_bus_unsubscribe(g_app.sub_usb_disconnected);
    event_bus_unsubscribe(g_app.sub_session_established);
    event_bus_unsubscribe(g_app.sub_session_terminated);
    event_bus_unsubscribe(g_app.sub_wifi_connected);
    event_bus_unsubscribe(g_app.sub_wifi_disconnected);
    
    event_bus_deinit();
    
    // 清理Web服务器
    web_config_server_deinit();
    
    // 清理OTA管理器
    ota_manager_deinit();
    
    // 清理配置管理器
    config_manager_deinit();
    
    // 删除互斥锁
    if (g_app.mutex) {
        vSemaphoreDelete(g_app.mutex);
    }
    
    memset(&g_app, 0, sizeof(g_app));
    
    ESP_LOGI(TAG, "App core deinitialized");
    return MIDI_OK;
}

bool app_core_is_running(void) {
    return g_app.running;
}

bool app_core_is_session_active(void) {
    return get_session_active();
}