#include "network_midi2.h"
#include "nm2_protocol.h"
#include "esp_log.h"
#include "esp_random.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>

static const char* TAG = "NM2";

#define NETWORK_MIDI2_VERSION "1.0.0"
#define MDNS_MULTICAST_ADDR "224.0.0.251"
#define MDNS_MULTICAST_PORT 5353
#define MAX_DISCOVERED_DEVICES 16
#define RECEIVE_TASK_PRIORITY 5
#define RECEIVE_TASK_STACK_SIZE 4096
#define DISCOVERY_TASK_STACK_SIZE 4096

/* ============================================================================
 * Device Structure
 * ============================================================================ */

typedef struct {
    uint32_t ip_address;
    uint16_t port;
    char device_name[64];
    char product_id[64];
    uint32_t ssrc;              ///< SSRC (32-bit)
} discovered_device_t;

typedef struct network_midi2_context {
    // Configuration
    char device_name[64];
    char product_id[64];
    uint16_t listen_port;
    network_midi2_device_mode_t mode;
    bool enable_discovery;
    bool is_running;
    
    // Sockets
    int data_socket;
    int discovery_socket;
    
    // Session state
    network_midi2_session_t current_session;
    network_midi2_session_state_t session_state;
    uint16_t send_sequence;
    
    // Local SSRC (32-bit)
    uint32_t local_ssrc;
    
    // Discovered devices
    discovered_device_t discovered_devices[MAX_DISCOVERED_DEVICES];
    int discovered_count;
    
    // Callbacks
    network_midi2_log_callback_t log_callback;
    network_midi2_midi_rx_callback_t midi_rx_callback;
    network_midi2_ump_rx_callback_t ump_rx_callback;
    
    // Task handles
    TaskHandle_t receive_task_handle;
    TaskHandle_t discovery_task_handle;
    
    // Synchronization
    SemaphoreHandle_t session_mutex;
} network_midi2_context_t;

/* ============================================================================
 * Logging Utilities
 * ============================================================================ */

static void network_midi2_log(network_midi2_context_t* ctx, const char* message) {
    if (ctx && ctx->log_callback) {
        ctx->log_callback(message);
    }
    ESP_LOGI(TAG, "%s", message);
}

static void network_midi2_logf(network_midi2_context_t* ctx, const char* fmt, ...) {
    static char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    network_midi2_log(ctx, buffer);
}

/* ============================================================================
 * Initialization Functions
 * ============================================================================ */

network_midi2_context_t* network_midi2_init(
    const char* device_name,
    const char* product_id,
    uint16_t port)
{
    network_midi2_config_t config = {0};
    config.device_name = device_name;
    config.product_id = product_id;
    config.listen_port = port;
    config.mode = MODE_PEER;
    config.enable_discovery = true;
    
    return network_midi2_init_with_config(&config);
}

network_midi2_context_t* network_midi2_init_with_config(
    const network_midi2_config_t* config)
{
    if (!config || !config->device_name || !config->product_id) {
        ESP_LOGE(TAG, "Invalid configuration");
        return NULL;
    }
    
    network_midi2_context_t* ctx = calloc(1, sizeof(network_midi2_context_t));
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to allocate context");
        return NULL;
    }
    
    // Copy configuration
    strncpy(ctx->device_name, config->device_name, sizeof(ctx->device_name) - 1);
    strncpy(ctx->product_id, config->product_id, sizeof(ctx->product_id) - 1);
    ctx->listen_port = config->listen_port ?: 5507;
    ctx->mode = config->mode;
    ctx->enable_discovery = config->enable_discovery;
    
    // Copy callbacks
    ctx->log_callback = config->log_callback;
    ctx->midi_rx_callback = config->midi_rx_callback;
    ctx->ump_rx_callback = config->ump_rx_callback;
    
    // Initialize state
    ctx->data_socket = -1;
    ctx->discovery_socket = -1;
    ctx->is_running = false;
    ctx->session_state = SESSION_STATE_IDLE;
    ctx->send_sequence = 0;
    ctx->discovered_count = 0;
    
    // Generate random 32-bit SSRC (MIDI 2.0 spec)
    ctx->local_ssrc = ((uint32_t)rand() << 16) | ((uint32_t)rand() & 0xFFFF);
    if (ctx->local_ssrc == 0) {
        ctx->local_ssrc = 1;  // SSRC must not be zero
    }
    
    // Create session mutex
    ctx->session_mutex = xSemaphoreCreateMutex();
    if (!ctx->session_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(ctx);
        return NULL;
    }
    
    network_midi2_logf(NULL, "[Init] Device: %s, Port: %d, SSRC: 0x%08lX",
                       config->device_name, ctx->listen_port, (unsigned long)ctx->local_ssrc);
    
    return ctx;
}

void network_midi2_deinit(network_midi2_context_t* ctx) {
    if (!ctx) return;
    
    if (ctx->is_running) {
        network_midi2_stop(ctx);
    }
    
    if (ctx->session_mutex) {
        vSemaphoreDelete(ctx->session_mutex);
    }
    
    free(ctx);
}

/* ============================================================================
 * Socket Functions
 * ============================================================================ */

static bool network_midi2_create_data_socket(network_midi2_context_t* ctx) {
    if (ctx->data_socket >= 0) {
        return true;
    }
    
    ctx->data_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ctx->data_socket < 0) {
        network_midi2_logf(ctx, "[Error] Failed to create data socket: %d", errno);
        return false;
    }
    
    // Set socket options
    int reuse = 1;
    if (setsockopt(ctx->data_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        network_midi2_logf(ctx, "[Error] Failed to set SO_REUSEADDR: %d", errno);
        close(ctx->data_socket);
        ctx->data_socket = -1;
        return false;
    }
    
    // Bind to listen port
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(ctx->listen_port);
    
    if (bind(ctx->data_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        network_midi2_logf(ctx, "[Error] Failed to bind to port %d: %d",
                          ctx->listen_port, errno);
        close(ctx->data_socket);
        ctx->data_socket = -1;
        return false;
    }
    
    network_midi2_logf(ctx, "[Socket] Data socket created on port %d", ctx->listen_port);
    return true;
}

static bool network_midi2_create_discovery_socket(network_midi2_context_t* ctx) {
    if (ctx->discovery_socket >= 0) {
        return true;
    }
    
    ctx->discovery_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ctx->discovery_socket < 0) {
        network_midi2_logf(ctx, "[Error] Failed to create discovery socket: %d", errno);
        return false;
    }
    
    // Set socket options
    int reuse = 1;
    if (setsockopt(ctx->discovery_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        network_midi2_logf(ctx, "[Error] Failed to set SO_REUSEADDR on discovery socket: %d", errno);
        close(ctx->discovery_socket);
        ctx->discovery_socket = -1;
        return false;
    }
    
    // Bind to mDNS port (for both client and server)
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(MDNS_MULTICAST_PORT);
    
    if (bind(ctx->discovery_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        network_midi2_logf(ctx, "[Error] Failed to bind discovery socket: %d", errno);
        close(ctx->discovery_socket);
        ctx->discovery_socket = -1;
        return false;
    }
    
    // Join multicast group
    struct ip_mreq mreq = {0};
    if (inet_aton(MDNS_MULTICAST_ADDR, &mreq.imr_multiaddr) == 0) {
        network_midi2_logf(ctx, "[Error] Invalid multicast address");
        close(ctx->discovery_socket);
        ctx->discovery_socket = -1;
        return false;
    }
    mreq.imr_interface.s_addr = INADDR_ANY;
    
    if (setsockopt(ctx->discovery_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        network_midi2_logf(ctx, "[Error] Failed to join multicast group: %d", errno);
        close(ctx->discovery_socket);
        ctx->discovery_socket = -1;
        return false;
    }
    
    network_midi2_logf(ctx, "[Socket] Discovery socket created");
    return true;
}

/* ============================================================================
 * mDNS/Discovery Functions
 * ============================================================================ */

static void network_midi2_mdns_encode_name(const char* name, uint8_t* buffer, int* offset) {
    const char* ptr = name;
    while (*ptr) {
        const char* dot = strchr(ptr, '.');
        int len = dot ? (dot - ptr) : strlen(ptr);
        
        buffer[(*offset)++] = (uint8_t)len;
        memcpy(&buffer[*offset], ptr, len);
        (*offset) += len;
        
        ptr += len;
        if (*ptr == '.') ptr++;
    }
    buffer[(*offset)++] = 0;
}

static uint8_t* network_midi2_create_mdns_query(int* out_len) {
    uint8_t* packet = malloc(512);
    if (!packet) {
        ESP_LOGE(TAG, "Failed to allocate mDNS query packet");
        return NULL;
    }
    
    int offset = 0;
    
    // DNS Header
    packet[offset++] = 0x00;  // ID high
    packet[offset++] = 0x00;  // ID low
    packet[offset++] = 0x00;  // Flags: query
    packet[offset++] = 0x00;  // Flags
    packet[offset++] = 0x00;  // Questions: high
    packet[offset++] = 0x01;  // Questions: low (1 question)
    packet[offset++] = 0x00;  // Answer RRs
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;  // Authority RRs
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;  // Additional RRs
    packet[offset++] = 0x00;
    
    // Question section: _midi2._udp.local
    network_midi2_mdns_encode_name("_midi2._udp.local", packet, &offset);
    packet[offset++] = 0x00;  // Type: PT (33)
    packet[offset++] = 0x21;
    packet[offset++] = 0x00;  // Class: IN
    packet[offset++] = 0x01;
    
    *out_len = offset;
    return packet;
}

static uint8_t* network_midi2_create_mdns_announcement(network_midi2_context_t* ctx, int* out_len) {
    uint8_t* packet = malloc(512);
    if (!packet) {
        ESP_LOGE(TAG, "Failed to allocate mDNS announcement packet");
        return NULL;
    }
    
    int offset = 0;
    
    // DNS Header
    packet[offset++] = 0x00;  // ID
    packet[offset++] = 0x00;
    packet[offset++] = 0x84;  // Flags: response, authoritative
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;  // Questions
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;  // Answer RRs
    packet[offset++] = 0x01;  // 1 answer
    packet[offset++] = 0x00;  // Authority RRs
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;  // Additional RRs
    packet[offset++] = 0x00;
    
    // Answer section: SRV record for _midi2._udp.local
    char service_name[128];
    snprintf(service_name, sizeof(service_name), "%s._midi2._udp.local", ctx->device_name);
    network_midi2_mdns_encode_name(service_name, packet, &offset);
    
    packet[offset++] = 0x00;  // Type: SRV (33)
    packet[offset++] = 0x21;
    packet[offset++] = 0x00;  // Class: IN (cache flush)
    packet[offset++] = 0x01;
    
    // TTL (4500 seconds)
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;
    packet[offset++] = 0x11;
    packet[offset++] = 0x94;
    
    // Data length: 8 bytes minimum for SRV
    packet[offset++] = 0x00;
    packet[offset++] = 0x08;
    
    // Priority (0)
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;
    
    // Weight (0)
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;
    
    // Port
    packet[offset++] = (ctx->listen_port >> 8) & 0xFF;
    packet[offset++] = ctx->listen_port & 0xFF;
    
    // Target: local
    packet[offset++] = 5;
    memcpy(&packet[offset], "local", 5);
    offset += 5;
    packet[offset++] = 0;
    
    *out_len = offset;
    return packet;
}

bool network_midi2_send_discovery_query(network_midi2_context_t* ctx) {
    if (!ctx) return false;
    
    if (!network_midi2_create_discovery_socket(ctx)) {
        return false;
    }
    
    int len = 0;
    uint8_t* query = network_midi2_create_mdns_query(&len);
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    inet_aton(MDNS_MULTICAST_ADDR, &addr.sin_addr);
    addr.sin_port = htons(MDNS_MULTICAST_PORT);
    
    int sent = sendto(ctx->discovery_socket, query, len, 0,
                      (struct sockaddr*)&addr, sizeof(addr));
    
    free(query);
    
    if (sent < 0) {
        network_midi2_logf(ctx, "[Discovery] Query send failed: %d", errno);
        return false;
    }
    
    network_midi2_log(ctx, "[Discovery] Sent mDNS query for _midi2._udp");
    return true;
}

int network_midi2_get_device_count(network_midi2_context_t* ctx) {
    if (!ctx) return 0;
    return ctx->discovered_count;
}

bool network_midi2_get_discovered_device(
    network_midi2_context_t* ctx,
    int index,
    char* device_name,
    uint32_t* ip_address,
    uint16_t* port)
{
    if (!ctx || index < 0 || index >= ctx->discovered_count) {
        return false;
    }
    
    discovered_device_t* dev = &ctx->discovered_devices[index];
    
    if (device_name) {
        strncpy(device_name, dev->device_name, 63);
    }
    if (ip_address) {
        *ip_address = dev->ip_address;
    }
    if (port) {
        *port = dev->port;
    }
    
    return true;
}

/* ============================================================================
 * Session Management Functions
 * ============================================================================ */

static void network_midi2_create_invitation_packet(network_midi2_context_t* ctx,
                                                   uint8_t* packet, int* length) {
    // 使用新协议模块构建 INV 包
    *length = nm2_protocol_build_inv(packet, 256,
                                      ctx->device_name,
                                      ctx->product_id,
                                      NM2_CAP_NONE);
}

bool network_midi2_session_initiate(
    network_midi2_context_t* ctx,
    uint32_t ip_address,
    uint16_t port,
    const char* remote_device_name)
{
    if (!ctx) return false;
    
    if (xSemaphoreTake(ctx->session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        network_midi2_log(ctx, "[Session] Mutex timeout");
        return false;
    }
    
    if (!network_midi2_create_data_socket(ctx)) {
        xSemaphoreGive(ctx->session_mutex);
        return false;
    }
    
    uint8_t packet[256];
    int length = 0;
    network_midi2_create_invitation_packet(ctx, packet, &length);
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip_address;
    addr.sin_port = htons(port);
    
    if (sendto(ctx->data_socket, packet, length, 0,
               (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        network_midi2_logf(ctx, "[Session] Send invitation failed: %d", errno);
        xSemaphoreGive(ctx->session_mutex);
        return false;
    }
    
    // Store session info
    ctx->current_session.ip_address = ip_address;
    ctx->current_session.port = port;
    ctx->current_session.remote_ssrc = 0;
    ctx->current_session.state = SESSION_STATE_INV_PENDING;
    ctx->session_state = SESSION_STATE_INV_PENDING;
    
    if (remote_device_name) {
        strncpy(ctx->current_session.device_name, remote_device_name, 63);
    }
    
    xSemaphoreGive(ctx->session_mutex);
    
    network_midi2_logf(ctx, "[Session] Sent INV to %08lX:%d (SSRC: 0x%08lX -> ?)",
                      (unsigned long)ip_address, port, (unsigned long)ctx->local_ssrc);
    
    return true;
}

bool network_midi2_session_accept(network_midi2_context_t* ctx) {
    if (!ctx) return false;
    
    if (xSemaphoreTake(ctx->session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    
    if (ctx->session_state != SESSION_STATE_INV_PENDING) {
        xSemaphoreGive(ctx->session_mutex);
        return false;
    }
    
    // 使用新协议模块构建 INV_ACCEPTED 包
    uint8_t packet[NM2_MAX_PACKET_SIZE];
    int packet_len = nm2_protocol_build_inv_accepted(packet, sizeof(packet),
                                                      ctx->device_name,
                                                      ctx->product_id);
    
    if (packet_len < 0) {
        xSemaphoreGive(ctx->session_mutex);
        return false;
    }
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ctx->current_session.ip_address;
    addr.sin_port = htons(ctx->current_session.port);
    
    if (sendto(ctx->data_socket, packet, packet_len, 0,
               (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        xSemaphoreGive(ctx->session_mutex);
        return false;
    }
    
    ctx->session_state = SESSION_STATE_ACTIVE;
    ctx->current_session.state = SESSION_STATE_ACTIVE;
    ctx->send_sequence = 0;
    
    xSemaphoreGive(ctx->session_mutex);
    
    network_midi2_logf(ctx, "[Session] Session accepted! SSRC: 0x%08lX <-> 0x%08lX",
                      (unsigned long)ctx->local_ssrc, (unsigned long)ctx->current_session.remote_ssrc);
    
    return true;
}

bool network_midi2_session_reject(network_midi2_context_t* ctx) {
    if (!ctx) return false;
    
    if (xSemaphoreTake(ctx->session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    
    if (ctx->session_state != SESSION_STATE_INV_PENDING) {
        xSemaphoreGive(ctx->session_mutex);
        return false;
    }
    
    // 使用新协议模块构建 BYE 包 (拒绝邀请)
    uint8_t packet[NM2_MAX_PACKET_SIZE];
    int packet_len = nm2_protocol_build_bye(packet, sizeof(packet),
                                             NM2_BYE_INV_REJECTED_BY_USER,
                                             "Session rejected");
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ctx->current_session.ip_address;
    addr.sin_port = htons(ctx->current_session.port);
    
    if (packet_len > 0) {
        sendto(ctx->data_socket, packet, packet_len, 0,
               (struct sockaddr*)&addr, sizeof(addr));
    }
    
    ctx->session_state = SESSION_STATE_IDLE;
    ctx->current_session.state = SESSION_STATE_IDLE;
    memset(&ctx->current_session, 0, sizeof(ctx->current_session));
    
    xSemaphoreGive(ctx->session_mutex);
    
    network_midi2_log(ctx, "[Session] Session rejected");
    return true;
}

bool network_midi2_session_terminate(network_midi2_context_t* ctx) {
    if (!ctx) return false;
    
    if (xSemaphoreTake(ctx->session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    
    if (ctx->session_state == SESSION_STATE_IDLE) {
        xSemaphoreGive(ctx->session_mutex);
        return true;
    }
    
    ctx->session_state = SESSION_STATE_CLOSING;
    
    // 使用新协议模块构建 BYE 包
    uint8_t packet[NM2_MAX_PACKET_SIZE];
    int packet_len = nm2_protocol_build_bye(packet, sizeof(packet),
                                             NM2_BYE_USER_TERMINATED,
                                             "Session terminated");
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ctx->current_session.ip_address;
    addr.sin_port = htons(ctx->current_session.port);
    
    if (packet_len > 0) {
        sendto(ctx->data_socket, packet, packet_len, 0,
               (struct sockaddr*)&addr, sizeof(addr));
    }
    
    ctx->session_state = SESSION_STATE_IDLE;
    memset(&ctx->current_session, 0, sizeof(ctx->current_session));
    
    xSemaphoreGive(ctx->session_mutex);
    
    network_midi2_log(ctx, "[Session] Session terminated");
    return true;
}

network_midi2_session_state_t network_midi2_get_session_state(
    network_midi2_context_t* ctx)
{
    if (!ctx) return SESSION_STATE_IDLE;
    return ctx->session_state;
}

bool network_midi2_is_session_active(network_midi2_context_t* ctx) {
    if (!ctx) return false;
    bool result = false;
    
    if (xSemaphoreTake(ctx->session_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        result = (ctx->session_state == SESSION_STATE_ACTIVE);
        xSemaphoreGive(ctx->session_mutex);
    }
    
    return result;
}

bool network_midi2_send_ping(network_midi2_context_t* ctx) {
    if (!ctx || ctx->session_state != SESSION_STATE_ACTIVE) {
        return false;
    }
    
    // 使用新协议模块构建 PING 包
    uint8_t packet[NM2_MAX_PACKET_SIZE];
    uint32_t ping_id = esp_random();
    int packet_len = nm2_protocol_build_ping(packet, sizeof(packet), ping_id);
    
    if (packet_len < 0) {
        return false;
    }
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ctx->current_session.ip_address;
    addr.sin_port = htons(ctx->current_session.port);
    
    if (sendto(ctx->data_socket, packet, packet_len, 0,
               (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        return false;
    }
    
    return true;
}

bool network_midi2_get_session_info(
    network_midi2_context_t* ctx,
    network_midi2_session_t* session)
{
    if (!ctx || !session) return false;
    
    if (xSemaphoreTake(ctx->session_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    
    if (ctx->session_state == SESSION_STATE_IDLE) {
        xSemaphoreGive(ctx->session_mutex);
        return false;
    }
    
    memcpy(session, &ctx->current_session, sizeof(network_midi2_session_t));
    xSemaphoreGive(ctx->session_mutex);
    return true;
}

/* ============================================================================
 * Data Transmission Functions
 * ============================================================================ */

static void network_midi2_create_ump_data_packet(network_midi2_context_t* ctx,
                                                 const uint8_t* ump_data,
                                                 uint16_t ump_len,
                                                 uint8_t* packet,
                                                 int* packet_len)
{
    // 使用新协议模块构建 UMP 数据包
    *packet_len = nm2_protocol_build_ump_data(packet, 256,
                                               ctx->send_sequence,
                                               ump_data, ump_len);
    ctx->send_sequence++;
}

static void network_midi2_midi_to_ump(uint8_t status, uint8_t data1, uint8_t data2,
                                      uint8_t* ump, int* ump_len) {
    // MIDI 1.0 Channel Voice Messages -> UMP System Real Time (0x2n)
    ump[0] = 0x20;  // MT=2 (MIDI 1.0), Group=0, reserved, status high bit
    ump[1] = status;
    ump[2] = data1;
    ump[3] = data2;
    *ump_len = 4;
}

bool network_midi2_send_midi(
    network_midi2_context_t* ctx,
    uint8_t status,
    uint8_t data1,
    uint8_t data2)
{
    if (!ctx || !network_midi2_is_session_active(ctx)) {
        return false;
    }
    
    uint8_t ump[4];
    int ump_len = 0;
    network_midi2_midi_to_ump(status, data1, data2, ump, &ump_len);
    
    return network_midi2_send_ump(ctx, ump, ump_len);
}

bool network_midi2_send_ump(
    network_midi2_context_t* ctx,
    const uint8_t* ump_data,
    uint16_t length)
{
    if (!ctx || !network_midi2_is_session_active(ctx) || !ump_data || length == 0) {
        return false;
    }
    
    uint8_t packet[256];
    int packet_len = 0;
    network_midi2_create_ump_data_packet(ctx, ump_data, length, packet, &packet_len);
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ctx->current_session.ip_address;
    addr.sin_port = htons(ctx->current_session.port);
    
    if (sendto(ctx->data_socket, packet, packet_len, 0,
               (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        network_midi2_logf(ctx, "[Send] UMP send failed: %d", errno);
        return false;
    }
    
    return true;
}

bool network_midi2_send_note_on(
    network_midi2_context_t* ctx,
    uint8_t note,
    uint8_t velocity,
    uint8_t channel)
{
    channel &= 0x0F;
    uint8_t status = 0x90 | channel;
    return network_midi2_send_midi(ctx, status, note, velocity);
}

bool network_midi2_send_note_off(
    network_midi2_context_t* ctx,
    uint8_t note,
    uint8_t velocity,
    uint8_t channel)
{
    channel &= 0x0F;
    uint8_t status = 0x80 | channel;
    return network_midi2_send_midi(ctx, status, note, velocity);
}

bool network_midi2_send_control_change(
    network_midi2_context_t* ctx,
    uint8_t controller,
    uint8_t value,
    uint8_t channel)
{
    channel &= 0x0F;
    uint8_t status = 0xB0 | channel;
    return network_midi2_send_midi(ctx, status, controller, value);
}

bool network_midi2_send_program_change(
    network_midi2_context_t* ctx,
    uint8_t program,
    uint8_t channel)
{
    channel &= 0x0F;
    uint8_t status = 0xC0 | channel;
    return network_midi2_send_midi(ctx, status, program, 0);
}

bool network_midi2_send_pitch_bend(
    network_midi2_context_t* ctx,
    int16_t bend,
    uint8_t channel)
{
    channel &= 0x0F;
    uint8_t status = 0xE0 | channel;
    // Convert -8192..8191 to 14-bit (0..16383)
    uint16_t bend_val = (uint16_t)(bend + 8192) & 0x3FFF;
    uint8_t data1 = bend_val & 0x7F;
    uint8_t data2 = (bend_val >> 7) & 0x7F;
    return network_midi2_send_midi(ctx, status, data1, data2);
}

/* ============================================================================
 * Receive and Processing
 * ============================================================================ */

static void network_midi2_process_received_packet(network_midi2_context_t* ctx,
                                                  const uint8_t* data,
                                                  int length)
{
    // 验证 UDP 签名
    if (!nm2_protocol_validate_signature(data, length)) {
        return;
    }
    
    // 解析命令包
    nm2_command_packet_t cmd;
    int parsed = nm2_protocol_parse_packet(data, length, &cmd, 1);
    
    if (parsed < 1) {
        return;
    }
    
    // UMP Data (0xFF)
    if (cmd.command == NM2_CMD_UMP_DATA) {
        uint16_t seq = ((uint16_t)cmd.specific1 << 8) | cmd.specific2;
        const uint8_t* ump_data = cmd.payload;
        int ump_len = cmd.payload_len;
        
        network_midi2_logf(ctx, "[RX] UMP seq=%d, len=%d", seq, ump_len);
        
        // Split UMP packets and invoke callbacks
        int offset = 0;
        while (offset + 4 <= ump_len) {
            int mt = (ump_data[offset] >> 4) & 0x0F;
            int pkt_size = (mt == 0x4) ? 8 : 4;
            
            if (offset + pkt_size > ump_len) break;
            
            // MIDI 1.0 System Real Time (MT=0x2)
            if (mt == 0x2 && pkt_size == 4) {
                if (ctx->midi_rx_callback && ctx->ump_rx_callback) {
                    // Both callbacks defined - let UMP callback handle it
                    ctx->ump_rx_callback(&ump_data[offset], pkt_size);
                } else if (ctx->midi_rx_callback) {
                    // Extract MIDI 1.0 from UMP
                    uint8_t midi[3] = {ump_data[offset + 1], ump_data[offset + 2], ump_data[offset + 3]};
                    ctx->midi_rx_callback(midi, 3);
                } else if (ctx->ump_rx_callback) {
                    ctx->ump_rx_callback(&ump_data[offset], pkt_size);
                }
            }
            // MIDI 2.0 Channel Voice(MT=0x4)
            else if (mt == 0x4 && pkt_size == 8) {
                if (ctx->ump_rx_callback) {
                    ctx->ump_rx_callback(&ump_data[offset], pkt_size);
                }
            }
            
            offset += pkt_size;
        }
        
        return;
    }
    
    // Session Commands
    switch (cmd.command) {
        case NM2_CMD_INV:  // INV (Invitation)
            if (xSemaphoreTake(ctx->session_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                // 解析邀请数据
                nm2_invitation_t inv;
                if (nm2_protocol_parse_inv(&cmd, &inv)) {
                    ctx->current_session.remote_ssrc = esp_random();  // 生成随机 SSRC
                    ctx->session_state = SESSION_STATE_INV_PENDING;
                    if (inv.ump_endpoint_name) {
                        strncpy(ctx->current_session.device_name, inv.ump_endpoint_name, 63);
                    }
                    network_midi2_logf(ctx, "[Session] INV received");
                }
                xSemaphoreGive(ctx->session_mutex);
            }
            break;
            
        case NM2_CMD_INV_ACCEPTED:  // INV_ACCEPTED
            if (xSemaphoreTake(ctx->session_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                nm2_invitation_reply_t reply;
                if (nm2_protocol_parse_inv_reply(&cmd, &reply)) {
                    if (reply.ump_endpoint_name) {
                        strncpy(ctx->current_session.device_name, reply.ump_endpoint_name, 63);
                    }
                }
                ctx->session_state = SESSION_STATE_ACTIVE;
                ctx->send_sequence = 0;
                network_midi2_logf(ctx, "[Session] INV ACCEPTED!");
                xSemaphoreGive(ctx->session_mutex);
            }
            break;
            
        case NM2_CMD_BYE:  // BYE (End Session)
            if (xSemaphoreTake(ctx->session_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                network_midi2_log(ctx, "[Session] BYE received");
                ctx->session_state = SESSION_STATE_IDLE;
                memset(&ctx->current_session, 0, sizeof(ctx->current_session));
                xSemaphoreGive(ctx->session_mutex);
            }
            break;
            
        case NM2_CMD_PING:  // PING
            {
                uint32_t ping_id;
                if (nm2_protocol_parse_ping(&cmd, &ping_id)) {
                    // 发送 PING_REPLY
                    uint8_t reply[NM2_MAX_PACKET_SIZE];
                    int reply_len = nm2_protocol_build_ping_reply(reply, sizeof(reply), ping_id);
                    
                    if (reply_len > 0) {
                        struct sockaddr_in addr = {0};
                        addr.sin_family = AF_INET;
                        addr.sin_addr.s_addr = ctx->current_session.ip_address;
                        addr.sin_port = htons(ctx->current_session.port);
                        sendto(ctx->data_socket, reply, reply_len, 0, (struct sockaddr*)&addr, sizeof(addr));
                        network_midi2_log(ctx, "[Session] PING received, sent PING_REPLY");
                    }
                }
            }
            break;
            
        default:
            network_midi2_logf(ctx, "[RX] Unknown command: 0x%02X", cmd.command);
            break;
    }
}

static void network_midi2_receive_task(void* arg) {
    network_midi2_context_t* ctx = (network_midi2_context_t*)arg;
    uint8_t buffer[512];
    
    while (ctx->is_running && ctx->data_socket >= 0) {
        struct sockaddr_in src_addr = {0};
        socklen_t src_addr_len = sizeof(src_addr);
        
        int n = recvfrom(ctx->data_socket, buffer, sizeof(buffer), 0,
                        (struct sockaddr*)&src_addr, &src_addr_len);
        
        if (n > 0) {
            network_midi2_process_received_packet(ctx, buffer, n);
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            if (ctx->is_running) {
                network_midi2_logf(ctx, "[RX] Socket error: %d", errno);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    vTaskDelete(NULL);
}

static void network_midi2_discovery_announce_task(void* arg) {
    network_midi2_context_t* ctx = (network_midi2_context_t*)arg;
    
    while (ctx->is_running && ctx->enable_discovery) {
        if (ctx->discovery_socket >= 0) {
            int len = 0;
            uint8_t* announcement = network_midi2_create_mdns_announcement(ctx, &len);
            
            struct sockaddr_in addr = {0};
            addr.sin_family = AF_INET;
            inet_aton(MDNS_MULTICAST_ADDR, &addr.sin_addr);
            addr.sin_port = htons(MDNS_MULTICAST_PORT);
            
            sendto(ctx->discovery_socket, announcement, len, 0,
                   (struct sockaddr*)&addr, sizeof(addr));
            
            free(announcement);
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000));  // Announce every 5 seconds
    }
    
    vTaskDelete(NULL);
}

bool network_midi2_start(network_midi2_context_t* ctx) {
    if (!ctx || ctx->is_running) return false;
    
    if (!network_midi2_create_data_socket(ctx)) {
        return false;
    }
    
    if (ctx->enable_discovery) {
        network_midi2_create_discovery_socket(ctx);
    }
    
    ctx->is_running = true;
    
    // Create receive task
    BaseType_t ret = xTaskCreate(
        network_midi2_receive_task,
        "nm2_rx",
        RECEIVE_TASK_STACK_SIZE,
        ctx,
        RECEIVE_TASK_PRIORITY,
        &ctx->receive_task_handle);
    
    if (ret != pdPASS) {
        network_midi2_log(ctx, "[Error] Failed to create receive task");
        ctx->is_running = false;
        return false;
    }
    
    // Create discovery announce task if enabled
    if (ctx->enable_discovery) {
        ret = xTaskCreate(
            network_midi2_discovery_announce_task,
            "nm2_disc",
            DISCOVERY_TASK_STACK_SIZE,
            ctx,
            RECEIVE_TASK_PRIORITY,
            &ctx->discovery_task_handle);
        
        if (ret != pdPASS) {
            network_midi2_log(ctx, "[Warn] Failed to create discovery task");
        }
    }
    
    network_midi2_logf(ctx, "[Start] Device started (mode: %d, discovery: %s)",
                      ctx->mode, ctx->enable_discovery ? "yes" : "no");
    
    return true;
}

void network_midi2_stop(network_midi2_context_t* ctx) {
    if (!ctx || !ctx->is_running) return;
    
    ctx->is_running = false;
    
    // Terminate session if active
    if (ctx->session_state != SESSION_STATE_IDLE) {
        network_midi2_session_terminate(ctx);
    }
    
    // Wait for tasks to finish
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Force delete tasks if still running
    if (ctx->receive_task_handle) {
        vTaskDelete(ctx->receive_task_handle);
        ctx->receive_task_handle = NULL;
    }
    
    if (ctx->discovery_task_handle) {
        vTaskDelete(ctx->discovery_task_handle);
        ctx->discovery_task_handle = NULL;
    }
    
    // Close sockets
    if (ctx->data_socket >= 0) {
        close(ctx->data_socket);
        ctx->data_socket = -1;
    }
    
    if (ctx->discovery_socket >= 0) {
        close(ctx->discovery_socket);
        ctx->discovery_socket = -1;
    }
    
    network_midi2_log(ctx, "[Stop] Device stopped");
}

/* ============================================================================
 * Callback Registration
 * ============================================================================ */

void network_midi2_set_log_callback(
    network_midi2_context_t* ctx,
    network_midi2_log_callback_t callback)
{
    if (ctx) {
        ctx->log_callback = callback;
    }
}

void network_midi2_set_midi_rx_callback(
    network_midi2_context_t* ctx,
    network_midi2_midi_rx_callback_t callback)
{
    if (ctx) {
        ctx->midi_rx_callback = callback;
    }
}

void network_midi2_set_ump_rx_callback(
    network_midi2_context_t* ctx,
    network_midi2_ump_rx_callback_t callback)
{
    if (ctx) {
        ctx->ump_rx_callback = callback;
    }
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

const char* network_midi2_get_version(void) {
    return NETWORK_MIDI2_VERSION;
}

int network_midi2_midi_to_string(
    uint8_t status,
    uint8_t data1,
    uint8_t data2,
    char* buffer,
    int buffer_len)
{
    if (!buffer || buffer_len < 32) return 0;
    
    uint8_t cmd = status & 0xF0;
    uint8_t ch = status & 0x0F;
    
    int len = 0;
    
    switch (cmd) {
        case 0x80:
            len = snprintf(buffer, buffer_len, "NoteOff(ch%d, note=%d, vel=%d)",
                          ch, data1, data2);
            break;
        case 0x90:
            len = snprintf(buffer, buffer_len, "NoteOn(ch%d, note=%d, vel=%d)",
                          ch, data1, data2);
            break;
        case 0xA0:
            len = snprintf(buffer, buffer_len, "PolyAftertouch(ch%d, note=%d, val=%d)",
                          ch, data1, data2);
            break;
        case 0xB0:
            len = snprintf(buffer, buffer_len, "CC(ch%d, ctrl=%d, val=%d)",
                          ch, data1, data2);
            break;
        case 0xC0:
            len = snprintf(buffer, buffer_len, "ProgramChange(ch%d, prog=%d)",
                          ch, data1);
            break;
        case 0xD0:
            len = snprintf(buffer, buffer_len, "ChannelAftertouch(ch%d, val=%d)",
                          ch, data1);
            break;
        case 0xE0:
            len = snprintf(buffer, buffer_len, "PitchBend(ch%d, val=%d)",
                          ch, data1 | (data2 << 7));
            break;
        default:
            len = snprintf(buffer, buffer_len, "Unknown(%02X %02X %02X)",
                          status, data1, data2);
            break;
    }
    
    return len;
}

