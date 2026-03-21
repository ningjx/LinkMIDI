/**
 * @file nm2_transport.c
 * @brief MIDI 2.0 传输实现
 * 
 * 参考: Network MIDI 2.0 Implementation Guide - Using Sequence Numbers for Robustness and Recovery
 */

#include "nm2_transport.h"
#include "nm2_protocol.h"
#include "nm2_session.h"
#include "midi_converter.h"
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
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
    
    // 会话上下文 (用于重传支持)
    nm2_session_t* session;
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
        
        // 验证 UDP 签名
        if (!nm2_protocol_validate_signature(buffer, len)) {
            ESP_LOGW(TAG, "Invalid packet signature");
            continue;
        }
        
        // 解析命令包
        nm2_command_packet_t cmd;
        int parsed = nm2_protocol_parse_packet(buffer, len, &cmd, 1);
        
        if (parsed < 1) {
            ESP_LOGW(TAG, "Failed to parse packet");
            continue;
        }
        
        // 根据命令类型处理
        switch (cmd.command) {
            case NM2_CMD_UMP_DATA: {
                // UMP 数据包
                if (cmd.payload_len > 0) {
                    uint16_t seq = ((uint16_t)cmd.specific1 << 8) | cmd.specific2;
                    const uint8_t* ump = cmd.payload;
                    int ump_len = cmd.payload_len;
                    
                    ESP_LOGD(TAG, "RX UMP: seq=%d, len=%d", seq, ump_len);
                    
                    // 更新最后接收的序列号
                    if (transport->session) {
                        nm2_session_update_last_received_seq(transport->session, seq);
                    }
                    
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
                break;
            }
            
            case NM2_CMD_RETRANSMIT_REQUEST: {
                // 重传请求 (参考 MIDI.org 实现指南)
                ESP_LOGD(TAG, "Received RETRANSMIT_REQUEST");
                
                if (transport->session && nm2_session_supports_retransmit(transport->session)) {
                    nm2_retransmit_buffer_t* retransmit_buf = nm2_session_get_retransmit_buffer(transport->session);
                    
                    // 解析重传请求参数
                    uint16_t first_seq = ((uint16_t)cmd.specific1 << 8) | cmd.specific2;
                    uint16_t count = cmd.payload_len > 0 ? cmd.payload[0] : 1;
                    
                    ESP_LOGI(TAG, "Retransmit request: first_seq=%d, count=%d", first_seq, count);
                    
                    // 获取会话信息
                    nm2_session_info_t info;
                    if (!nm2_session_get_info(transport->session, &info)) {
                        ESP_LOGW(TAG, "Failed to get session info");
                        break;
                    }
                    
                    // 从重传缓冲区获取数据包
                    uint8_t retransmit_data[NM2_MAX_PACKET_SIZE];
                    for (int i = 0; i < count; i++) {
                        uint16_t seq = (first_seq + i) & 0xFFFF;
                        int data_len = nm2_retransmit_buffer_get(retransmit_buf, seq,
                                                                  retransmit_data, sizeof(retransmit_data));
                        
                        if (data_len > 0) {
                            // 重传数据包
                            midi_error_t err = nm2_transport_send_ump(transport, 
                                                                       info.ip_address, info.port,
                                                                       seq, retransmit_data, data_len);
                            if (err == MIDI_OK) {
                                ESP_LOGD(TAG, "Retransmitted seq=%d", seq);
                            } else {
                                ESP_LOGW(TAG, "Failed to retransmit seq=%d", seq);
                            }
                        } else {
                            ESP_LOGW(TAG, "Retransmit entry not found for seq=%d", seq);
                        }
                    }
                }
                break;
            }
            
            case NM2_CMD_SESSION_RESET: {
                // 会话重置请求
                ESP_LOGI(TAG, "Received SESSION_RESET");
                if (transport->session) {
                    nm2_session_handle_reset(transport->session, transport->socket, buffer, len);
                }
                break;
            }
            
            default:
                // 其他命令由上层处理
                ESP_LOGD(TAG, "Received command: 0x%02X", cmd.command);
                break;
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
    BaseType_t ret = xTaskCreate(receive_task, "nm2_rx", RECEIVE_TASK_STACK, transport, 
                RECEIVE_TASK_PRIO, &transport->rx_task);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create receive task");
        transport->running = false;
        close(transport->socket);
        transport->socket = -1;
        return MIDI_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Transport started on port %d", transport->port);
    return MIDI_OK;
}

midi_error_t nm2_transport_stop(nm2_transport_t* transport) {
    if (!transport) return MIDI_ERR_INVALID_ARG;
    if (!transport->running) return MIDI_OK;
    
    transport->running = false;
    
    // 等待任务结束
    if (transport->rx_task) {
        vTaskDelay(pdMS_TO_TICKS(200));
        // 强制删除任务（如果还在运行）
        TaskHandle_t task = transport->rx_task;
        transport->rx_task = NULL;
        if (task) {
            vTaskDelete(task);
        }
    }
    
    if (transport->socket >= 0) {
        close(transport->socket);
        transport->socket = -1;
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

void nm2_transport_set_session(nm2_transport_t* transport, nm2_session_t* session) {
    if (!transport) return;
    transport->session = session;
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
    
    // 使用新协议模块构建 UMP 数据包
    uint8_t packet[NM2_MAX_PACKET_SIZE];
    int packet_len = nm2_protocol_build_ump_data(packet, sizeof(packet),
                                                   sequence, ump_data, length);
    
    if (packet_len < 0) {
        ESP_LOGE(TAG, "Failed to build UMP packet");
        return MIDI_ERR_INVALID_ARG;
    }
    
    // 存入重传缓冲区 (如果支持重传)
    if (transport->session && nm2_session_supports_retransmit(transport->session)) {
        nm2_retransmit_buffer_t* retransmit_buf = nm2_session_get_retransmit_buffer(transport->session);
        if (retransmit_buf) {
            nm2_retransmit_buffer_add(retransmit_buf, sequence, ump_data, length);
        }
    }
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip;
    addr.sin_port = htons(port);
    
    int sent = sendto(transport->socket, packet, packet_len, 0,
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
    
    // 使用新协议模块构建 PING 包
    uint8_t packet[NM2_MAX_PACKET_SIZE];
    uint32_t ping_id = esp_random();  // 生成随机 ping ID
    int packet_len = nm2_protocol_build_ping(packet, sizeof(packet), ping_id);
    
    if (packet_len < 0) {
        ESP_LOGE(TAG, "Failed to build PING packet");
        return MIDI_ERR_INVALID_ARG;
    }
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip;
    addr.sin_port = htons(port);
    
    sendto(transport->socket, packet, packet_len, 0,
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