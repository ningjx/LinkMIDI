#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char* TAG = "WIFI_MGR";

/* FreeRTOS event group for WiFi events */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t g_wifi_event_group = NULL;
static int g_retry_num = 0;

/**
 * @brief WiFi event handler
 */
static void event_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (g_retry_num < CONFIG_WIFI_MAXIMUM_RETRY) {
            ESP_LOGI(TAG, "WiFi disconnected, retrying (attempt %d/%d)",
                    g_retry_num + 1, CONFIG_WIFI_MAXIMUM_RETRY);
            esp_wifi_connect();
            g_retry_num++;
        } else {
            ESP_LOGW(TAG, "WiFi connection failed after %d attempts", CONFIG_WIFI_MAXIMUM_RETRY);
            xEventGroupSetBits(g_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        g_retry_num = 0;
        xEventGroupSetBits(g_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

bool wifi_manager_init(void) {
    // Initialize NVS (Non-Volatile Storage)
    ESP_LOGI(TAG, "Initializing NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        ESP_LOGW(TAG, "Erasing NVS partition...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "NVS initialized successfully");
    
    // Initialize network stack
    ESP_LOGI(TAG, "Initializing network stack...");
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default event loop
    ESP_LOGI(TAG, "Creating default event loop...");
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create default WiFi STA interface
    esp_netif_create_default_wifi_sta();
    
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
    
    ESP_LOGI(TAG, "WiFi manager initialized successfully");
    return true;
}

bool wifi_manager_connect(void) {
    if (!g_wifi_event_group) {
        ESP_LOGE(TAG, "WiFi manager not initialized");
        return false;
    }
    
    // Configure WiFi with credentials from Kconfig
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        #if CONFIG_WIFI_CONNECT_AP_BY_SIGNAL
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
        #else
            .sort_method = WIFI_CONNECT_AP_BY_SECURITY,
        #endif
            .scan_method = CONFIG_WIFI_SCAN_METHOD ? WIFI_ALL_CHANNEL_SCAN : WIFI_FAST_SCAN,
        },
    };
    
    g_retry_num = 0;
    
    ESP_LOGI(TAG, "Configuring WiFi with SSID: %s", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    ESP_LOGI(TAG, "Starting WiFi...");
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
        ESP_LOGI(TAG, "WiFi connected!");
        return true;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "WiFi connection failed!");
        return false;
    } else {
        ESP_LOGE(TAG, "WiFi connection timeout!");
        return false;
    }
}

void wifi_manager_deinit(void) {
    if (g_wifi_event_group) {
        vEventGroupDelete(g_wifi_event_group);
        g_wifi_event_group = NULL;
    }
    
    esp_wifi_deinit();
    esp_netif_deinit();
    esp_event_loop_delete_default();
    
    ESP_LOGI(TAG, "WiFi manager uninitialized");
}

bool wifi_manager_is_connected(void) {
    if (!g_wifi_event_group) {
        return false;
    }
    
    EventBits_t bits = xEventGroupGetBits(g_wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}
