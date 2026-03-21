/**
 * @file nm2_session.h
 * @brief MIDI 2.0 会话管理子模块
 * 
 * 负责会话生命周期管理：邀请、接受、终止等。
 * 
 * 参考: Network MIDI 2.0 Implementation Guide - Using Sequence Numbers for Robustness and Recovery
 */

#ifndef NM2_SESSION_H
#define NM2_SESSION_H

#include <stdint.h>
#include <stdbool.h>
#include "midi_error.h"
#include "nm2_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 常量定义
 * ============================================================================ */

/** 保活间隔 (毫秒) - 规范建议 10 秒 */
#define NM2_KEEPALIVE_INTERVAL_MS   10000

/** 保活超时 (毫秒) - 规范建议 30 秒 */
#define NM2_KEEPALIVE_TIMEOUT_MS    30000

/** 保活最大重试次数 */
#define NM2_KEEPALIVE_MAX_RETRIES   3

/** 重传等待时间 (毫秒) - 规范建议 100ms */
#define NM2_RETRANSMIT_WAIT_MS      100

/** 重传缓冲区最大保留时间 (毫秒) - 建议数秒 */
#define NM2_RETRANSMIT_MAX_AGE_MS   5000

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
    uint16_t sequence_number;   ///< 发送序列号 (LastSentSeqNum)
    uint16_t last_received_seq; ///< 最后接收序列号 (LastReceivedSeqNum)
    nm2_session_state_t state;  ///< 会话状态
} nm2_session_info_t;

/**
 * @brief 会话配置
 */
typedef struct {
    bool enable_retransmit;     ///< 启用重传支持
    bool enable_fec;            ///< 启用前向纠错 (FEC)
    uint16_t keepalive_interval;///< 保活间隔 (毫秒)
} nm2_session_config_t;

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

/**
 * @brief 设置会话配置
 */
void nm2_session_set_config(nm2_session_t* session, const nm2_session_config_t* config);

/* ============================================================================
 * 序列号与重传支持 API
 * ============================================================================ */

/**
 * @brief 获取最后发送的序列号 (LastSentSeqNum)
 */
uint16_t nm2_session_get_last_sent_seq(const nm2_session_t* session);

/**
 * @brief 获取最后接收的序列号 (LastReceivedSeqNum)
 */
uint16_t nm2_session_get_last_received_seq(const nm2_session_t* session);

/**
 * @brief 更新最后接收的序列号
 * 
 * 当收到 UMP 数据包时调用，用于检测丢包。
 */
void nm2_session_update_last_received_seq(nm2_session_t* session, uint16_t seq);

/**
 * @brief 获取下一个发送序列号并递增
 * 
 * 用于发送 UMP 数据时获取序列号。
 */
uint16_t nm2_session_next_send_seq(nm2_session_t* session);

/**
 * @brief 获取重传缓冲区
 */
nm2_retransmit_buffer_t* nm2_session_get_retransmit_buffer(nm2_session_t* session);

/**
 * @brief 检查是否支持重传
 */
bool nm2_session_supports_retransmit(const nm2_session_t* session);

/**
 * @brief 设置远端是否支持重传
 */
void nm2_session_set_remote_retransmit_support(nm2_session_t* session, bool supported);

/* ============================================================================
 * 保活机制 API
 * ============================================================================ */

/**
 * @brief 获取最后活动时间 (毫秒)
 */
uint32_t nm2_session_get_last_activity_ms(const nm2_session_t* session);

/**
 * @brief 检查是否需要发送保活包
 */
bool nm2_session_need_keepalive(const nm2_session_t* session);

/**
 * @brief 发送保活包 (PING)
 */
midi_error_t nm2_session_send_keepalive(nm2_session_t* session, int socket);

/**
 * @brief 更新活动时间 (收到数据时调用)
 */
void nm2_session_update_activity(nm2_session_t* session);

#ifdef __cplusplus
}
#endif

#endif // NM2_SESSION_H