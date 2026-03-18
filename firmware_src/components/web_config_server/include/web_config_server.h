/**
 * @file web_config_server.h
 * @brief Web配置服务器接口
 * 
 * 提供WiFi配网和设备管理的Web界面
 */

#ifndef WEB_CONFIG_SERVER_H
#define WEB_CONFIG_SERVER_H

#include <stdbool.h>
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
 * @brief 初始化Web配置服务器
 * @param config 服务器配置
 * @return MIDI_OK 成功
 */
midi_error_t web_config_server_init(const web_server_config_t* config);

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