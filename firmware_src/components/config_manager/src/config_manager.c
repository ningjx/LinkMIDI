/**
 * @file config_manager.c
 * @brief 配置管理器实现
 */

#include "config_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char* TAG = "CONFIG_MGR";

// NVS 命名空间
#define CONFIG_NAMESPACE "linkmidi_config"

// 配置版本号（用于兼容性检查）
#define CONFIG_VERSION 1

// NVS 键名
#define KEY_CONFIG_VERSION   "version"
#define KEY_IS_CONFIGURED    "configured"
#define KEY_WIFI_SSID        "wifi_ssid"
#define KEY_WIFI_PASSWORD    "wifi_pass"
#define KEY_WIFI_MAX_RETRY   "wifi_retry"
#define KEY_WIFI_AUTO_CONN   "wifi_auto"
#define KEY_MIDI_DEVICE_NAME "midi_name"
#define KEY_MIDI_PRODUCT_ID  "midi_pid"
#define KEY_MIDI_LISTEN_PORT "midi_port"
#define KEY_MIDI_DEVICE_MODE "midi_mode"
#define KEY_MIDI_DISCOVERY   "midi_disc"

// 全局配置缓存
static system_config_t g_config = {0};
static bool g_initialized = false;
static nvs_handle_t g_nvs_handle = 0;

/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

/**
 * @brief 从 NVS 读取字符串
 */
static esp_err_t read_string(nvs_handle_t handle, const char* key, char* value, size_t max_len) {
    size_t len = max_len;
    esp_err_t err = nvs_get_str(handle, key, value, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        value[0] = '\0';
        return ESP_OK;  // 未找到时返回空字符串
    }
    return err;
}

/**
 * @brief 从 NVS 读取 uint8
 */
static esp_err_t read_u8(nvs_handle_t handle, const char* key, uint8_t* value) {
    esp_err_t err = nvs_get_u8(handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *value = 0;
        return ESP_OK;
    }
    return err;
}

/**
 * @brief 从 NVS 读取 uint16
 */
static esp_err_t read_u16(nvs_handle_t handle, const char* key, uint16_t* value) {
    esp_err_t err = nvs_get_u16(handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *value = 0;
        return ESP_OK;
    }
    return err;
}

/**
 * @brief 从 NVS 读取 uint32
 */
static esp_err_t read_u32(nvs_handle_t handle, const char* key, uint32_t* value) {
    esp_err_t err = nvs_get_u32(handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *value = 0;
        return ESP_OK;
    }
    return err;
}

/* ============================================================================
 * 公共 API 实现
 * ============================================================================ */

midi_error_t config_manager_init(void) {
    if (g_initialized) {
        ESP_LOGW(TAG, "Config manager already initialized");
        return MIDI_OK;
    }
    
    // 打开 NVS 命名空间
    esp_err_t err = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &g_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return MIDI_ERR_STORAGE_ERROR;
    }
    
    // 加载配置到缓存
    midi_error_t ret = config_manager_load(&g_config);
    if (ret != MIDI_OK) {
        ESP_LOGW(TAG, "Failed to load config, using defaults");
        config_manager_get_defaults(&g_config);
    }
    
    g_initialized = true;
    ESP_LOGI(TAG, "Config manager initialized");
    return MIDI_OK;
}

void config_manager_deinit(void) {
    if (!g_initialized) return;
    
    if (g_nvs_handle) {
        nvs_close(g_nvs_handle);
        g_nvs_handle = 0;
    }
    
    g_initialized = false;
    ESP_LOGI(TAG, "Config manager deinitialized");
}

midi_error_t config_manager_load(system_config_t* config) {
    if (!config) return MIDI_ERR_INVALID_ARG;
    if (!g_nvs_handle) return MIDI_ERR_NOT_INITIALIZED;
    
    esp_err_t err;
    
    // 读取配置版本
    err = read_u32(g_nvs_handle, KEY_CONFIG_VERSION, &config->config_version);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read config version: %s", esp_err_to_name(err));
        return MIDI_ERR_STORAGE_ERROR;
    }
    
    // 检查版本兼容性
    if (config->config_version != CONFIG_VERSION) {
        ESP_LOGW(TAG, "Config version mismatch (stored: %d, current: %d), using defaults",
                 config->config_version, CONFIG_VERSION);
        config_manager_get_defaults(config);
        return MIDI_OK;
    }
    
    // 读取配置状态
    uint8_t is_configured = 0;
    err = read_u8(g_nvs_handle, KEY_IS_CONFIGURED, &is_configured);
    config->is_configured = (is_configured != 0);
    
    // 读取 WiFi 配置
    err = read_string(g_nvs_handle, KEY_WIFI_SSID, config->wifi.ssid, sizeof(config->wifi.ssid));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WiFi SSID: %s", esp_err_to_name(err));
        return MIDI_ERR_STORAGE_ERROR;
    }
    
    err = read_string(g_nvs_handle, KEY_WIFI_PASSWORD, config->wifi.password, sizeof(config->wifi.password));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WiFi password: %s", esp_err_to_name(err));
        return MIDI_ERR_STORAGE_ERROR;
    }
    
    err = read_u8(g_nvs_handle, KEY_WIFI_MAX_RETRY, &config->wifi.max_retry);
    err = read_u8(g_nvs_handle, KEY_WIFI_AUTO_CONN, (uint8_t*)&config->wifi.auto_connect);
    
    // 读取 MIDI 配置
    err = read_string(g_nvs_handle, KEY_MIDI_DEVICE_NAME, config->midi.device_name, sizeof(config->midi.device_name));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MIDI device name: %s", esp_err_to_name(err));
        return MIDI_ERR_STORAGE_ERROR;
    }
    
    err = read_string(g_nvs_handle, KEY_MIDI_PRODUCT_ID, config->midi.product_id, sizeof(config->midi.product_id));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MIDI product ID: %s", esp_err_to_name(err));
        return MIDI_ERR_STORAGE_ERROR;
    }
    
    err = read_u16(g_nvs_handle, KEY_MIDI_LISTEN_PORT, &config->midi.listen_port);
    err = read_u8(g_nvs_handle, KEY_MIDI_DEVICE_MODE, &config->midi.device_mode);
    err = read_u8(g_nvs_handle, KEY_MIDI_DISCOVERY, (uint8_t*)&config->midi.enable_discovery);
    
    ESP_LOGI(TAG, "Configuration loaded successfully");
    ESP_LOGI(TAG, "  WiFi SSID: %s", config->wifi.ssid);
    ESP_LOGI(TAG, "  Device Name: %s", config->midi.device_name);
    ESP_LOGI(TAG, "  Listen Port: %d", config->midi.listen_port);
    
    return MIDI_OK;
}

midi_error_t config_manager_save(const system_config_t* config) {
    if (!config) return MIDI_ERR_INVALID_ARG;
    if (!g_nvs_handle) return MIDI_ERR_NOT_INITIALIZED;
    
    esp_err_t err;
    
    // 保存配置版本
    err = nvs_set_u32(g_nvs_handle, KEY_CONFIG_VERSION, CONFIG_VERSION);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save config version: %s", esp_err_to_name(err));
        return MIDI_ERR_STORAGE_ERROR;
    }
    
    // 保存配置状态
    err = nvs_set_u8(g_nvs_handle, KEY_IS_CONFIGURED, config->is_configured ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save configured flag: %s", esp_err_to_name(err));
        return MIDI_ERR_STORAGE_ERROR;
    }
    
    // 保存 WiFi 配置
    err = nvs_set_str(g_nvs_handle, KEY_WIFI_SSID, config->wifi.ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save WiFi SSID: %s", esp_err_to_name(err));
        return MIDI_ERR_STORAGE_ERROR;
    }
    
    err = nvs_set_str(g_nvs_handle, KEY_WIFI_PASSWORD, config->wifi.password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save WiFi password: %s", esp_err_to_name(err));
        return MIDI_ERR_STORAGE_ERROR;
    }
    
    err = nvs_set_u8(g_nvs_handle, KEY_WIFI_MAX_RETRY, config->wifi.max_retry);
    err = nvs_set_u8(g_nvs_handle, KEY_WIFI_AUTO_CONN, config->wifi.auto_connect ? 1 : 0);
    
    // 保存 MIDI 配置
    err = nvs_set_str(g_nvs_handle, KEY_MIDI_DEVICE_NAME, config->midi.device_name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save MIDI device name: %s", esp_err_to_name(err));
        return MIDI_ERR_STORAGE_ERROR;
    }
    
    err = nvs_set_str(g_nvs_handle, KEY_MIDI_PRODUCT_ID, config->midi.product_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save MIDI product ID: %s", esp_err_to_name(err));
        return MIDI_ERR_STORAGE_ERROR;
    }
    
    err = nvs_set_u16(g_nvs_handle, KEY_MIDI_LISTEN_PORT, config->midi.listen_port);
    err = nvs_set_u8(g_nvs_handle, KEY_MIDI_DEVICE_MODE, config->midi.device_mode);
    err = nvs_set_u8(g_nvs_handle, KEY_MIDI_DISCOVERY, config->midi.enable_discovery ? 1 : 0);
    
    // 提交更改
    err = nvs_commit(g_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit config: %s", esp_err_to_name(err));
        return MIDI_ERR_STORAGE_ERROR;
    }
    
    // 更新缓存
    memcpy(&g_config, config, sizeof(system_config_t));
    
    ESP_LOGI(TAG, "Configuration saved successfully");
    return MIDI_OK;
}

void config_manager_get_defaults(system_config_t* config) {
    if (!config) return;
    
    memset(config, 0, sizeof(system_config_t));
    
    // WiFi 默认配置
    strncpy(config->wifi.ssid, CONFIG_WIFI_SSID, sizeof(config->wifi.ssid) - 1);
    strncpy(config->wifi.password, CONFIG_WIFI_PASSWORD, sizeof(config->wifi.password) - 1);
    config->wifi.max_retry = CONFIG_WIFI_MAXIMUM_RETRY;
    config->wifi.auto_connect = true;
    
    // MIDI 默认配置
    strncpy(config->midi.device_name, CONFIG_MIDI_DEVICE_NAME, sizeof(config->midi.device_name) - 1);
    strncpy(config->midi.product_id, CONFIG_MIDI_PRODUCT_ID, sizeof(config->midi.product_id) - 1);
    config->midi.listen_port = CONFIG_MIDI_LISTEN_PORT;
    config->midi.device_mode = 1;  // Server mode
    config->midi.enable_discovery = true;
    
    // 配置版本
    config->config_version = CONFIG_VERSION;
    config->is_configured = false;
    
    ESP_LOGI(TAG, "Default configuration loaded");
}

midi_error_t config_manager_factory_reset(void) {
    if (!g_nvs_handle) return MIDI_ERR_NOT_INITIALIZED;
    
    esp_err_t err = nvs_erase_all(g_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(err));
        return MIDI_ERR_STORAGE_ERROR;
    }
    
    err = nvs_commit(g_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit erase: %s", esp_err_to_name(err));
        return MIDI_ERR_STORAGE_ERROR;
    }
    
    // 重置为默认配置
    config_manager_get_defaults(&g_config);
    
    ESP_LOGI(TAG, "Factory reset completed");
    return MIDI_OK;
}

bool config_manager_is_configured(void) {
    return g_config.is_configured;
}

midi_error_t config_manager_update_wifi(const wifi_config_data_t* wifi_config) {
    if (!wifi_config) return MIDI_ERR_INVALID_ARG;
    
    system_config_t config;
    memcpy(&config, &g_config, sizeof(config));
    memcpy(&config.wifi, wifi_config, sizeof(config.wifi));
    config.is_configured = true;
    
    return config_manager_save(&config);
}

midi_error_t config_manager_update_midi(const midi_config_data_t* midi_config) {
    if (!midi_config) return MIDI_ERR_INVALID_ARG;
    
    system_config_t config;
    memcpy(&config, &g_config, sizeof(config));
    memcpy(&config.midi, midi_config, sizeof(config.midi));
    config.is_configured = true;
    
    return config_manager_save(&config);
}

const system_config_t* config_manager_get_current(void) {
    return &g_config;
}