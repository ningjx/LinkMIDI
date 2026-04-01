#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file wifi_manager.h
 * @brief WiFi Connection Management with Smart Provisioning
 * 
 * Features:
 * - Read WiFi credentials from NVS storage
 * - Auto-connect on startup
 * - Switch to AP mode if connection fails
 * - Web-based configuration in AP mode
 */

/* AP模式默认配置 */
#define WIFI_AP_SSID        "LinkMidi"
#define WIFI_AP_PASSWORD    "linkmidi"
#define WIFI_AP_CHANNEL     1
#define WIFI_AP_MAX_CONN    4

/* 连接超时配置 */
#define WIFI_CONNECT_TIMEOUT_MS     30000   // 30秒
#define WIFI_CONNECT_MAX_RETRY      5       // 最多5次重试

/**
 * @brief WiFi运行模式
 */
typedef enum {
    WIFI_RUN_MODE_NONE,         ///< 未初始化
    WIFI_RUN_MODE_STA_TRYING,   ///< STA模式尝试连接中
    WIFI_RUN_MODE_STA_CONNECTED,///< STA模式已连接
    WIFI_RUN_MODE_AP,           ///< AP模式（配网模式）
} wifi_run_mode_t;

/**
 * @brief WiFi连接结果回调
 */
typedef void (*wifi_connect_callback_t)(bool success, const char* ip);

/**
 * @brief 初始化WiFi管理器
 * @return true 成功
 */
bool wifi_manager_init(void);

/**
 * @brief 使用存储的凭据连接WiFi
 * @param ssid WiFi名称（NULL则使用存储的）
 * @param password WiFi密码（NULL则使用存储的）
 * @return true 开始连接
 */
bool wifi_manager_connect(const char* ssid, const char* password);

/**
 * @brief 等待WiFi连接
 * @param timeout_ms 超时时间（毫秒）
 * @return true 连接成功
 */
bool wifi_manager_wait_for_connection(uint32_t timeout_ms);

/**
 * @brief 启动AP模式（配网模式）
 * @return true 成功
 */
bool wifi_manager_start_ap_mode(void);

/**
 * @brief 停止AP模式
 */
void wifi_manager_stop_ap_mode(void);

/**
 * @brief 保存WiFi凭据到NVS
 * @param ssid WiFi名称
 * @param password WiFi密码
 * @return true 成功
 */
bool wifi_manager_save_credentials(const char* ssid, const char* password);

/**
 * @brief 获取当前WiFi模式
 */
wifi_run_mode_t wifi_manager_get_mode(void);

/**
 * @brief 检查是否已连接
 */
bool wifi_manager_is_connected(void);

/**
 * @brief 获取当前IP地址
 * @param ip_str 输出缓冲区（至少16字节）
 * @return true 成功获取
 */
bool wifi_manager_get_ip(char* ip_str);

/**
 * @brief 反初始化
 */
void wifi_manager_deinit(void);

#ifdef __cplusplus
}
#endif
