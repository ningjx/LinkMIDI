/**
 * @file nm2_discovery.h
 * @brief MIDI 2.0 设备发现子模块
 * 
 * 负责 mDNS/DNS-SD 设备发现，使用 ESP-IDF 内置 mDNS 组件。
 */

#ifndef NM2_DISCOVERY_H
#define NM2_DISCOVERY_H

#include <stdint.h>
#include <stdbool.h>
#include "midi_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 类型定义
 * ============================================================================ */

/**
 * @brief 发现设备信息
 */
typedef struct {
    char device_name[64];       ///< 设备名
    char product_id[64];        ///< 产品 ID
    uint32_t ip_address;        ///< IP 地址
    uint16_t port;              ///< 端口
} nm2_discovered_device_t;

/**
 * @brief 发现上下文 (不透明)
 */
typedef struct nm2_discovery nm2_discovery_t;

/**
 * @brief 设备发现回调
 */
typedef void (*nm2_device_found_callback_t)(const nm2_discovered_device_t* device, void* user_data);

/* ============================================================================
 * 发现 API
 * ============================================================================ */

/**
 * @brief 创建发现上下文
 * @param device_name 本设备名
 * @param product_id 产品 ID
 * @param port 服务端口
 */
nm2_discovery_t* nm2_discovery_create(const char* device_name,
                                       const char* product_id,
                                       uint16_t port);

/**
 * @brief 销毁发现上下文
 */
void nm2_discovery_destroy(nm2_discovery_t* discovery);

/**
 * @brief 启动发现服务
 * 
 * 启动 mDNS 广播和监听。
 */
midi_error_t nm2_discovery_start(nm2_discovery_t* discovery);

/**
 * @brief 停止发现服务
 */
midi_error_t nm2_discovery_stop(nm2_discovery_t* discovery);

/**
 * @brief 设置设备发现回调
 */
void nm2_discovery_set_callback(nm2_discovery_t* discovery,
                                 nm2_device_found_callback_t callback,
                                 void* user_data);

/**
 * @brief 发送发现查询
 */
midi_error_t nm2_discovery_send_query(nm2_discovery_t* discovery);

/**
 * @brief 获取发现的设备数量
 */
int nm2_discovery_get_device_count(const nm2_discovery_t* discovery);

/**
 * @brief 获取发现的设备信息
 */
bool nm2_discovery_get_device(const nm2_discovery_t* discovery, int index,
                               nm2_discovered_device_t* device);

/**
 * @brief 清空发现的设备列表
 */
void nm2_discovery_clear_devices(nm2_discovery_t* discovery);

#ifdef __cplusplus
}
#endif

#endif // NM2_DISCOVERY_H