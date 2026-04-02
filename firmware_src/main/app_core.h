/**
 * @file app_core.h
 * @brief 应用核心模块 - 管理模块生命周期和事件处理
 * 
 * 通过事件总线解耦各模块，移除全局变量依赖。
 */

#ifndef APP_CORE_H
#define APP_CORE_H

#include <stdbool.h>
#include "midi_error.h"
#include "config_manager.h"  // 引入 system_config_t

// 前向声明
typedef struct network_midi2_context network_midi2_context_t;
typedef struct usb_midi_host_context usb_midi_host_context_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 应用配置
 */
typedef struct {
    const char* device_name;
    const char* product_id;
    uint16_t listen_port;
} app_config_t;

/**
 * @brief 初始化应用核心
 * @param config 应用配置
 * @return MIDI_OK 成功
 */
midi_error_t app_core_init(const app_config_t* config);

/**
 * @brief 启动应用
 * @return MIDI_OK 成功
 */
midi_error_t app_core_start(void);

/**
 * @brief 停止应用
 * @return MIDI_OK 成功
 */
midi_error_t app_core_stop(void);

/**
 * @brief 反初始化应用核心
 * @return MIDI_OK 成功
 */
midi_error_t app_core_deinit(void);

/**
 * @brief 检查应用是否运行
 * @return true 运行中
 */
bool app_core_is_running(void);

/**
 * @brief 检查会话是否激活
 * @return true 会话激活
 */
bool app_core_is_session_active(void);

/**
 * @brief 获取当前系统配置
 * @return 系统配置指针（只读）
 */
const system_config_t* app_core_get_config(void);

/**
 * @brief 更新 WiFi 配置并保存
 * @param ssid WiFi SSID
 * @param password WiFi 密码
 * @return MIDI_OK 成功
 */
midi_error_t app_core_update_wifi_config(const char* ssid, const char* password);

/**
 * @brief 更新 MIDI 配置并保存
 * @param device_name 设备名称
 * @param listen_port 监听端口
 * @return MIDI_OK 成功
 */
midi_error_t app_core_update_midi_config(const char* device_name, uint16_t listen_port);

/**
 * @brief 获取 Network MIDI2 上下文
 * @return 上下文指针（只读）
 */
network_midi2_context_t* app_core_get_midi2_context(void);

/**
 * @brief 获取 USB MIDI Host 上下文
 * @return 上下文指针（只读）
 */
usb_midi_host_context_t* app_core_get_usb_midi_context(void);

#ifdef __cplusplus
}
#endif

#endif // APP_CORE_H