#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/inet.h"

static const char* TAG = "WIFI_MGR";

/* NVS存储键名 */
#define NVS_NAMESPACE      "wifi_config"
#define NVS_KEY_SSID       "ssid"
#define NVS_KEY_PASSWORD   "password"

/* FreeRTOS event group for WiFi events */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t g_wifi_event_group = NULL;
static int g_retry_num = 0;
static wifi_run_mode_t g_wifi_mode = WIFI_RUN_MODE_NONE;
static esp_netif_t* g_sta_netif = NULL;
static esp_netif_t* g_ap_netif = NULL;
static char g_current_ip[16] = {0};

/**
 * @brief WiFi event handler
 */
static void event_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        g_wifi_mode = WIFI_RUN_MODE_STA_TRYING;
        if (g_retry_num < WIFI_CONNECT_MAX_RETRY) {
            ESP_LOGI(TAG, "WiFi disconnected, retrying (attempt %d/%d)",
                    g_retry_num + 1, WIFI_CONNECT_MAX_RETRY);
            esp_wifi_connect();
            g_retry_num++;
        } else {
            ESP_LOGW(TAG, "WiFi connection failed after %d attempts", WIFI_CONNECT_MAX_RETRY);
            xEventGroupSetBits(g_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        snprintf(g_current_ip, sizeof(g_current_ip), IPSTR, IP2STR(&event->ip_info.ip));
        g_retry_num = 0;
        g_wifi_mode = WIFI_RUN_MODE_STA_CONNECTED;
        xEventGroupSetBits(g_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Station connected to AP");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "Station disconnected from AP");
    }
}

bool wifi_manager_init(void) {
    // NVS already initialized by config_manager
    
    // Initialize network stack
    ESP_LOGI(TAG, "Initializing network stack...");
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default event loop
    ESP_LOGI(TAG, "Creating default event loop...");
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create WiFi interfaces
    g_sta_netif = esp_netif_create_default_wifi_sta();
    g_ap_netif = esp_netif_create_default_wifi_ap();
    
    // Initialize WiFi driver with default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Create WiFi event group
    g_wifi_event_group = xEventGroupCreate();
    if (!g_wifi_event_group) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        return false;
    }
    
    // Register WiFi event handler
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                               &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                               &event_handler, NULL));
    
    g_wifi_mode = WIFI_RUN_MODE_NONE;
    ESP_LOGI(TAG, "WiFi manager initialized successfully");
    return true;
}

/**
 * @brief 从NVS读取WiFi凭据
 */
static bool load_credentials(char* ssid, size_t ssid_len, char* password, size_t pass_len) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No saved WiFi credentials found");
        return false;
    }
    
    size_t len;
    len = ssid_len;
    err = nvs_get_str(handle, NVS_KEY_SSID, ssid, &len);
    if (err != ESP_OK) {
        nvs_close(handle);
        return false;
    }
    
    len = pass_len;
    err = nvs_get_str(handle, NVS_KEY_PASSWORD, password, &len);
    nvs_close(handle);
    
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return false;
    }
    
    return true;
}

bool wifi_manager_connect(const char* ssid, const char* password) {
    if (!g_wifi_event_group) {
        ESP_LOGE(TAG, "WiFi manager not initialized");
        return false;
    }
    
    char wifi_ssid[32] = {0};
    char wifi_pass[64] = {0};
    
    if (ssid && password) {
        // 使用传入的凭据
        strncpy(wifi_ssid, ssid, sizeof(wifi_ssid) - 1);
        strncpy(wifi_pass, password, sizeof(wifi_pass) - 1);
    } else {
        // 从NVS读取存储的凭据
        if (!load_credentials(wifi_ssid, sizeof(wifi_ssid), wifi_pass, sizeof(wifi_pass))) {
            ESP_LOGW(TAG, "No WiFi credentials, will start AP mode");
            return false;
        }
    }
    
    if (strlen(wifi_ssid) == 0) {
        ESP_LOGW(TAG, "Empty SSID, will start AP mode");
        return false;
    }
    
    // Configure WiFi
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, wifi_pass, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    
    g_retry_num = 0;
    g_wifi_mode = WIFI_RUN_MODE_STA_TRYING;
    
    ESP_LOGI(TAG, "Connecting to WiFi: %s", wifi_ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    return true;
}

bool wifi_manager_wait_for_connection(uint32_t timeout_ms) {
    if (!g_wifi_event_group) {
        ESP_LOGE(TAG, "WiFi manager not initialized");
        return false;
    }
    
    ESP_LOGI(TAG, "Waiting for WiFi connection (timeout: %ums)...", timeout_ms);
    
    EventBits_t bits = xEventGroupWaitBits(g_wifi_event_group,
                                          WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                          pdFALSE,
                                          pdFALSE,
                                          pdMS_TO_TICKS(timeout_ms));
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected! IP: %s", g_current_ip);
        return true;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "WiFi connection failed!");
        return false;
    } else {
        ESP_LOGE(TAG, "WiFi connection timeout!");
        return false;
    }
}

bool wifi_manager_start_ap_mode(void) {
    ESP_LOGI(TAG, "Starting APSTA mode for provisioning...");
    
    // 停止WiFi
    esp_wifi_stop();
    
    // 配置AP
    wifi_config_t ap_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .password = WIFI_AP_PASSWORD,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    // 使用APSTA模式，既能提供AP又能扫描WiFi
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    g_wifi_mode = WIFI_RUN_MODE_AP;
    
    // 获取AP IP
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(g_ap_netif, &ip_info);
    snprintf(g_current_ip, sizeof(g_current_ip), IPSTR, IP2STR(&ip_info.ip));
    
    ESP_LOGI(TAG, "APSTA mode started: SSID=%s, Password=%s, IP=%s", 
             WIFI_AP_SSID, WIFI_AP_PASSWORD, g_current_ip);
    
    return true;
}

void wifi_manager_stop_ap_mode(void) {
    ESP_LOGI(TAG, "Stopping AP mode...");
    esp_wifi_stop();
    g_wifi_mode = WIFI_RUN_MODE_NONE;
}

bool wifi_manager_save_credentials(const char* ssid, const char* password) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }
    
    err = nvs_set_str(handle, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        nvs_close(handle);
        return false;
    }
    
    err = nvs_set_str(handle, NVS_KEY_PASSWORD, password ? password : "");
    if (err != ESP_OK) {
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    ESP_LOGI(TAG, "WiFi credentials saved: %s", ssid);
    return err == ESP_OK;
}

wifi_run_mode_t wifi_manager_get_mode(void) {
    return g_wifi_mode;
}

bool wifi_manager_is_connected(void) {
    return g_wifi_mode == WIFI_RUN_MODE_STA_CONNECTED;
}

bool wifi_manager_get_ip(char* ip_str) {
    if (g_current_ip[0] != '\0') {
        strcpy(ip_str, g_current_ip);
        return true;
    }
    return false;
}

void wifi_manager_deinit(void) {
    if (g_wifi_event_group) {
        vEventGroupDelete(g_wifi_event_group);
        g_wifi_event_group = NULL;
    }
    
    esp_wifi_deinit();
    g_wifi_mode = WIFI_RUN_MODE_NONE;
    
    ESP_LOGI(TAG, "WiFi manager uninitialized");
}
