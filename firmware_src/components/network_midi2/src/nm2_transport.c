/**
 * @file nm2_transport.c
 * @brief MIDI 2.0 传输实现
 */

#include "nm2_transport.h"
#include "midi_converter.h"
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

static const char* TAG = "NM2_XPORT";

#define RECEIVE_TASK_STACK  4096
#define RECEIVE_TASK_PRIO   5
#define RX_BUFFER_SIZE      1500

/* ============================================================================
 * 内部结构
 * ============================================================================ */

struct nm2_transport {
    uint16_t port;
    int socket;
    bool running;
    
    TaskHandle_t rx_task;
    
    nm2_ump_rx_callback_t ump_callback;
    nm2_midi_rx_callback_t midi_callback;
    void* callback_user_data;
};

/* ============================================================================
 * 内部函数
 * ============================================================================ */

static void receive_task(void* arg) {
    nm2_transport_t* transport = (nm2_transport_t*)arg;
    uint8_t buffer[RX_BUFFER_SIZE];
    
    while (transport->running) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        
        int len = recvfrom(transport->socket, buffer, sizeof(buffer), 0,
                           (struct sockaddr*)&from_addr, &from_len);
        
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            ESP_LOGE(TAG, "Receive error: %d", errno);
            continue;
        }
        
        if (len < 4) continue;
        
        uint8_t cmd = buffer[0];
        
        // UMP 数据 (cmd = 0x10)
        if (cmd == 0x10 && len > 4) {
            uint16_t seq = ((uint16_t)buffer[1] << 8) | buffer[2];
            uint8_t* ump = &buffer[4];
            int ump_len = len - 4;
            
            ESP_LOGD(TAG, "RX UMP: seq=%d, len=%d", seq, ump_len);
            
            if (transport->ump_callback) {
                transport->ump_callback(ump, ump_len, transport->callback_user_data);
            }
            
            // 尝试转换为 MIDI 1.0
            if (transport->midi_callback) {
                uint8_t midi[3];
                uint8_t midi_len;
                if (nm2_ump_to_midi(ump, ump_len, midi, &midi_len)) {
                    transport->midi_callback(midi, midi_len, transport->callback_user_data);
                }
            }
        }
    }
    
    vTaskDelete(NULL);
}

/* ============================================================================
 * 公共 API
 * ============================================================================ */

nm2_transport_t* nm2_transport_create(uint16_t port) {
    nm2_transport_t* transport = calloc(1, sizeof(nm2_transport_t));
    if (!transport) return NULL;
    
    transport->port = port;
    transport->socket = -1;
    
    return transport;
}

void nm2_transport_destroy(nm2_transport_t* transport) {
    if (!transport) return;
    
    nm2_transport_stop(transport);
    free(transport);
}

midi_error_t nm2_transport_start(nm2_transport_t* transport) {
    if (!transport) return MIDI_ERR_INVALID_ARG;
    if (transport->running) return MIDI_ERR_ALREADY_INITIALIZED;
    
    // 创建 UDP socket
    transport->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (transport->socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %d", errno);
        return MIDI_ERR_NET_SOCKET_ERROR;
    }
    
    // 设置非阻塞
    int flags = fcntl(transport->socket, F_GETFL, 0);
    fcntl(transport->socket, F_SETFL, flags | O_NONBLOCK);
    
    // 绑定端口
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(transport->port);
    
    if (bind(transport->socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind port %d: %d", transport->port, errno);
        close(transport->socket);
        transport->socket = -1;
        return MIDI_ERR_NET_BIND_FAILED;
    }
    
    transport->running = true;
    
    // 创建接收任务
    xTaskCreate(receive_task, "nm2_rx", RECEIVE_TASK_STACK, transport, 
                RECEIVE_TASK_PRIO, &transport->rx_task);
    
    ESP_LOGI(TAG, "Transport started on port %d", transport->port);
    return MIDI_OK;
}

midi_error_t nm2_transport_stop(nm2_transport_t* transport) {
    if (!transport) return MIDI_ERR_INVALID_ARG;
    if (!transport->running) return MIDI_OK;
    
    transport->running = false;
    
    if (transport->socket >= 0) {
        close(transport->socket);
        transport->socket = -1;
    }
    
    if (transport->rx_task) {
        vTaskDelay(pdMS_TO_TICKS(100));
        transport->rx_task = NULL;
    }
    
    ESP_LOGI(TAG, "Transport stopped");
    return MIDI_OK;
}

void nm2_transport_set_callbacks(nm2_transport_t* transport,
                                  nm2_ump_rx_callback_t ump_cb,
                                  nm2_midi_rx_callback_t midi_cb,
                                  void* user_data) {
    if (!transport) return;
    transport->ump_callback = ump_cb;
    transport->midi_callback = midi_cb;
    transport->callback_user_data = user_data;
}

int nm2_transport_get_socket(const nm2_transport_t* transport) {
    return transport ? transport->socket : -1;
}

midi_error_t nm2_transport_send_ump(nm2_transport_t* transport,
                                     uint32_t ip, uint16_t port,
                                     uint16_t sequence,
                                     const uint8_t* ump_data, uint16_t length) {
    if (!transport || !ump_data) return MIDI_ERR_INVALID_ARG;
    if (transport->socket < 0) return MIDI_ERR_NET_NOT_CONNECTED;
    
    uint8_t packet[1500];
    int offset = 0;
    
    // Header
    packet[offset++] = 0x10;  // UMP data
    packet[offset++] = (sequence >> 8) & 0xFF;
    packet[offset++] = sequence & 0xFF;
    packet[offset++] = 0x00;  // Reserved
    
    // UMP data
    if (length > sizeof(packet) - 4) {
        length = sizeof(packet) - 4;
    }
    memcpy(&packet[offset], ump_data, length);
    offset += length;
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip;
    addr.sin_port = htons(port);
    
    int sent = sendto(transport->socket, packet, offset, 0,
                       (struct sockaddr*)&addr, sizeof(addr));
    if (sent < 0) {
        ESP_LOGE(TAG, "Send failed: %d", errno);
        return MIDI_ERR_NET_SEND_FAILED;
    }
    
    return MIDI_OK;
}

midi_error_t nm2_transport_send_midi(nm2_transport_t* transport,
                                      uint32_t ip, uint16_t port,
                                      uint16_t sequence,
                                      uint8_t status, uint8_t data1, uint8_t data2) {
    uint8_t ump[4];
    uint8_t ump_len;
    
    nm2_midi_to_ump(status, data1, data2, ump, &ump_len);
    
    return nm2_transport_send_ump(transport, ip, port, sequence, ump, ump_len);
}

midi_error_t nm2_transport_send_ping(nm2_transport_t* transport,
                                      uint32_t ip, uint16_t port) {
    if (!transport) return MIDI_ERR_INVALID_ARG;
    if (transport->socket < 0) return MIDI_ERR_NET_NOT_CONNECTED;
    
    uint8_t packet[4] = { 0x00, 0x00, 0x00, 0x00 };
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip;
    addr.sin_port = htons(port);
    
    sendto(transport->socket, packet, sizeof(packet), 0,
           (struct sockaddr*)&addr, sizeof(addr));
    
    return MIDI_OK;
}

/* ============================================================================
 * 协议转换 (使用 midi_converter 模块)
 * ============================================================================ */

void nm2_midi_to_ump(uint8_t status, uint8_t data1, uint8_t data2,
                     uint8_t* ump_out, uint8_t* ump_len) {
    if (!ump_out || !ump_len) return;
    
    // 使用标准 MIDI 1.0 → UMP 转换 (MT=0x2)
    midi_error_t err = midi1_to_ump_cv(status, data1, data2, 0, ump_out);
    if (err == MIDI_OK) {
        *ump_len = 4;
    } else {
        *ump_len = 0;
    }
}

bool nm2_ump_to_midi(const uint8_t* ump_data, uint8_t ump_len,
                     uint8_t* midi_out, uint8_t* midi_len) {
    if (!ump_data || ump_len < 4 || !midi_out || !midi_len) return false;
    
    midi1_message_t msg;
    midi_error_t err = ump_to_midi1(ump_data, ump_len, &msg);
    
    if (err == MIDI_OK && msg.length >= 2) {
        midi_out[0] = msg.status;
        midi_out[1] = msg.data1;
        midi_out[2] = msg.data2;
        *midi_len = msg.length;
        return true;
    }
    
    return false;
}