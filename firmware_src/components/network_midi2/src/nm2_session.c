/**
 * @file nm2_session.c
 * @brief MIDI 2.0 会话管理实现
 * 
 * 参考: Network MIDI 2.0 Implementation Guide - Using Sequence Numbers for Robustness and Recovery
 */

#include "nm2_session.h"
#include "nm2_protocol.h"
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

static const char* TAG = "NM2_SESS";

/* ============================================================================
 * 内部结构
 * ============================================================================ */

struct nm2_session {
    nm2_session_info_t info;
    nm2_session_config_t config;
    
    uint32_t session_id;            ///< 会话 ID (随机生成)
    
    // 序列号管理 (参考 MIDI.org 实现指南)
    uint16_t last_sent_seq;         ///< LastSentSeqNum - 最后发送的序列号
    uint16_t last_received_seq;     ///< LastReceivedSeqNum - 最后接收的序列号
    
    // 重传支持
    nm2_retransmit_buffer_t retransmit_buf;  ///< 重传缓冲区
    bool remote_supports_retransmit;         ///< 远端是否支持重传
    
    // 保活机制
    uint32_t last_activity_ms;      ///< 最后活动时间
    uint8_t keepalive_retries;      ///< 保活重试次数
    
    // 同步
    SemaphoreHandle_t mutex;
    nm2_session_event_cb event_callback;
    void* event_user_data;
};

/* ============================================================================
 * 公共 API
 * ============================================================================ */

nm2_session_t* nm2_session_create(uint32_t local_ssrc) {
    nm2_session_t* session = calloc(1, sizeof(nm2_session_t));
    if (!session) return NULL;
    
    session->mutex = xSemaphoreCreateMutex();
    if (!session->mutex) {
        free(session);
        return NULL;
    }
    
    session->info.local_ssrc = local_ssrc;
    session->info.state = NM2_SESSION_IDLE;
    session->session_id = nm2_protocol_generate_session_id();
    
    // 默认配置
    session->config.enable_retransmit = true;
    session->config.enable_fec = true;
    session->config.keepalive_interval = NM2_KEEPALIVE_INTERVAL_MS;
    
    // 初始化重传缓冲区
    nm2_retransmit_buffer_init(&session->retransmit_buf);
    
    // 序列号从 1 开始 (规范要求)
    session->last_sent_seq = 0;
    session->last_received_seq = 0;
    
    return session;
}

void nm2_session_destroy(nm2_session_t* session) {
    if (!session) return;
    
    // 清理重传缓冲区
    nm2_retransmit_buffer_clear(&session->retransmit_buf);
    
    if (session->mutex) {
        vSemaphoreDelete(session->mutex);
    }
    free(session);
}

void nm2_session_set_event_callback(nm2_session_t* session, nm2_session_event_cb callback, void* user_data) {
    if (!session) return;
    
    if (xSemaphoreTake(session->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        session->event_callback = callback;
        session->event_user_data = user_data;
        xSemaphoreGive(session->mutex);
    }
}

void nm2_session_set_config(nm2_session_t* session, const nm2_session_config_t* config) {
    if (!session || !config) return;
    
    if (xSemaphoreTake(session->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        session->config = *config;
        xSemaphoreGive(session->mutex);
    }
}

static void notify_event(nm2_session_t* session) {
    if (session->event_callback) {
        session->event_callback(session->info.state, &session->info, session->event_user_data);
    }
}

/**
 * @brief 更新最后活动时间
 */
static void update_activity(nm2_session_t* session) {
    session->last_activity_ms = (uint32_t)(esp_timer_get_time() / 1000);
    session->keepalive_retries = 0;
}

midi_error_t nm2_session_initiate(nm2_session_t* session, int socket,
                                   uint32_t ip, uint16_t port, const char* device_name) {
    if (!session || socket < 0) return MIDI_ERR_INVALID_ARG;
    
    if (xSemaphoreTake(session->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return MIDI_ERR_TIMEOUT;
    }
    
    if (session->info.state != NM2_SESSION_IDLE) {
        xSemaphoreGive(session->mutex);
        return MIDI_ERR_SESSION_ALREADY_ACTIVE;
    }
    
    // 使用新协议模块构建 INV 包
    uint8_t packet[NM2_MAX_PACKET_SIZE];
    int packet_len = nm2_protocol_build_inv(packet, sizeof(packet),
                                             device_name ? device_name : "ESP32-NM2",
                                             NULL, NM2_CAP_NONE);
    
    if (packet_len < 0) {
        ESP_LOGE(TAG, "Failed to build INV packet");
        xSemaphoreGive(session->mutex);
        return MIDI_ERR_INVALID_ARG;
    }
    
    // 发送
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip;
    addr.sin_port = htons(port);
    
    int sent = sendto(socket, packet, packet_len, 0, (struct sockaddr*)&addr, sizeof(addr));
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send INV: %d", errno);
        xSemaphoreGive(session->mutex);
        return MIDI_ERR_NET_SEND_FAILED;
    }
    
    session->info.ip_address = ip;
    session->info.port = port;
    session->info.state = NM2_SESSION_INV_PENDING;
    if (device_name) {
        strncpy(session->info.device_name, device_name, sizeof(session->info.device_name) - 1);
    }
    
    xSemaphoreGive(session->mutex);
    
    notify_event(session);
    return MIDI_OK;
}

midi_error_t nm2_session_accept(nm2_session_t* session, int socket) {
    if (!session || socket < 0) return MIDI_ERR_INVALID_ARG;
    
    if (xSemaphoreTake(session->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return MIDI_ERR_TIMEOUT;
    }
    
    if (session->info.state != NM2_SESSION_INV_PENDING) {
        xSemaphoreGive(session->mutex);
        return MIDI_ERR_SESSION_NOT_ACTIVE;
    }
    
    // 使用新协议模块构建 INV_ACCEPTED 包
    uint8_t packet[NM2_MAX_PACKET_SIZE];
    int packet_len = nm2_protocol_build_inv_accepted(packet, sizeof(packet),
                                                      session->info.device_name,
                                                      NULL);
    
    if (packet_len < 0) {
        ESP_LOGE(TAG, "Failed to build INV_ACCEPTED packet");
        xSemaphoreGive(session->mutex);
        return MIDI_ERR_INVALID_ARG;
    }
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = session->info.ip_address;
    addr.sin_port = htons(session->info.port);
    
    int sent = sendto(socket, packet, packet_len, 0, (struct sockaddr*)&addr, sizeof(addr));
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send INV_ACCEPTED: %d", errno);
        xSemaphoreGive(session->mutex);
        return MIDI_ERR_NET_SEND_FAILED;
    }
    
    session->info.state = NM2_SESSION_ACTIVE;
    session->info.sequence_number = 0;
    
    xSemaphoreGive(session->mutex);
    
    ESP_LOGI(TAG, "Session accepted");
    notify_event(session);
    return MIDI_OK;
}

midi_error_t nm2_session_reject(nm2_session_t* session, int socket) {
    if (!session || socket < 0) return MIDI_ERR_INVALID_ARG;
    
    if (xSemaphoreTake(session->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return MIDI_ERR_TIMEOUT;
    }
    
    // 使用 BYE 命令拒绝邀请 (根据规范 Section 3.2.3)
    uint8_t packet[NM2_MAX_PACKET_SIZE];
    int packet_len = nm2_protocol_build_bye(packet, sizeof(packet),
                                             NM2_BYE_INV_REJECTED_BY_USER,
                                             "Session rejected");
    
    if (packet_len > 0) {
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = session->info.ip_address;
        addr.sin_port = htons(session->info.port);
        sendto(socket, packet, packet_len, 0, (struct sockaddr*)&addr, sizeof(addr));
    }
    
    session->info.state = NM2_SESSION_IDLE;
    
    xSemaphoreGive(session->mutex);
    return MIDI_OK;
}

midi_error_t nm2_session_terminate(nm2_session_t* session, int socket) {
    if (!session) return MIDI_ERR_INVALID_ARG;
    
    if (xSemaphoreTake(session->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return MIDI_ERR_TIMEOUT;
    }
    
    if (session->info.state == NM2_SESSION_IDLE) {
        xSemaphoreGive(session->mutex);
        return MIDI_OK;
    }
    
    // 使用新协议模块构建 BYE 包
    uint8_t packet[NM2_MAX_PACKET_SIZE];
    int packet_len = nm2_protocol_build_bye(packet, sizeof(packet),
                                             NM2_BYE_USER_TERMINATED,
                                             "Session terminated");
    
    if (packet_len > 0 && socket >= 0 && session->info.ip_address != 0) {
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = session->info.ip_address;
        addr.sin_port = htons(session->info.port);
        sendto(socket, packet, packet_len, 0, (struct sockaddr*)&addr, sizeof(addr));
    }
    
    session->info.state = NM2_SESSION_IDLE;
    
    xSemaphoreGive(session->mutex);
    
    ESP_LOGI(TAG, "Session terminated");
    notify_event(session);
    return MIDI_OK;
}

nm2_session_state_t nm2_session_get_state(const nm2_session_t* session) {
    if (!session) return NM2_SESSION_IDLE;
    return session->info.state;
}

bool nm2_session_is_active(const nm2_session_t* session) {
    return session && session->info.state == NM2_SESSION_ACTIVE;
}

bool nm2_session_get_info(const nm2_session_t* session, nm2_session_info_t* info) {
    if (!session || !info) return false;
    *info = session->info;
    return true;
}

midi_error_t nm2_session_handle_invitation(nm2_session_t* session, const uint8_t* data, int length,
                                            uint32_t remote_ip, uint16_t remote_port) {
    if (!session || !data || length < 4) return MIDI_ERR_INVALID_ARG;
    
    // 使用新协议模块解析 INV 包
    nm2_command_packet_t cmd;
    int parsed = nm2_protocol_parse_packet(data, length, &cmd, 1);
    
    if (parsed < 1 || cmd.command != NM2_CMD_INV) {
        ESP_LOGW(TAG, "Invalid INV packet");
        return MIDI_ERR_INVALID_ARG;
    }
    
    // 解析邀请数据
    nm2_invitation_t inv;
    if (!nm2_protocol_parse_inv(&cmd, &inv)) {
        ESP_LOGW(TAG, "Failed to parse INV data");
        return MIDI_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(session->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return MIDI_ERR_TIMEOUT;
    }
    
    // SSRC 在会话层面管理，这里使用随机生成的值
    session->info.remote_ssrc = esp_random();
    session->info.ip_address = remote_ip;
    session->info.port = remote_port;
    session->info.state = NM2_SESSION_INV_PENDING;
    
    // 保存设备名称
    if (inv.ump_endpoint_name) {
        strncpy(session->info.device_name, inv.ump_endpoint_name, sizeof(session->info.device_name) - 1);
    }
    
    xSemaphoreGive(session->mutex);
    
    ESP_LOGI(TAG, "Received INV from 0x%08X", (unsigned int)remote_ip);
    notify_event(session);
    return MIDI_OK;
}

midi_error_t nm2_session_handle_inv_response(nm2_session_t* session, const uint8_t* data, int length) {
    if (!session || !data || length < 4) return MIDI_ERR_INVALID_ARG;
    
    // 使用新协议模块解析响应包
    nm2_command_packet_t cmd;
    int parsed = nm2_protocol_parse_packet(data, length, &cmd, 1);
    
    if (parsed < 1) {
        ESP_LOGW(TAG, "Invalid response packet");
        return MIDI_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(session->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return MIDI_ERR_TIMEOUT;
    }
    
    if (cmd.command == NM2_CMD_INV_ACCEPTED) {  // INV_ACCEPTED
        // 解析邀请回复数据
        nm2_invitation_reply_t reply;
        if (nm2_protocol_parse_inv_reply(&cmd, &reply)) {
            if (reply.ump_endpoint_name) {
                strncpy(session->info.device_name, reply.ump_endpoint_name, sizeof(session->info.device_name) - 1);
            }
        }
        session->info.state = NM2_SESSION_ACTIVE;
        session->info.sequence_number = 0;
        ESP_LOGI(TAG, "Session established");
    } else if (cmd.command == NM2_CMD_BYE) {  // BYE (拒绝)
        session->info.state = NM2_SESSION_IDLE;
        ESP_LOGW(TAG, "Session rejected");
    }
    
    xSemaphoreGive(session->mutex);
    
    notify_event(session);
    return MIDI_OK;
}

midi_error_t nm2_session_handle_termination(nm2_session_t* session, const uint8_t* data, int length) {
    if (!session) return MIDI_ERR_INVALID_ARG;
    
    // 使用新协议模块解析 BYE 包
    nm2_command_packet_t cmd;
    int parsed = nm2_protocol_parse_packet(data, length, &cmd, 1);
    
    if (parsed < 1 || cmd.command != NM2_CMD_BYE) {
        ESP_LOGW(TAG, "Invalid BYE packet");
        return MIDI_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(session->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return MIDI_ERR_TIMEOUT;
    }
    
    session->info.state = NM2_SESSION_IDLE;
    
    xSemaphoreGive(session->mutex);
    
    ESP_LOGI(TAG, "Session terminated by remote");
    notify_event(session);
    return MIDI_OK;
}

uint16_t nm2_session_next_sequence(nm2_session_t* session) {
    if (!session) return 0;
    
    uint16_t seq;
    if (xSemaphoreTake(session->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        seq = session->info.sequence_number++;
        xSemaphoreGive(session->mutex);
    } else {
        seq = 0;
    }
    return seq;
}

midi_error_t nm2_session_handle_reset(nm2_session_t* session, int socket, const uint8_t* data, int length) {
    if (!session) return MIDI_ERR_INVALID_ARG;
    
    // 解析 SESSION_RESET 包
    nm2_command_packet_t cmd;
    int parsed = nm2_protocol_parse_packet(data, length, &cmd, 1);
    
    if (parsed < 1 || cmd.command != NM2_CMD_SESSION_RESET) {
        ESP_LOGW(TAG, "Invalid SESSION_RESET packet");
        return MIDI_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(session->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return MIDI_ERR_TIMEOUT;
    }
    
    // 重置序列号
    session->info.sequence_number = 1;
    
    xSemaphoreGive(session->mutex);
    
    // 发送 SESSION_RESET_REPLY
    uint8_t reply[NM2_MAX_PACKET_SIZE];
    int reply_len = nm2_protocol_build_session_reset_reply(reply, sizeof(reply));
    
    if (reply_len > 0 && socket >= 0 && session->info.ip_address != 0) {
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = session->info.ip_address;
        addr.sin_port = htons(session->info.port);
        sendto(socket, reply, reply_len, 0, (struct sockaddr*)&addr, sizeof(addr));
    }
    
    ESP_LOGI(TAG, "Session reset completed");
    
    // 通知上层应用
    notify_event(session);
    
    return MIDI_OK;
}

midi_error_t nm2_session_reset(nm2_session_t* session) {
    if (!session) return MIDI_ERR_INVALID_ARG;
    
    if (xSemaphoreTake(session->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return MIDI_ERR_TIMEOUT;
    }
    
    // 重置序列号
    session->info.sequence_number = 1;
    
    // 生成新的会话 ID
    session->session_id = nm2_protocol_generate_session_id();
    
    xSemaphoreGive(session->mutex);
    
    ESP_LOGI(TAG, "Session reset (new ID: 0x%08X)", (unsigned int)session->session_id);
    
    return MIDI_OK;
}

uint32_t nm2_session_get_id(const nm2_session_t* session) {
    if (!session) return 0;
    return session->session_id;
}

/* ============================================================================
 * 序列号与重传支持 (参考 MIDI.org 实现指南)
 * ============================================================================ */

uint16_t nm2_session_get_last_sent_seq(const nm2_session_t* session) {
    if (!session) return 0;
    return session->last_sent_seq;
}

uint16_t nm2_session_get_last_received_seq(const nm2_session_t* session) {
    if (!session) return 0;
    return session->last_received_seq;
}

void nm2_session_update_last_received_seq(nm2_session_t* session, uint16_t seq) {
    if (!session) return;
    
    if (xSemaphoreTake(session->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        session->last_received_seq = seq;
        update_activity(session);
        xSemaphoreGive(session->mutex);
    }
}

uint16_t nm2_session_next_send_seq(nm2_session_t* session) {
    if (!session) return 0;
    
    uint16_t seq = 0;
    if (xSemaphoreTake(session->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // 序列号递增 (0 跳过，根据规范)
        session->last_sent_seq++;
        if (session->last_sent_seq == 0) {
            session->last_sent_seq = 1;
        }
        seq = session->last_sent_seq;
        xSemaphoreGive(session->mutex);
    }
    return seq;
}

nm2_retransmit_buffer_t* nm2_session_get_retransmit_buffer(nm2_session_t* session) {
    if (!session) return NULL;
    return &session->retransmit_buf;
}

bool nm2_session_supports_retransmit(const nm2_session_t* session) {
    if (!session) return false;
    return session->remote_supports_retransmit && session->config.enable_retransmit;
}

void nm2_session_set_remote_retransmit_support(nm2_session_t* session, bool supported) {
    if (!session) return;
    
    if (xSemaphoreTake(session->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        session->remote_supports_retransmit = supported;
        xSemaphoreGive(session->mutex);
    }
}

/* ============================================================================
 * 保活机制
 * ============================================================================ */

uint32_t nm2_session_get_last_activity_ms(const nm2_session_t* session) {
    if (!session) return 0;
    return session->last_activity_ms;
}

bool nm2_session_need_keepalive(const nm2_session_t* session) {
    if (!session || session->info.state != NM2_SESSION_ACTIVE) return false;
    
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t elapsed = now_ms - session->last_activity_ms;
    
    return elapsed >= session->config.keepalive_interval;
}

midi_error_t nm2_session_send_keepalive(nm2_session_t* session, int socket) {
    if (!session || socket < 0) return MIDI_ERR_INVALID_ARG;
    if (session->info.state != NM2_SESSION_ACTIVE) return MIDI_ERR_SESSION_NOT_ACTIVE;
    
    // 使用新协议模块构建 PING 包
    uint8_t packet[NM2_MAX_PACKET_SIZE];
    int packet_len = nm2_protocol_build_ping(packet, sizeof(packet));
    
    if (packet_len < 0) {
        ESP_LOGE(TAG, "Failed to build PING packet");
        return MIDI_ERR_INVALID_ARG;
    }
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = session->info.ip_address;
    addr.sin_port = htons(session->info.port);
    
    int sent = sendto(socket, packet, packet_len, 0, (struct sockaddr*)&addr, sizeof(addr));
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send PING: %d", errno);
        return MIDI_ERR_NET_SEND_FAILED;
    }
    
    session->keepalive_retries++;
    ESP_LOGD(TAG, "Sent PING (retry %d)", session->keepalive_retries);
    
    return MIDI_OK;
}

void nm2_session_update_activity(nm2_session_t* session) {
    if (!session) return;
    
    if (xSemaphoreTake(session->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        update_activity(session);
        xSemaphoreGive(session->mutex);
    }
}