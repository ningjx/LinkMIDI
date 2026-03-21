/**
 * @file nm2_session.c
 * @brief MIDI 2.0 会话管理实现
 */

#include "nm2_session.h"
#include "nm2_protocol.h"
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
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
    uint32_t session_id;            ///< 会话 ID (随机生成)
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
    
    return session;
}

void nm2_session_destroy(nm2_session_t* session) {
    if (!session) return;
    
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

static void notify_event(nm2_session_t* session) {
    if (session->event_callback) {
        session->event_callback(session->info.state, &session->info, session->event_user_data);
    }
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