/**
 * @file ota_manager.c
 * @brief OTA升级管理器实现
 */

#include "ota_manager.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_system.h"
#include <string.h>

static const char* TAG = "OTA_MGR";

// OTA状态
static ota_state_t g_ota_state = OTA_STATE_IDLE;
static int g_ota_progress = 0;
static ota_progress_cb_t g_ota_callback = NULL;
static esp_ota_handle_t g_ota_handle = 0;
static const esp_partition_t* g_update_partition = NULL;

/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

static void set_ota_state(ota_state_t state, int progress, const char* message) {
    g_ota_state = state;
    g_ota_progress = progress;
    
    if (g_ota_callback) {
        g_ota_callback(state, progress, message);
    }
    
    ESP_LOGI(TAG, "OTA State: %d, Progress: %d%%, Message: %s", state, progress, message);
}

/* ============================================================================
 * 公共API实现
 * ============================================================================ */

midi_error_t ota_manager_init(void) {
    // 检查是否有待验证的OTA
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "OTA image pending verification");
        }
    }
    
    ESP_LOGI(TAG, "OTA manager initialized, running from %s", running->label);
    return MIDI_OK;
}

void ota_manager_deinit(void) {
    g_ota_state = OTA_STATE_IDLE;
    g_ota_progress = 0;
    g_ota_callback = NULL;
}

midi_error_t ota_manager_start_from_url(const char* url, ota_progress_cb_t callback) {
    if (!url) {
        return MIDI_ERR_INVALID_ARG;
    }
    
    if (g_ota_state != OTA_STATE_IDLE) {
        ESP_LOGW(TAG, "OTA already in progress");
        return MIDI_ERR_BUSY;
    }
    
    g_ota_callback = callback;
    set_ota_state(OTA_STATE_DOWNLOADING, 0, "Starting download");
    
    // 配置HTTPS OTA
    esp_http_client_config_t config = {
        .url = url,
        .cert_pem = NULL,  // 不验证证书
        .timeout_ms = 30000,
    };
    
    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };
    
    esp_err_t err = esp_https_ota(&ota_config);
    
    if (err == ESP_OK) {
        set_ota_state(OTA_STATE_SUCCESS, 100, "OTA successful, restarting...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
        return MIDI_OK;
    } else {
        set_ota_state(OTA_STATE_FAILED, 0, esp_err_to_name(err));
        return MIDI_ERR_UNKNOWN;
    }
}

midi_error_t ota_manager_start_from_data(const uint8_t* data, size_t len, ota_progress_cb_t callback) {
    if (!data || len == 0) {
        return MIDI_ERR_INVALID_ARG;
    }
    
    if (g_ota_state != OTA_STATE_IDLE) {
        ESP_LOGW(TAG, "OTA already in progress");
        return MIDI_ERR_BUSY;
    }
    
    g_ota_callback = callback;
    set_ota_state(OTA_STATE_WRITING, 0, "Starting OTA update");
    
    // 获取下一个OTA分区
    g_update_partition = esp_ota_get_next_update_partition(NULL);
    if (!g_update_partition) {
        set_ota_state(OTA_STATE_FAILED, 0, "No update partition found");
        return MIDI_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Writing to partition %s at offset 0x%lx", 
             g_update_partition->label, g_update_partition->address);
    
    // 开始OTA
    esp_err_t err = esp_ota_begin(g_update_partition, OTA_SIZE_UNKNOWN, &g_ota_handle);
    if (err != ESP_OK) {
        set_ota_state(OTA_STATE_FAILED, 0, "Failed to begin OTA");
        return MIDI_ERR_UNKNOWN;
    }
    
    // 写入数据
    size_t bytes_written = 0;
    const size_t chunk_size = 4096;
    
    while (bytes_written < len) {
        size_t to_write = (len - bytes_written) > chunk_size ? chunk_size : (len - bytes_written);
        
        err = esp_ota_write(g_ota_handle, data + bytes_written, to_write);
        if (err != ESP_OK) {
            esp_ota_abort(g_ota_handle);
            set_ota_state(OTA_STATE_FAILED, 0, "Failed to write OTA data");
            return MIDI_ERR_UNKNOWN;
        }
        
        bytes_written += to_write;
        int progress = (bytes_written * 100) / len;
        set_ota_state(OTA_STATE_WRITING, progress, "Writing firmware");
    }
    
    set_ota_state(OTA_STATE_VERIFYING, 100, "Verifying firmware");
    
    // 完成OTA
    err = esp_ota_end(g_ota_handle);
    if (err != ESP_OK) {
        set_ota_state(OTA_STATE_FAILED, 0, "Failed to end OTA");
        return MIDI_ERR_UNKNOWN;
    }
    
    // 设置启动分区
    err = esp_ota_set_boot_partition(g_update_partition);
    if (err != ESP_OK) {
        set_ota_state(OTA_STATE_FAILED, 0, "Failed to set boot partition");
        return MIDI_ERR_UNKNOWN;
    }
    
    set_ota_state(OTA_STATE_SUCCESS, 100, "OTA successful, restarting...");
    
    // 重启
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    
    return MIDI_OK;
}

ota_state_t ota_manager_get_state(void) {
    return g_ota_state;
}

const char* ota_manager_get_running_partition(void) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    return running ? running->label : "unknown";
}

midi_error_t ota_manager_mark_valid(void) {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Marked running app as valid");
        return MIDI_OK;
    }
    
    return MIDI_ERR_UNKNOWN;
}

midi_error_t ota_manager_rollback(void) {
    esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    
    if (err == ESP_OK) {
        return MIDI_OK;
    }
    
    return MIDI_ERR_UNKNOWN;
}

bool ota_manager_needs_validation(void) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        return ota_state == ESP_OTA_IMG_PENDING_VERIFY;
    }
    
    return false;
}

int ota_manager_get_progress(void) {
    return g_ota_progress;
}