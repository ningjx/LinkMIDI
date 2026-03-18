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
    bool enable_test_sender;    ///< 是否启用测试发送器 (每秒发送 C4)
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

#ifdef __cplusplus
}
#endif

#endif // APP_CORE_H