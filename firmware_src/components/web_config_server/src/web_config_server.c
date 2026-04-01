/**
 * @file web_config_server.c
 * @brief Web配置服务器实现
 */

#include "web_config_server.h"
#include "config_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "lwip/inet.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "WEB_CONFIG";

// HTTP服务器句柄
static httpd_handle_t g_server = NULL;
static bool g_running = false;
static uint16_t g_port = 80;

// SoftAP配置
#define AP_SSID "LinkMIDI-Setup"
#define AP_PASSWORD "12345678"
#define AP_MAX_CONN 4

/* ============================================================================
 * HTML页面
 * ============================================================================ */

static const char* html_index = 
"<!DOCTYPE html>"
"<html lang='zh-CN'>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>LinkMIDI 配置</title>"
"<style>"
"body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }"
".container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }"
"h1 { color: #333; text-align: center; }"
".section { margin: 20px 0; padding: 15px; background: #f9f9f9; border-radius: 5px; }"
"label { display: block; margin: 10px 0 5px; font-weight: bold; }"
"input, select { width: 100%; padding: 10px; margin: 5px 0; border: 1px solid #ddd; border-radius: 5px; box-sizing: border-box; }"
"button { background: #4CAF50; color: white; padding: 12px 24px; border: none; border-radius: 5px; cursor: pointer; width: 100%; margin: 10px 0; font-size: 16px; }"
"button:hover { background: #45a049; }"
"button.danger { background: #f44336; }"
"button.danger:hover { background: #da190b; }"
"#status { margin: 10px 0; padding: 10px; border-radius: 5px; display: none; }"
".success { background: #d4edda; color: #155724; }"
".error { background: #f8d7da; color: #721c24; }"
"</style>"
"</head>"
"<body>"
"<div class='container'>"
"<h1>🎹 LinkMIDI 配置</h1>"
"<div id='status'></div>"

"<div class='section'>"
"<h2>WiFi 配置</h2>"
"<label for='ssid'>网络名称 (SSID):</label>"
"<select id='ssid' onchange='updateSSID()'><option value=''>请先扫描...</option></select>"
"<button onclick='scanWiFi()' style='margin-bottom:10px;'>🔍 扫描WiFi</button>"
"<label for='password'>密码:</label>"
"<input type='password' id='password' placeholder='WiFi密码'>"
"<button onclick='connectWiFi()'>连接 WiFi</button>"
"</div>"

"<div class='section'>"
"<h2>设备配置</h2>"
"<label for='device_name'>设备名称:</label>"
"<input type='text' id='device_name' placeholder='LinkMIDI-Device'>"
"<label for='listen_port'>监听端口:</label>"
"<input type='number' id='listen_port' value='5506' min='1024' max='65535'>"
"<button onclick='saveConfig()'>保存配置</button>"
"</div>"

"<div class='section'>"
"<h2>系统管理</h2>"
"<button onclick='getInfo()'>系统信息</button>"
"<button onclick='restart()'>重启设备</button>"
"<button class='danger' onclick='factoryReset()'>恢复出厂设置</button>"
"</div>"

"<div class='section'>"
"<h2>固件升级</h2>"
"<label for='firmware'>选择固件文件:</label>"
"<input type='file' id='firmware' accept='.bin'>"
"<button onclick='uploadFirmware()'>上传并升级</button>"
"<div id='ota_status' style='margin-top:10px;'></div>"
"</div>"

"</div>"

"<script>"
"function showStatus(msg, isError) {"
"var s = document.getElementById('status');"
"s.textContent = msg;"
"s.className = isError ? 'error' : 'success';"
"s.style.display = 'block';"
"setTimeout(() => s.style.display = 'none', 5000);"
"}"

"function scanWiFi() {"
"fetch('/api/wifi/scan')"
".then(r => r.json())"
".then(data => {"
"var sel = document.getElementById('ssid');"
"sel.innerHTML = '<option value=\"\">选择网络...</option>';"
"data.forEach(net => {"
"var opt = document.createElement('option');"
"opt.value = net.ssid;"
"opt.textContent = net.ssid + ' (' + net.rssi + ' dBm)';"
"sel.appendChild(opt);"
"});"
"})"
".catch(err => showStatus('扫描失败: ' + err, true));"
"}"

"function updateSSID() {"
"var ssid = document.getElementById('ssid').value;"
"if (ssid) document.getElementById('password').focus();"
"}"

"function connectWiFi() {"
"var ssid = document.getElementById('ssid').value;"
"var pass = document.getElementById('password').value;"
"if (!ssid) { showStatus('请选择WiFi网络', true); return; }"
"fetch('/api/wifi/connect', {"
"method: 'POST',"
"headers: {'Content-Type': 'application/json'},"
"body: JSON.stringify({ssid: ssid, password: pass})"
"})"
".then(r => r.json())"
".then(data => {"
"if (data.success) {"
"showStatus('WiFi连接成功！IP: ' + data.ip, false);"
"setTimeout(() => location.reload(), 3000);"
"} else {"
"showStatus('连接失败: ' + data.message, true);"
"}"
"})"
".catch(err => showStatus('请求失败: ' + err, true));"
"}"

"function loadConfig() {"
"fetch('/api/config')"
".then(r => r.json())"
".then(data => {"
"document.getElementById('device_name').value = data.device_name || '';"
"document.getElementById('listen_port').value = data.listen_port || 5506;"
"})"
".catch(err => console.error('加载配置失败:', err));"
"}"

"function saveConfig() {"
"var config = {"
"device_name: document.getElementById('device_name').value,"
"listen_port: parseInt(document.getElementById('listen_port').value)"
"};"
"fetch('/api/config', {"
"method: 'POST',"
"headers: {'Content-Type': 'application/json'},"
"body: JSON.stringify(config)"
"})"
".then(r => r.json())"
".then(data => {"
"if (data.success) showStatus('配置已保存', false);"
"else showStatus('保存失败: ' + data.message, true);"
"})"
".catch(err => showStatus('请求失败: ' + err, true));"
"}"

"function getInfo() {"
"fetch('/api/system/info')"
".then(r => r.json())"
".then(data => {"
"alert('设备: ' + data.device_name + '\\n'"
"+ '固件版本: ' + data.version + '\\n'"
"+ 'IP地址: ' + data.ip + '\\n'"
"+ '运行分区: ' + data.partition + '\\n'"
"+ '可用内存: ' + data.free_heap + ' bytes');"
"})"
".catch(err => showStatus('获取信息失败: ' + err, true));"
"}"

"function restart() {"
"if (confirm('确定要重启设备吗？')) {"
"fetch('/api/system/restart', {method: 'POST'})"
".then(() => showStatus('设备正在重启...', false));"
"}"
"}"

"function factoryReset() {"
"if (confirm('确定要恢复出厂设置吗？所有配置将被清除！')) {"
"fetch('/api/system/factory_reset', {method: 'POST'})"
".then(() => showStatus('恢复出厂设置完成，设备将重启...', false));"
"}"
"}"

"function uploadFirmware() {"
"var file = document.getElementById('firmware').files[0];"
"if (!file) { showStatus('请选择固件文件', true); return; }"
"var status = document.getElementById('ota_status');"
"status.textContent = '上传中...';"
"var formData = new FormData();"
"formData.append('firmware', file);"
"fetch('/api/ota/upload', {"
"method: 'POST',"
"body: formData"
"})"
".then(r => r.json())"
".then(data => {"
"if (data.success) {"
"status.textContent = '升级成功！设备将重启...';"
"setTimeout(() => location.reload(), 5000);"
"} else {"
"status.textContent = '升级失败: ' + data.message;"
"}"
"})"
".catch(err => status.textContent = '上传失败: ' + err);"
"}"

"// 页面加载时初始化"
"window.onload = function() {"
"scanWiFi();"
"loadConfig();"
"};"
"</script>"
"</body>"
"</html>";

/* ============================================================================
 * HTTP处理函数
 * ============================================================================ */

static esp_err_t http_get_index(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_index, strlen(html_index));
    return ESP_OK;
}

static esp_err_t http_get_wifi_scan(httpd_req_t *req) {
    ESP_LOGI(TAG, "WiFi scan requested");
    
    // APSTA模式下的WiFi扫描配置
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,  // 扫描所有信道
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 100,  // 最小扫描时间100ms
                .max = 300,  // 最大扫描时间300ms
            },
        },
    };
    
    // 使用阻塞扫描，但时间较短
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Scan failed: %s", esp_err_to_name(err));
        cJSON *root = cJSON_CreateArray();
        char *json_str = cJSON_Print(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_str, strlen(json_str));
        free(json_str);
        cJSON_Delete(root);
        return ESP_OK;
    }
    
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "Found %d APs", ap_count);
    
    if (ap_count == 0) {
        cJSON *root = cJSON_CreateArray();
        char *json_str = cJSON_Print(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_str, strlen(json_str));
        free(json_str);
        cJSON_Delete(root);
        return ESP_OK;
    }
    
    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_list) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory error");
        return ESP_FAIL;
    }
    
    esp_wifi_scan_get_ap_records(&ap_count, ap_list);
    
    // 构建JSON响应
    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < ap_count; i++) {
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", (char*)ap_list[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", ap_list[i].rssi);
        cJSON_AddItemToArray(root, ap);
        ESP_LOGI(TAG, "  AP[%d]: %s (rssi=%d)", i, ap_list[i].ssid, ap_list[i].rssi);
    }
    
    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(root);
    free(ap_list);
    
    ESP_LOGI(TAG, "WiFi scan completed, sent %d APs", ap_count);
    return ESP_OK;
}

static esp_err_t http_post_wifi_connect(httpd_req_t *req) {
    ESP_LOGI(TAG, "WiFi connect request received");
    
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    ESP_LOGI(TAG, "Request data: %s", buf);
    
    // 解析JSON
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGE(TAG, "Invalid JSON");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *ssid_json = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass_json = cJSON_GetObjectItem(root, "password");
    
    if (!ssid_json || !cJSON_IsString(ssid_json)) {
        ESP_LOGE(TAG, "Missing SSID");
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
        return ESP_FAIL;
    }
    
    const char* ssid = ssid_json->valuestring;
    const char* password = (pass_json && cJSON_IsString(pass_json)) ? pass_json->valuestring : "";
    
    ESP_LOGI(TAG, "Connecting to WiFi: SSID=%s, Password=%s", ssid, password);
    
    // 保存WiFi凭据到NVS
    nvs_handle_t handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &handle);
    
    cJSON *resp = cJSON_CreateObject();
    if (err == ESP_OK) {
        nvs_set_str(handle, "ssid", ssid);
        nvs_set_str(handle, "password", password);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "WiFi credentials saved to NVS");
        
        // 同时更新config_manager
        wifi_config_data_t wifi_config = {0};
        strncpy(wifi_config.ssid, ssid, sizeof(wifi_config.ssid) - 1);
        strncpy(wifi_config.password, password, sizeof(wifi_config.password) - 1);
        wifi_config.auto_connect = true;
        config_manager_update_wifi(&wifi_config);
        ESP_LOGI(TAG, "WiFi config updated in config_manager");
        
        // 立即尝试连接WiFi（从APSTA切换到STA模式）
        ESP_LOGI(TAG, "Attempting to connect to WiFi...");
        
        // 停止当前WiFi
        esp_wifi_stop();
        
        // 配置STA模式
        wifi_config_t sta_config = {0};
        strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
        strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);
        sta_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        sta_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        
        // 切换到STA模式并连接
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        esp_wifi_start();
        esp_wifi_connect();
        
        ESP_LOGI(TAG, "WiFi connection initiated, waiting for result...");
        
        // 等待连接结果（最多10秒）
        int wait_count = 0;
        bool connected = false;
        char ip_str[16] = "unknown";
        
        while (wait_count < 100) {
            vTaskDelay(pdMS_TO_TICKS(100));
            
            // 检查是否已连接
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                connected = true;
                ESP_LOGI(TAG, "WiFi connected successfully!");
                
                // 获取IP地址
                esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                if (netif) {
                    esp_netif_ip_info_t ip_info;
                    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
                        ESP_LOGI(TAG, "Got IP: %s", ip_str);
                    }
                }
                break;
            }
            wait_count++;
        }
        
        if (connected) {
            cJSON_AddBoolToObject(resp, "success", true);
            cJSON_AddStringToObject(resp, "message", "WiFi连接成功！");
            cJSON_AddStringToObject(resp, "ip", ip_str);
            ESP_LOGI(TAG, "WiFi connect success, IP=%s", ip_str);
        } else {
            cJSON_AddBoolToObject(resp, "success", false);
            cJSON_AddStringToObject(resp, "message", "WiFi连接失败，请检查密码");
            ESP_LOGW(TAG, "WiFi connect failed after 10s timeout");
            
            // 连接失败，恢复APSTA模式
            esp_wifi_stop();
            esp_wifi_set_mode(WIFI_MODE_APSTA);
            wifi_config_t ap_config = {
                .ap = {
                    .ssid = "LinkMidi",
                    .ssid_len = strlen("LinkMidi"),
                    .channel = 1,
                    .password = "linkmidi",
                    .max_connection = 4,
                    .authmode = WIFI_AUTH_WPA2_PSK,
                },
            };
            esp_wifi_set_config(WIFI_IF_AP, &ap_config);
            esp_wifi_start();
            ESP_LOGI(TAG, "Restored APSTA mode for provisioning");
        }
    } else {
        cJSON_AddBoolToObject(resp, "success", false);
        cJSON_AddStringToObject(resp, "message", "保存配置失败");
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    }
    
    char *json_str = cJSON_Print(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(resp);
    cJSON_Delete(root);
    
    return ESP_OK;
}

static esp_err_t http_get_config(httpd_req_t *req) {
    const system_config_t* config = config_manager_get_current();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_name", config->midi.device_name);
    cJSON_AddNumberToObject(root, "listen_port", config->midi.listen_port);
    
    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(root);
    
    return ESP_OK;
}

static esp_err_t http_post_config(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    midi_config_data_t midi_config = {0};
    cJSON *name_json = cJSON_GetObjectItem(root, "device_name");
    cJSON *port_json = cJSON_GetObjectItem(root, "listen_port");
    
    if (name_json && cJSON_IsString(name_json)) {
        strncpy(midi_config.device_name, name_json->valuestring, sizeof(midi_config.device_name) - 1);
    }
    if (port_json && cJSON_IsNumber(port_json)) {
        midi_config.listen_port = port_json->valueint;
    }
    
    midi_error_t err = config_manager_update_midi(&midi_config);
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", err == MIDI_OK);
    if (err != MIDI_OK) {
        cJSON_AddStringToObject(resp, "message", "保存失败");
    }
    
    char *json_str = cJSON_Print(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(resp);
    cJSON_Delete(root);
    
    return ESP_OK;
}

static esp_err_t http_get_system_info(httpd_req_t *req) {
    const system_config_t* config = config_manager_get_current();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_name", config->midi.device_name);
    cJSON_AddStringToObject(root, "version", "1.0.0");
    
    // 获取IP地址
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(netif, &ip_info);
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
        cJSON_AddStringToObject(root, "ip", ip_str);
    }
    
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddStringToObject(root, "partition", "factory");
    
    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(root);
    
    return ESP_OK;
}

static esp_err_t http_post_restart(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    
    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(root);
    
    // 延迟重启
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    
    return ESP_OK;
}

static esp_err_t http_post_factory_reset(httpd_req_t *req) {
    midi_error_t err = config_manager_factory_reset();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", err == MIDI_OK);
    
    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(root);
    
    if (err == MIDI_OK) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    
    return ESP_OK;
}

/* ============================================================================
 * 公共API实现
 * ============================================================================ */

midi_error_t web_config_server_init(const web_server_config_t* config) {
    if (g_server) {
        return MIDI_ERR_ALREADY_INITIALIZED;
    }
    
    if (config) {
        g_port = config->port;
    }
    
    ESP_LOGI(TAG, "Web config server initialized on port %d", g_port);
    return MIDI_OK;
}

midi_error_t web_config_server_start(void) {
    if (g_running) {
        return MIDI_ERR_ALREADY_INITIALIZED;
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = g_port;
    config.max_uri_handlers = 10;
    
    esp_err_t err = httpd_start(&g_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return MIDI_ERR_NET_INIT_FAILED;
    }
    
    // 注册URI处理函数
    httpd_uri_t uri_index = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = http_get_index,
    };
    httpd_register_uri_handler(g_server, &uri_index);
    
    httpd_uri_t uri_wifi_scan = {
        .uri = "/api/wifi/scan",
        .method = HTTP_GET,
        .handler = http_get_wifi_scan,
    };
    httpd_register_uri_handler(g_server, &uri_wifi_scan);
    
    httpd_uri_t uri_wifi_connect = {
        .uri = "/api/wifi/connect",
        .method = HTTP_POST,
        .handler = http_post_wifi_connect,
    };
    httpd_register_uri_handler(g_server, &uri_wifi_connect);
    
    httpd_uri_t uri_get_config = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = http_get_config,
    };
    httpd_register_uri_handler(g_server, &uri_get_config);
    
    httpd_uri_t uri_post_config = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = http_post_config,
    };
    httpd_register_uri_handler(g_server, &uri_post_config);
    
    httpd_uri_t uri_system_info = {
        .uri = "/api/system/info",
        .method = HTTP_GET,
        .handler = http_get_system_info,
    };
    httpd_register_uri_handler(g_server, &uri_system_info);
    
    httpd_uri_t uri_restart = {
        .uri = "/api/system/restart",
        .method = HTTP_POST,
        .handler = http_post_restart,
    };
    httpd_register_uri_handler(g_server, &uri_restart);
    
    httpd_uri_t uri_factory_reset = {
        .uri = "/api/system/factory_reset",
        .method = HTTP_POST,
        .handler = http_post_factory_reset,
    };
    httpd_register_uri_handler(g_server, &uri_factory_reset);
    
    g_running = true;
    ESP_LOGI(TAG, "Web server started on port %d", g_port);
    
    return MIDI_OK;
}

midi_error_t web_config_server_stop(void) {
    if (!g_running || !g_server) {
        return MIDI_ERR_NOT_INITIALIZED;
    }
    
    httpd_stop(g_server);
    g_server = NULL;
    g_running = false;
    
    ESP_LOGI(TAG, "Web server stopped");
    return MIDI_OK;
}

void web_config_server_deinit(void) {
    web_config_server_stop();
    g_port = 80;
}

bool web_config_server_is_running(void) {
    return g_running;
}

midi_error_t web_config_start_ap_mode(const char* ssid, const char* password) {
    // 配置SoftAP
    wifi_config_t ap_config = {0};
    strncpy((char*)ap_config.ap.ssid, ssid ? ssid : AP_SSID, sizeof(ap_config.ap.ssid) - 1);
    strncpy((char*)ap_config.ap.password, password ? password : AP_PASSWORD, sizeof(ap_config.ap.password) - 1);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = AP_MAX_CONN;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP mode: %s", esp_err_to_name(err));
        return MIDI_ERR_NET_INIT_FAILED;
    }
    
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(err));
        return MIDI_ERR_NET_INIT_FAILED;
    }
    
    ESP_LOGI(TAG, "SoftAP started: SSID=%s", ap_config.ap.ssid);
    return MIDI_OK;
}

midi_error_t web_config_stop_ap_mode(void) {
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop AP mode: %s", esp_err_to_name(err));
        return MIDI_ERR_NET_INIT_FAILED;
    }
    
    ESP_LOGI(TAG, "SoftAP stopped");
    return MIDI_OK;
}