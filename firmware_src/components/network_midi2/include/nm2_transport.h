/**
 * @file nm2_transport.h
 * @brief MIDI 2.0 传输子模块
 * 
 * 负责 UDP 数据传输：发送/接收 UMP 数据包。
 */

#ifndef NM2_TRANSPORT_H
#define NM2_TRANSPORT_H

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
 * @brief 传输上下文 (不透明)
 */
typedef struct nm2_transport nm2_transport_t;

/**
 * @brief UMP 数据接收回调
 * @param data UMP 数据
 * @param length 数据长度
 * @param user_data 用户数据
 */
typedef void (*nm2_ump_rx_callback_t)(const uint8_t* data, uint16_t length, void* user_data);

/**
 * @brief MIDI 数据接收回调 (转换后的 MIDI 1.0)
 */
typedef void (*nm2_midi_rx_callback_t)(const uint8_t* data, uint16_t length, void* user_data);

/* ============================================================================
 * 传输 API
 * ============================================================================ */

/**
 * @brief 创建传输上下文
 * @param port 监听端口
 */
nm2_transport_t* nm2_transport_create(uint16_t port);

/**
 * @brief 销毁传输上下文
 */
void nm2_transport_destroy(nm2_transport_t* transport);

/**
 * @brief 启动传输服务
 */
midi_error_t nm2_transport_start(nm2_transport_t* transport);

/**
 * @brief 停止传输服务
 */
midi_error_t nm2_transport_stop(nm2_transport_t* transport);

/**
 * @brief 设置数据接收回调
 */
void nm2_transport_set_callbacks(nm2_transport_t* transport,
                                  nm2_ump_rx_callback_t ump_cb,
                                  nm2_midi_rx_callback_t midi_cb,
                                  void* user_data);

/**
 * @brief 获取数据 socket (用于会话管理)
 */
int nm2_transport_get_socket(const nm2_transport_t* transport);

/**
 * @brief 发送 UMP 数据包
 * @param transport 传输上下文
 * @param ip 目标 IP
 * @param port 目标端口
 * @param sequence 序列号
 * @param ump_data UMP 数据
 * @param length 数据长度
 */
midi_error_t nm2_transport_send_ump(nm2_transport_t* transport,
                                     uint32_t ip, uint16_t port,
                                     uint16_t sequence,
                                     const uint8_t* ump_data, uint16_t length);

/**
 * @brief 发送 MIDI 1.0 消息 (自动转换为 UMP)
 */
midi_error_t nm2_transport_send_midi(nm2_transport_t* transport,
                                      uint32_t ip, uint16_t port,
                                      uint16_t sequence,
                                      uint8_t status, uint8_t data1, uint8_t data2);

/**
 * @brief 发送 Ping 包
 */
midi_error_t nm2_transport_send_ping(nm2_transport_t* transport,
                                      uint32_t ip, uint16_t port);

/* ============================================================================
 * 协议转换工具函数
 * ============================================================================ */

/**
 * @brief MIDI 1.0 转 UMP
 * @param status MIDI 状态字节
 * @param data1 数据字节 1
 * @param data2 数据字节 2
 * @param ump_out 输出 UMP 数据 (至少 4 字节)
 * @param ump_len 输出 UMP 长度
 */
void nm2_midi_to_ump(uint8_t status, uint8_t data1, uint8_t data2,
                     uint8_t* ump_out, uint8_t* ump_len);

/**
 * @brief UMP 转 MIDI 1.0
 * @param ump_data UMP 数据
 * @param ump_len UMP 长度
 * @param midi_out 输出 MIDI 数据 (至少 3 字节)
 * @param midi_len 输出 MIDI 长度
 * @return true 转换成功
 */
bool nm2_ump_to_midi(const uint8_t* ump_data, uint8_t ump_len,
                     uint8_t* midi_out, uint8_t* midi_len);

#ifdef __cplusplus
}
#endif

#endif // NM2_TRANSPORT_H