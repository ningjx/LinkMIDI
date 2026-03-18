/**
 * @file nm2_session.c
 * @brief MIDI 2.0 会话管理实现
 */

#include "nm2_session.h"
#include <string.h>
#include "esp_log.h"
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
    SemaphoreHandle_t mutex;
    nm2_session_event_cb event_callback;
    void* event_user_data;
};

/* ============================================================================
 * 公共 API
 * ============================================================================ */

nm2_session_t* nm2_session_create(uint8_t local_ssrc) {
    nm2_session_t* session = calloc(1, sizeof(nm2_session_t));
    if (!session) return NULL;
    
    session->mutex = xSemaphoreCreateMutex();
    if (!session->mutex) {
        free(session);
        return NULL;
    }
    
    session->info.local_ssrc = local_ssrc;
    session->info.state = NM2_SESSION_IDLE;
    
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
    
    // 创建 INV 包
    uint8_t packet[36];
    int offset = 0;
    
    packet[offset++] = 0x01;  // INV command
    packet[offset++] = 0x00;  // Status
    packet[offset++] = session->info.local_ssrc;
    packet[offset++] = 0x00;  // Placeholder
    
    // 发送
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip;
    addr.sin_port = htons(port);
    
    int sent = sendto(socket, packet, offset, 0, (struct sockaddr*)&addr, sizeof(addr));
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
    
    // 创建 OK 包
    uint8_t packet[36];
    int offset = 0;
    
    packet[offset++] = 0x02;  // OK command
    packet[offset++] = 0x00;  // Status
    packet[offset++] = session->info.local_ssrc;
    packet[offset++] = session->info.remote_ssrc;
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = session->info.ip_address;
    addr.sin_port = htons(session->info.port);
    
    int sent = sendto(socket, packet, offset, 0, (struct sockaddr*)&addr, sizeof(addr));
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send OK: %d", errno);
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
    
    // 创建 NO 包
    uint8_t packet[36];
    int offset = 0;
    
    packet[offset++] = 0x03;  // NO command
    packet[offset++] = 0x00;
    packet[offset++] = session->info.local_ssrc;
    packet[offset++] = session->info.remote_ssrc;
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = session->info.ip_address;
    addr.sin_port = htons(session->info.port);
    
    sendto(socket, packet, offset, 0, (struct sockaddr*)&addr, sizeof(addr));
    
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
    
    // 创建 BYE 包
    uint8_t packet[36];
    int offset = 0;
    
    packet[offset++] = 0x04;  // BYE command
    packet[offset++] = 0x00;
    packet[offset++] = session->info.local_ssrc;
    packet[offset++] = session->info.remote_ssrc;
    
    if (socket >= 0 && session->info.ip_address != 0) {
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = session->info.ip_address;
        addr.sin_port = htons(session->info.port);
        sendto(socket, packet, offset, 0, (struct sockaddr*)&addr, sizeof(addr));
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
    
    if (xSemaphoreTake(session->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return MIDI_ERR_TIMEOUT;
    }
    
    session->info.remote_ssrc = data[2];
    session->info.ip_address = remote_ip;
    session->info.port = remote_port;
    session->info.state = NM2_SESSION_INV_PENDING;
    
    xSemaphoreGive(session->mutex);
    
    ESP_LOGI(TAG, "Received INV from 0x%08X", (unsigned int)remote_ip);
    notify_event(session);
    return MIDI_OK;
}

midi_error_t nm2_session_handle_inv_response(nm2_session_t* session, const uint8_t* data, int length) {
    if (!session || !data || length < 4) return MIDI_ERR_INVALID_ARG;
    
    uint8_t cmd = data[0];
    
    if (xSemaphoreTake(session->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return MIDI_ERR_TIMEOUT;
    }
    
    if (cmd == 0x02) {  // OK
        session->info.remote_ssrc = data[3];
        session->info.state = NM2_SESSION_ACTIVE;
        session->info.sequence_number = 0;
        ESP_LOGI(TAG, "Session established");
    } else if (cmd == 0x03) {  // NO
        session->info.state = NM2_SESSION_IDLE;
        ESP_LOGW(TAG, "Session rejected");
    }
    
    xSemaphoreGive(session->mutex);
    
    notify_event(session);
    return MIDI_OK;
}

midi_error_t nm2_session_handle_termination(nm2_session_t* session, const uint8_t* data, int length) {
    if (!session) return MIDI_ERR_INVALID_ARG;
    
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