/**
 * @file web_config_server.h
 * @brief Web配置服务器接口
 * 
 * 提供WiFi配网和设备管理的Web界面
 */

#ifndef WEB_CONFIG_SERVER_H
#define WEB_CONFIG_SERVER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "midi_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Web服务器配置
 */
typedef struct {
    uint16_t port;              /**< HTTP服务器端口 */
    bool enable_captive_portal; /**< 启用强制门户 */
} web_server_config_t;

/**
 * @brief 状态获取回调类型
 */
typedef struct {
    // WiFi 状态回调
    int (*wifi_get_mode)(void);  // Returns wifi_run_mode_t
    bool (*wifi_is_connected)(void);
    void (*wifi_get_ip)(char* ip_str, size_t len);
    int (*wifi_get_rssi)(void);
    const char* (*wifi_get_ssid)(void);
    
    // USB 状态回调
    uint8_t (*usb_get_device_count)(void);
    bool (*usb_get_device_info)(uint8_t index, void* dev_info);
    bool (*usb_is_running)(void);
    
    // NM2 状态回调
    bool (*nm2_is_session_active)(void);
    const char* (*nm2_get_remote_name)(void);
    bool (*nm2_get_session_info)(void* session);
    bool (*nm2_send_midi)(uint8_t status, uint8_t data1, uint8_t data2);
} web_status_callbacks_t;

/**
 * @brief 初始化Web配置服务器
 * @param config 服务器配置
 * @return MIDI_OK 成功
 */
midi_error_t web_config_server_init(const web_server_config_t* config);

/**
 * @brief 注册状态回调函数
 * @param callbacks 回调函数结构体
 * @return MIDI_OK 成功
 */
midi_error_t web_config_server_register_callbacks(const web_status_callbacks_t* callbacks);

/**
 * @brief 启动Web服务器
 * @return MIDI_OK 成功
 */
midi_error_t web_config_server_start(void);

/**
 * @brief 停止Web服务器
 * @return MIDI_OK 成功
 */
midi_error_t web_config_server_stop(void);

/**
 * @brief 反初始化Web服务器
 */
void web_config_server_deinit(void);

/**
 * @brief 检查Web服务器是否运行
 * @return true 运行中
 */
bool web_config_server_is_running(void);

/**
 * @brief 启动SoftAP配网模式
 * @param ssid AP的SSID
 * @param password AP的密码
 * @return MIDI_OK 成功
 */
midi_error_t web_config_start_ap_mode(const char* ssid, const char* password);

/**
 * @brief 停止SoftAP模式
 * @return MIDI_OK 成功
 */
midi_error_t web_config_stop_ap_mode(void);

#ifdef __cplusplus
}
#endif

#endif // WEB_CONFIG_SERVER_H