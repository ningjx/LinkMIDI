/**
 * @file nm2_session.h
 * @brief MIDI 2.0 会话管理子模块
 * 
 * 负责会话生命周期管理：邀请、接受、终止等。
 */

#ifndef NM2_SESSION_H
#define NM2_SESSION_H

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
 * @brief 会话状态
 */
typedef enum {
    NM2_SESSION_IDLE,           ///< 无会话
    NM2_SESSION_INV_PENDING,    ///< 邀请等待中
    NM2_SESSION_ACTIVE,         ///< 会话激活
    NM2_SESSION_CLOSING         ///< 会话关闭中
} nm2_session_state_t;

/**
 * @brief 会话信息
 */
typedef struct {
    char device_name[64];       ///< 设备名
    char product_id[64];        ///< 产品 ID
    uint32_t ip_address;        ///< IP 地址 (网络字节序)
    uint16_t port;              ///< UDP 端口
    uint32_t local_ssrc;         ///< 本地 SSRC (32-bit)
    uint32_t remote_ssrc;        ///< 远端 SSRC (32-bit)
    uint16_t sequence_number;   ///< 序列号
    nm2_session_state_t state;  ///< 会话状态
} nm2_session_info_t;

/**
 * @brief 会话上下文 (不透明)
 */
typedef struct nm2_session nm2_session_t;

/**
 * @brief 会话事件回调
 */
typedef void (*nm2_session_event_cb)(nm2_session_state_t state, const nm2_session_info_t* info, void* user_data);

/* ============================================================================
 * 会话 API
 * ============================================================================ */

/**
 * @brief 创建会话上下文
 */
nm2_session_t* nm2_session_create(uint32_t local_ssrc);

/**
 * @brief 销毁会话上下文
 */
void nm2_session_destroy(nm2_session_t* session);

/**
 * @brief 设置事件回调
 */
void nm2_session_set_event_callback(nm2_session_t* session, nm2_session_event_cb callback, void* user_data);

/**
 * @brief 发起会话邀请 (客户端)
 */
midi_error_t nm2_session_initiate(nm2_session_t* session, int socket,
                                   uint32_t ip, uint16_t port, const char* device_name);

/**
 * @brief 接受会话邀请 (服务端)
 */
midi_error_t nm2_session_accept(nm2_session_t* session, int socket);

/**
 * @brief 拒绝会话邀请 (服务端)
 */
midi_error_t nm2_session_reject(nm2_session_t* session, int socket);

/**
 * @brief 终止会话
 */
midi_error_t nm2_session_terminate(nm2_session_t* session, int socket);

/**
 * @brief 获取会话状态
 */
nm2_session_state_t nm2_session_get_state(const nm2_session_t* session);

/**
 * @brief 检查会话是否激活
 */
bool nm2_session_is_active(const nm2_session_t* session);

/**
 * @brief 获取会话信息
 */
bool nm2_session_get_info(const nm2_session_t* session, nm2_session_info_t* info);

/**
 * @brief 处理收到的邀请包
 */
midi_error_t nm2_session_handle_invitation(nm2_session_t* session, const uint8_t* data, int length,
                                            uint32_t remote_ip, uint16_t remote_port);

/**
 * @brief 处理收到的邀请响应
 */
midi_error_t nm2_session_handle_inv_response(nm2_session_t* session, const uint8_t* data, int length);

/**
 * @brief 处理终止包
 */
midi_error_t nm2_session_handle_termination(nm2_session_t* session, const uint8_t* data, int length);

/**
 * @brief 处理会话重置请求
 */
midi_error_t nm2_session_handle_reset(nm2_session_t* session, int socket, const uint8_t* data, int length);

/**
 * @brief 重置会话状态 (不终止会话)
 */
midi_error_t nm2_session_reset(nm2_session_t* session);

/**
 * @brief 更新序列号
 */
uint16_t nm2_session_next_sequence(nm2_session_t* session);

/**
 * @brief 获取会话 ID
 */
uint32_t nm2_session_get_id(const nm2_session_t* session);

#ifdef __cplusplus
}
#endif

#endif // NM2_SESSION_H