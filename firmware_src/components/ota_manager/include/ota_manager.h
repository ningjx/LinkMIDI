/**
 * @file ota_manager.h
 * @brief OTA升级管理器接口
 * 
 * 支持AB分区OTA升级，带回滚保护
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include "midi_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA状态
 */
typedef enum {
    OTA_STATE_IDLE,             /**< 空闲 */
    OTA_STATE_DOWNLOADING,      /**< 下载中 */
    OTA_STATE_WRITING,          /**< 写入中 */
    OTA_STATE_VERIFYING,        /**< 验证中 */
    OTA_STATE_SUCCESS,          /**< 成功 */
    OTA_STATE_FAILED,           /**< 失败 */
} ota_state_t;

/**
 * @brief OTA进度回调
 */
typedef void (*ota_progress_cb_t)(ota_state_t state, int progress, const char* message);

/**
 * @brief 初始化OTA管理器
 * @return MIDI_OK 成功
 */
midi_error_t ota_manager_init(void);

/**
 * @brief 反初始化OTA管理器
 */
void ota_manager_deinit(void);

/**
 * @brief 从URL启动OTA升级
 * @param url 固件URL
 * @param callback 进度回调
 * @return MIDI_OK 成功
 */
midi_error_t ota_manager_start_from_url(const char* url, ota_progress_cb_t callback);

/**
 * @brief 从数据启动OTA升级
 * @param data 固件数据
 * @param len 数据长度
 * @param callback 进度回调
 * @return MIDI_OK 成功
 */
midi_error_t ota_manager_start_from_data(const uint8_t* data, size_t len, ota_progress_cb_t callback);

/**
 * @brief 获取当前OTA状态
 * @return OTA状态
 */
ota_state_t ota_manager_get_state(void);

/**
 * @brief 获取当前运行分区
 * @return 分区名称
 */
const char* ota_manager_get_running_partition(void);

/**
 * @brief 标记当前固件为有效
 * @return MIDI_OK 成功
 */
midi_error_t ota_manager_mark_valid(void);

/**
 * @brief 回滚到上一个版本
 * @return MIDI_OK 成功
 */
midi_error_t ota_manager_rollback(void);

/**
 * @brief 检查是否需要验证
 * @return true 需要验证
 */
bool ota_manager_needs_validation(void);

/**
 * @brief 获取OTA进度
 * @return 0-100
 */
int ota_manager_get_progress(void);

#ifdef __cplusplus
}
#endif

#endif // OTA_MANAGER_H