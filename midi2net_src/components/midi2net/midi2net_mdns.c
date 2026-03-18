#include "midi2net_priv.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "midi2net_mdns";

#define MDNS_ADDR "224.0.0.251"
#define MDNS_PORT 5353

static TaskHandle_t s_mdns_rx_task = NULL;
static TaskHandle_t s_mdns_tx_task = NULL;
static int s_mdns_sock = -1;

static void write_name(uint8_t *out, size_t out_len, size_t *off, const char *name)
{
    // name like "ESP32S3NM2._midi2._udp.local"
    const char *p = name;
    while (*p && *off < out_len)
    {
        const char *dot = strchr(p, '.');
        size_t lab_len = dot ? (size_t)(dot - p) : strlen(p);
        if (*off + 1 + lab_len >= out_len)
        {
            return;
        }
        out[(*off)++] = (uint8_t)lab_len;
        memcpy(&out[*off], p, lab_len);
        *off += lab_len;
        if (!dot)
        {
            break;
        }
        p = dot + 1;
    }
    if (*off < out_len)
    {
        out[(*off)++] = 0;
    }
}

static uint32_t get_local_ipv4_be(void)
{
    esp_netif_ip_info_t ip;
    memset(&ip, 0, sizeof(ip));
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif)
    {
        return 0;
    }
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK)
    {
        return 0;
    }
    return ip.ip.addr; // already network byte order
}

static int build_srv_announcement(uint8_t *out, size_t out_len, const midi2net_server_ctx_t *ctx)
{
    // Minimal mDNS response with 1 SRV answer:
    // NAME = <instance>._midi2._udp.local
    // TYPE = SRV(33), CLASS=IN(1), TTL=4500
    // RDATA = priority(0) weight(0) port + target(<hostname>.local)
    const char *instance = (ctx->instance_name[0] != 0) ? ctx->instance_name : "ESP32S3NM2";
    const char *hostname = (ctx->hostname[0] != 0) ? ctx->hostname : "esp32s3-nm2";

    char service_name[128];
    char target_name[128];
    snprintf(service_name, sizeof(service_name), "%s._midi2._udp.local", instance);
    snprintf(target_name, sizeof(target_name), "%s.local", hostname);

    size_t off = 0;
    if (out_len < 12)
    {
        return -1;
    }

    // DNS header
    out[off++] = 0x00;
    out[off++] = 0x00; // ID
    out[off++] = 0x84;
    out[off++] = 0x00; // flags: response + authoritative
    out[off++] = 0x00;
    out[off++] = 0x00; // QDCOUNT
    out[off++] = 0x00;
    out[off++] = 0x01; // ANCOUNT = 1
    out[off++] = 0x00;
    out[off++] = 0x00; // NSCOUNT
    out[off++] = 0x00;
    out[off++] = 0x00; // ARCOUNT

    // Answer: NAME
    write_name(out, out_len, &off, service_name);
    if (off + 10 >= out_len)
    {
        return -1;
    }

    // TYPE=SRV(33), CLASS=IN(1)
    out[off++] = 0x00;
    out[off++] = 0x21;
    out[off++] = 0x00;
    out[off++] = 0x01;
    // TTL=4500
    out[off++] = 0x00;
    out[off++] = 0x00;
    out[off++] = 0x11;
    out[off++] = 0x94;

    // Reserve RDLENGTH
    size_t rdlen_pos = off;
    out[off++] = 0x00;
    out[off++] = 0x00;

    size_t rdata_start = off;
    // priority, weight
    out[off++] = 0x00;
    out[off++] = 0x00;
    out[off++] = 0x00;
    out[off++] = 0x00;
    // port
    out[off++] = (uint8_t)((ctx->port >> 8) & 0xFF);
    out[off++] = (uint8_t)(ctx->port & 0xFF);
    // target
    write_name(out, out_len, &off, target_name);

    uint16_t rdlen = (uint16_t)(off - rdata_start);
    out[rdlen_pos] = (uint8_t)((rdlen >> 8) & 0xFF);
    out[rdlen_pos + 1] = (uint8_t)(rdlen & 0xFF);

    return (int)off;
}

static int skip_name(const uint8_t *data, int len, int off)
{
    while (off < len)
    {
        uint8_t l = data[off++];
        if (l == 0)
        {
            break;
        }
        if ((l & 0xC0) == 0xC0)
        {
            // compression pointer: 2 bytes
            off++;
            break;
        }
        off += l;
    }
    return off;
}

static void mdns_send_announcement(const midi2net_server_ctx_t *ctx)
{
    uint8_t pkt[512];
    int n = build_srv_announcement(pkt, sizeof(pkt), ctx);
    if (n <= 0)
    {
        return;
    }

    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(MDNS_PORT);
    inet_aton(MDNS_ADDR, &to.sin_addr);

    sendto(s_mdns_sock, pkt, n, 0, (struct sockaddr *)&to, sizeof(to));
}

static void mdns_tx_task(void *arg)
{
    const midi2net_server_ctx_t *ctx = (const midi2net_server_ctx_t *)arg;
    while (1)
    {
        mdns_send_announcement(ctx);
        ESP_LOGI(TAG, "mDNS announce: %s._midi2._udp.local port=%u", ctx->instance_name, ctx->port);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void mdns_rx_task(void *arg)
{
    const midi2net_server_ctx_t *ctx = (const midi2net_server_ctx_t *)arg;
    uint8_t buf[512];

    while (1)
    {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        int r = recvfrom(s_mdns_sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);
        if (r < 12)
        {
            continue;
        }

        // Only respond to queries (QR=0)
        uint16_t flags = (uint16_t)((buf[2] << 8) | buf[3]);
        if (flags & 0x8000)
        {
            continue;
        }

        uint16_t qdcount = (uint16_t)((buf[4] << 8) | buf[5]);
        int off = 12;
        for (uint16_t i = 0; i < qdcount && off < r; i++)
        {
            int name_start = off;
            off = skip_name(buf, r, off);
            if (off + 4 > r)
            {
                break;
            }
            uint16_t qtype = (uint16_t)((buf[off] << 8) | buf[off + 1]);
            off += 4; // qtype+qclass

            // Cheap string match in the raw name section for "_midi2" and "_udp".
            // Works with typical uncompressed queries.
            bool match = false;
            for (int j = name_start; j < off && j + 6 < r; j++)
            {
                if (memcmp(&buf[j], "_midi2", 6) == 0)
                {
                    match = true;
                    break;
                }
            }

            if (match && qtype == 0x0021)
            {
                mdns_send_announcement(ctx);
            }
        }
    }
}

static int mdns_socket_create(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        return -1;
    }

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(MDNS_PORT);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        close(sock);
        return -1;
    }

    // join multicast 224.0.0.251
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(MDNS_ADDR);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    return sock;
}

void midi2net_mdns_start(const midi2net_server_ctx_t *ctx)
{
    if (s_mdns_sock >= 0)
    {
        return;
    }

    (void)get_local_ipv4_be(); // ensure netif exists (best-effort)

    s_mdns_sock = mdns_socket_create();
    if (s_mdns_sock < 0)
    {
        ESP_LOGW(TAG, "mDNS socket create failed");
        return;
    }

    xTaskCreate(mdns_rx_task, "midi2net_mdns_rx", 4096, (void *)ctx, 4, &s_mdns_rx_task);
    xTaskCreate(mdns_tx_task, "midi2net_mdns_tx", 4096, (void *)ctx, 3, &s_mdns_tx_task);
}

void midi2net_mdns_stop(void)
{
    if (s_mdns_rx_task)
    {
        vTaskDelete(s_mdns_rx_task);
        s_mdns_rx_task = NULL;
    }
    if (s_mdns_tx_task)
    {
        vTaskDelete(s_mdns_tx_task);
        s_mdns_tx_task = NULL;
    }
    if (s_mdns_sock >= 0)
    {
        close(s_mdns_sock);
        s_mdns_sock = -1;
    }
}

