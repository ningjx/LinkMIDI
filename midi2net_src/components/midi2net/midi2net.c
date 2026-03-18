#include "midi2net.h"
#include "midi2net_priv.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "midi2net";

midi2net_server_ctx_t g_midi2net = {
    .sock = -1,
};

static TaskHandle_t s_rx_task = NULL;

static void lock_ctx(void)
{
    if (g_midi2net.lock)
    {
        xSemaphoreTake(g_midi2net.lock, portMAX_DELAY);
    }
}

static void unlock_ctx(void)
{
    if (g_midi2net.lock)
    {
        xSemaphoreGive(g_midi2net.lock);
    }
}

static void set_connected(const struct sockaddr_in *remote, uint8_t remote_ssrc)
{
    lock_ctx();
    g_midi2net.remote_addr = *remote;
    g_midi2net.remote_ssrc = remote_ssrc;
    g_midi2net.connected = true;
    unlock_ctx();
}

static void clear_session(void)
{
    lock_ctx();
    memset(&g_midi2net.remote_addr, 0, sizeof(g_midi2net.remote_addr));
    g_midi2net.remote_ssrc = 0;
    g_midi2net.connected = false;
    unlock_ctx();
}

static void send_accept(const struct sockaddr_in *remote, uint8_t remote_ssrc)
{
    // { 0x01, 0x01, local_ssrc, remote_ssrc }
    uint8_t pkt[4];
    pkt[0] = 0x01;
    pkt[1] = 0x01;
    pkt[2] = g_midi2net.local_ssrc;
    pkt[3] = remote_ssrc;
    sendto(g_midi2net.sock, pkt, sizeof(pkt), 0, (const struct sockaddr *)remote, sizeof(*remote));
}

static void send_pong(const struct sockaddr_in *remote, uint8_t remote_ssrc)
{
    // { 0x03, 0x01, local_ssrc, remote_ssrc }
    uint8_t pkt[4];
    pkt[0] = 0x03;
    pkt[1] = 0x01;
    pkt[2] = g_midi2net.local_ssrc;
    pkt[3] = remote_ssrc;
    sendto(g_midi2net.sock, pkt, sizeof(pkt), 0, (const struct sockaddr *)remote, sizeof(*remote));
}

static void rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[1500];

    while (1)
    {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        int r = recvfrom(g_midi2net.sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);
        if (r < 0)
        {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (r < 4)
        {
            continue;
        }

        const uint8_t cmdByte = buf[0];
        const uint8_t cmd = (uint8_t)(cmdByte & 0x0F);
        const uint8_t status = buf[1];
        const uint8_t ssrc = buf[2];        // sender ssrc
        const uint8_t remote_ssrc = buf[3]; // receiver ssrc (as provided by peer)

        if (cmdByte == 0x10)
        {
            // UMP data, ignore for now (we only need sending in this task request)
            continue;
        }

        switch (cmd)
        {
        case 0x01: // INV
            if (status == 0x00)
            {
                // accept anyone, store session
                set_connected(&from, ssrc);
                send_accept(&from, ssrc);
                ESP_LOGI(TAG, "INV from %s:%u ssrc=%02X -> ACCEPT (local=%02X)",
                         inet_ntoa(from.sin_addr), ntohs(from.sin_port), ssrc, g_midi2net.local_ssrc);
            }
            break;

        case 0x02: // END
            ESP_LOGI(TAG, "END from %s:%u", inet_ntoa(from.sin_addr), ntohs(from.sin_port));
            clear_session();
            break;

        case 0x03: // PING/PONG
            if (status == 0x00)
            {
                send_pong(&from, ssrc);
            }
            break;
        default:
            break;
        }
    }
}

static esp_err_t send_ump8(const uint8_t ump[8])
{
    if (g_midi2net.sock < 0)
    {
        return ESP_ERR_INVALID_STATE;
    }

    struct sockaddr_in remote;
    bool ok = false;
    lock_ctx();
    ok = g_midi2net.connected;
    remote = g_midi2net.remote_addr;
    unlock_ctx();

    if (!ok)
    {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t pkt[4 + 8];
    pkt[0] = 0x10;
    pkt[1] = (uint8_t)((g_midi2net.seq >> 8) & 0xFF);
    pkt[2] = (uint8_t)(g_midi2net.seq & 0xFF);
    pkt[3] = 0x00;
    memcpy(&pkt[4], ump, 8);

    g_midi2net.seq++;
    int s = sendto(g_midi2net.sock, pkt, sizeof(pkt), 0, (const struct sockaddr *)&remote, sizeof(remote));
    return (s == sizeof(pkt)) ? ESP_OK : ESP_FAIL;
}

esp_err_t midi2net_server_start(const midi2net_server_config_t *cfg)
{
    if (!cfg)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (g_midi2net.sock >= 0)
    {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&g_midi2net, 0, sizeof(g_midi2net));
    g_midi2net.sock = -1;
    g_midi2net.lock = xSemaphoreCreateMutex();
    g_midi2net.port = (cfg->port != 0) ? cfg->port : 5506;

    // Copy strings
    if (cfg->instance_name)
    {
        strncpy(g_midi2net.instance_name, cfg->instance_name, sizeof(g_midi2net.instance_name) - 1);
    }
    if (cfg->hostname)
    {
        strncpy(g_midi2net.hostname, cfg->hostname, sizeof(g_midi2net.hostname) - 1);
    }
    if (cfg->product_instance_id)
    {
        strncpy(g_midi2net.product_instance_id, cfg->product_instance_id, sizeof(g_midi2net.product_instance_id) - 1);
    }

    // SSRC: 1..255
    g_midi2net.local_ssrc = (uint8_t)((esp_random() % 254) + 1);
    g_midi2net.seq = 0;

    // Create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "socket() failed");
        return ESP_FAIL;
    }

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(g_midi2net.port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        ESP_LOGE(TAG, "bind() failed on port %u", g_midi2net.port);
        close(sock);
        return ESP_FAIL;
    }

    g_midi2net.sock = sock;
    g_midi2net.local_addr = addr;

    midi2net_mdns_start(&g_midi2net);

    xTaskCreate(rx_task, "midi2net_rx", 4096, NULL, 5, &s_rx_task);

    ESP_LOGI(TAG, "Server started port=%u local_ssrc=%02X", g_midi2net.port, g_midi2net.local_ssrc);
    return ESP_OK;
}

void midi2net_server_stop(void)
{
    if (g_midi2net.sock < 0)
    {
        return;
    }

    // Best effort: stop task
    if (s_rx_task)
    {
        vTaskDelete(s_rx_task);
        s_rx_task = NULL;
    }

    midi2net_mdns_stop();

    close(g_midi2net.sock);
    g_midi2net.sock = -1;

    clear_session();

    if (g_midi2net.lock)
    {
        vSemaphoreDelete(g_midi2net.lock);
        g_midi2net.lock = NULL;
    }
}

bool midi2net_server_is_connected(void)
{
    bool ok;
    lock_ctx();
    ok = g_midi2net.connected;
    unlock_ctx();
    return ok;
}

midi2net_server_status_t midi2net_server_get_status(void)
{
    midi2net_server_status_t st = {0};
    lock_ctx();
    st.connected = g_midi2net.connected;
    st.remote_ip = g_midi2net.remote_addr.sin_addr.s_addr;
    st.remote_port = ntohs(g_midi2net.remote_addr.sin_port);
    st.local_ssrc = g_midi2net.local_ssrc;
    st.remote_ssrc = g_midi2net.remote_ssrc;
    unlock_ctx();
    return st;
}

esp_err_t midi2net_server_send_note_on(uint8_t note, uint8_t velocity, uint8_t channel)
{
    // UMP 64-bit MIDI2 Channel Voice (mt=0x4, group=0)
    // Following the simplified packing used in your C# test:
    // byte0 = 0x40 (mt=4, group=0)
    // byte1 = status (0x90|ch)
    // byte2 = note
    // byte5..6 = vel16 (velocity<<9)
    const uint8_t status = (uint8_t)(0x90 | (channel & 0x0F));
    const uint16_t vel16 = (uint16_t)(velocity << 9);

    uint8_t ump[8] = {0};
    ump[0] = 0x40;
    ump[1] = status;
    ump[2] = note;
    ump[3] = 0;
    ump[4] = 0;
    ump[5] = (uint8_t)((vel16 >> 8) & 0xFF);
    ump[6] = (uint8_t)(vel16 & 0xFF);
    ump[7] = 0;

    return send_ump8(ump);
}

esp_err_t midi2net_server_send_note_off(uint8_t note, uint8_t channel)
{
    const uint8_t status = (uint8_t)(0x80 | (channel & 0x0F));
    uint8_t ump[8] = {0};
    ump[0] = 0x40;
    ump[1] = status;
    ump[2] = note;
    // rest already 0
    return send_ump8(ump);
}

