#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"

typedef struct
{
    // config
    char instance_name[64];
    char hostname[64];
    char product_instance_id[64];
    uint16_t port;

    // networking
    int sock;
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_addr;

    // session
    bool connected;
    uint8_t local_ssrc;
    uint8_t remote_ssrc;
    uint16_t seq;

    // sync
    SemaphoreHandle_t lock;
} midi2net_server_ctx_t;

extern midi2net_server_ctx_t g_midi2net;

void midi2net_mdns_start(const midi2net_server_ctx_t *ctx);
void midi2net_mdns_stop(void);

