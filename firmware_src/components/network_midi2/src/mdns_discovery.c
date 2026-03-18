#include "mdns_discovery.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static const char* TAG = "MDNS_DISCOVERY";

#define MDNS_MULTICAST_ADDR "224.0.0.251"
#define MDNS_MULTICAST_PORT 5353
#define MAX_DISCOVERED_DEVICES 16
#define DISCOVERY_TASK_STACK_SIZE 4096

/**
 * @brief Discovered device structure
 */
typedef struct {
    uint32_t ip_address;
    uint16_t port;
    char device_name[64];
    char product_id[64];
    uint8_t ssrc;
} discovered_device_t;

/**
 * @brief mDNS discovery context
 */
typedef struct mdns_discovery_context {
    char device_name[64];
    char product_id[64];
    uint16_t listen_port;
    
    int discovery_socket;
    bool is_running;
    
    discovered_device_t discovered_devices[MAX_DISCOVERED_DEVICES];
    int discovered_count;
    
    TaskHandle_t discovery_task_handle;
    SemaphoreHandle_t device_list_mutex;
} mdns_discovery_context_t;

/**
 * @brief Encode DNS name into DNS wire format
 */
static void mdns_encode_name(const char* name, uint8_t* buffer, int* offset) {
    const char* ptr = name;
    while (*ptr) {
        const char* dot = strchr(ptr, '.');
        int len = dot ? (dot - ptr) : (int)strlen(ptr);
        
        buffer[(*offset)++] = (uint8_t)len;
        memcpy(&buffer[*offset], ptr, len);
        (*offset) += len;
        
        ptr += len;
        if (*ptr == '.') ptr++;
    }
    buffer[(*offset)++] = 0;
}

/**
 * @brief Create mDNS query packet
 */
static uint8_t* mdns_create_query(int* out_len) {
    uint8_t* packet = malloc(512);
    if (!packet) return NULL;
    
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
    mdns_encode_name("_midi2._udp.local", packet, &offset);
    packet[offset++] = 0x00;  // Type: PTR (12)
    packet[offset++] = 0x0C;
    packet[offset++] = 0x00;  // Class: IN
    packet[offset++] = 0x01;
    
    *out_len = offset;
    return packet;
}

/**
 * @brief Create mDNS announcement packet
 */
static uint8_t* mdns_create_announcement(
    const char* device_name,
    const char* product_id,
    uint16_t port,
    int* out_len) {
    
    uint8_t* packet = malloc(512);
    if (!packet) return NULL;
    
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
    snprintf(service_name, sizeof(service_name), "%s._midi2._udp.local", device_name);
    mdns_encode_name(service_name, packet, &offset);
    
    packet[offset++] = 0x00;  // Type: SRV (33)
    packet[offset++] = 0x21;
    packet[offset++] = 0x00;  // Class: IN
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
    packet[offset++] = (port >> 8) & 0xFF;
    packet[offset++] = port & 0xFF;
    
    // Target: local
    packet[offset++] = 5;
    memcpy(&packet[offset], "local", 5);
    offset += 5;
    packet[offset++] = 0;
    
    *out_len = offset;
    return packet;
}

/**
 * @brief Discovery task - listens for mDNS queries and responds
 */
static void discovery_task(void* arg) {
    mdns_discovery_context_t* ctx = (mdns_discovery_context_t*)arg;
    uint8_t buffer[512];
    struct sockaddr_in src_addr;
    socklen_t src_addr_len = sizeof(src_addr);
    
    ESP_LOGI(TAG, "Discovery task started");
    
    while (ctx->is_running) {
        int received = recvfrom(ctx->discovery_socket, buffer, sizeof(buffer), 0,
                               (struct sockaddr*)&src_addr, &src_addr_len);
        
        if (received > 0) {
            // Periodically send announcement
            if (received > 12) {  // Valid DNS packet
                int announce_len = 0;
                uint8_t* announce = mdns_create_announcement(
                    ctx->device_name,
                    ctx->product_id,
                    ctx->listen_port,
                    &announce_len);
                
                if (announce) {
                    struct sockaddr_in addr = {0};
                    addr.sin_family = AF_INET;
                    inet_aton(MDNS_MULTICAST_ADDR, &addr.sin_addr);
                    addr.sin_port = htons(MDNS_MULTICAST_PORT);
                    
                    sendto(ctx->discovery_socket, announce, announce_len, 0,
                           (struct sockaddr*)&addr, sizeof(addr));
                    
                    free(announce);
                }
            }
        } else if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            ESP_LOGE(TAG, "Discovery receive error: %d", errno);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(TAG, "Discovery task ended");
    vTaskDelete(NULL);
}

/* ============================================================================
 * Public API Functions
 * ============================================================================ */

mdns_discovery_context_t* mdns_discovery_init(
    const char* device_name,
    const char* product_id,
    uint16_t port) {
    
    if (!device_name || !product_id) {
        ESP_LOGE(TAG, "Invalid parameters");
        return NULL;
    }
    
    mdns_discovery_context_t* ctx = calloc(1, sizeof(mdns_discovery_context_t));
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to allocate context");
        return NULL;
    }
    
    strncpy(ctx->device_name, device_name, sizeof(ctx->device_name) - 1);
    strncpy(ctx->product_id, product_id, sizeof(ctx->product_id) - 1);
    ctx->listen_port = port;
    ctx->discovery_socket = -1;
    ctx->is_running = false;
    ctx->discovered_count = 0;
    
    ctx->device_list_mutex = xSemaphoreCreateMutex();
    if (!ctx->device_list_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(ctx);
        return NULL;
    }
    
    ESP_LOGI(TAG, "Initialized mDNS discovery for device: %s", device_name);
    return ctx;
}

void mdns_discovery_deinit(mdns_discovery_context_t* ctx) {
    if (!ctx) return;
    
    if (ctx->is_running) {
        mdns_discovery_stop(ctx);
    }
    
    if (ctx->device_list_mutex) {
        vSemaphoreDelete(ctx->device_list_mutex);
    }
    
    free(ctx);
}

bool mdns_discovery_start(mdns_discovery_context_t* ctx) {
    if (!ctx || ctx->is_running) {
        return false;
    }
    
    // Create discovery socket
    ctx->discovery_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ctx->discovery_socket < 0) {
        ESP_LOGE(TAG, "Failed to create discovery socket: %d", errno);
        return false;
    }
    
    // Set socket options
    int reuse = 1;
    setsockopt(ctx->discovery_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    // Set timeout
    struct timeval tv = {0};
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(ctx->discovery_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    // Bind to mDNS port
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(MDNS_MULTICAST_PORT);
    
    if (bind(ctx->discovery_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind discovery socket: %d", errno);
        close(ctx->discovery_socket);
        ctx->discovery_socket = -1;
        return false;
    }
    
    // Join multicast group
    struct ip_mreq mreq = {0};
    if (inet_aton(MDNS_MULTICAST_ADDR, &mreq.imr_multiaddr) == 0) {
        ESP_LOGE(TAG, "Invalid multicast address");
        close(ctx->discovery_socket);
        ctx->discovery_socket = -1;
        return false;
    }
    mreq.imr_interface.s_addr = INADDR_ANY;
    
    if (setsockopt(ctx->discovery_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        ESP_LOGE(TAG, "Failed to join multicast group: %d", errno);
        close(ctx->discovery_socket);
        ctx->discovery_socket = -1;
        return false;
    }
    
    ctx->is_running = true;
    
    // Create discovery task
    if (xTaskCreate(discovery_task, "mdns_discovery", DISCOVERY_TASK_STACK_SIZE, ctx, 4, 
                   &ctx->discovery_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create discovery task");
        ctx->is_running = false;
        close(ctx->discovery_socket);
        ctx->discovery_socket = -1;
        return false;
    }
    
    ESP_LOGI(TAG, "mDNS discovery started");
    return true;
}

void mdns_discovery_stop(mdns_discovery_context_t* ctx) {
    if (!ctx || !ctx->is_running) {
        return;
    }
    
    ctx->is_running = false;
    
    // Wait for task to finish (up to 2 seconds)
    for (int i = 0; i < 20; i++) {
        if (ctx->discovery_task_handle == NULL) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    if (ctx->discovery_socket >= 0) {
        close(ctx->discovery_socket);
        ctx->discovery_socket = -1;
    }
    
    ESP_LOGI(TAG, "mDNS discovery stopped");
}

bool mdns_discovery_send_query(mdns_discovery_context_t* ctx) {
    if (!ctx || !ctx->is_running) {
        return false;
    }
    
    int len = 0;
    uint8_t* query = mdns_create_query(&len);
    if (!query) {
        return false;
    }
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    inet_aton(MDNS_MULTICAST_ADDR, &addr.sin_addr);
    addr.sin_port = htons(MDNS_MULTICAST_PORT);
    
    int sent = sendto(ctx->discovery_socket, query, len, 0,
                      (struct sockaddr*)&addr, sizeof(addr));
    
    free(query);
    
    if (sent < 0) {
        ESP_LOGE(TAG, "Query send failed: %d", errno);
        return false;
    }
    
    ESP_LOGI(TAG, "Sent mDNS query for _midi2._udp");
    return true;
}

bool mdns_discovery_get_device(
    mdns_discovery_context_t* ctx,
    int index,
    char* device_name,
    uint32_t* ip_address,
    uint16_t* port) {
    
    if (!ctx || index < 0) {
        return false;
    }
    
    if (xSemaphoreTake(ctx->device_list_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    
    bool result = false;
    if (index < ctx->discovered_count) {
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
        result = true;
    }
    
    xSemaphoreGive(ctx->device_list_mutex);
    return result;
}

int mdns_discovery_get_device_count(mdns_discovery_context_t* ctx) {
    if (!ctx) {
        return 0;
    }
    
    int count = 0;
    if (xSemaphoreTake(ctx->device_list_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        count = ctx->discovered_count;
        xSemaphoreGive(ctx->device_list_mutex);
    }
    
    return count;
}

void mdns_discovery_clear_devices(mdns_discovery_context_t* ctx) {
    if (!ctx) {
        return;
    }
    
    if (xSemaphoreTake(ctx->device_list_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ctx->discovered_count = 0;
        memset(ctx->discovered_devices, 0, sizeof(ctx->discovered_devices));
        xSemaphoreGive(ctx->device_list_mutex);
    }
    
    ESP_LOGI(TAG, "Cleared discovered devices list");
}
