#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    const char *instance_name;        // mDNS instance name (e.g. "ESP32S3NM2")
    const char *hostname;             // mDNS hostname without ".local" (optional, NULL -> "esp32s3-nm2")
    const char *product_instance_id;  // TXT: productInstanceId=...
    uint16_t port;                    // UDP port for NM2 session/data (e.g. 5506/5507)
} midi2net_server_config_t;

typedef struct
{
    bool connected;
    uint32_t remote_ip;   // network byte order (lwIP ip4_addr_t.addr style)
    uint16_t remote_port; // host order
    uint8_t local_ssrc;
    uint8_t remote_ssrc;
} midi2net_server_status_t;

esp_err_t midi2net_server_start(const midi2net_server_config_t *cfg);
void midi2net_server_stop(void);

bool midi2net_server_is_connected(void);
midi2net_server_status_t midi2net_server_get_status(void);

esp_err_t midi2net_server_send_note_on(uint8_t note, uint8_t velocity, uint8_t channel);
esp_err_t midi2net_server_send_note_off(uint8_t note, uint8_t channel);

#ifdef __cplusplus
}
#endif

