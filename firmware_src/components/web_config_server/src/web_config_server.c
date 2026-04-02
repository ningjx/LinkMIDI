/**
 * @file web_config_server.c
 * @brief Web配置服务器实现
 */

#include "web_config_server.h"
#include "config_manager.h"
// WiFi status is obtained via callbacks, not direct include
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

// 状态回调函数
static web_status_callbacks_t g_status_callbacks = {0};

// SoftAP配置
#define AP_SSID "LinkMIDI-Setup"
#define AP_PASSWORD "12345678"
#define AP_MAX_CONN 4

/* ============================================================================
 * HTML页面
 * ============================================================================ */

static const char* html_index = 
"<!DOCTYPE html>"
"<html><head>"
"<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>LinkMIDI</title>"
"<style>"
":root{--p:#4CAF50;--d:#f44336;--bg:#f5f5f5;--c:#fff;--t:#333;--b:#e0e0e0}"
"*{box-sizing:border-box}body{margin:0;font-family:sans-serif;background:var(--bg);color:var(--t)}"
".container{max-width:800px;margin:0 auto;padding:20px}"
"h1{text-align:center;margin:0 0 20px}"
".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:15px;margin:20px 0}"
".card{background:var(--c);border-radius:8px;padding:15px;border-left:4px solid var(--b);box-shadow:0 2px 6px #0001}"
".card.on{border-left-color:var(--p)}"
".card h3{margin:5px 0;font-size:13px;font-weight:500;color:#666}"
".card .st{font-size:18px;font-weight:600;margin:8px 0}"
".card .dt{font-size:12px;color:#888}"
".panel{background:var(--c);border-radius:8px;padding:20px;margin:20px 0;box-shadow:0 2px 6px #0001}"
".panel h2{margin:0 0 15px;font-size:16px;border-bottom:1px solid var(--b);padding-bottom:10px}"
".fg{margin:12px 0}"
".fg label{display:block;margin-bottom:5px;font-weight:500;font-size:13px}"
".fg input,.fg select{width:100%;padding:10px;border:1px solid var(--b);border-radius:6px;font-size:14px}"
".btn{background:var(--p);color:#fff;border:0;padding:10px 20px;border-radius:6px;cursor:pointer;font-size:14px;width:100%;margin:10px 0}"
".btn:hover{background:#388E3C}"
".btn.danger{background:var(--d)}"
".btn.danger:hover{background:#D32F2F}"
".btn:disabled{background:#ccc;cursor:not-allowed}"
".test-btn{padding:20px 40px;font-size:16px;margin:20px 0}"
".msg{position:fixed;bottom:20px;left:50%;transform:translateX(-50%);padding:12px 24px;background:#f44336;color:#fff;border-radius:6px;opacity:0;transition:opacity 0.3s;pointer-events:none}"
".msg.ok{background:var(--p)}"
".msg.show{opacity:1}"
"@media(max-width:600px){.container{padding:10px}}"
"</style>"
"</head><body>"
"<div class='container'>"
"<h1>🎹 LinkMIDI</h1>"
"<div class='grid'>"
"<div class='card' id='w'><h3>📶 WiFi</h3><div class='st' id='ws'>未连接</div><div class='dt' id='wd'>-</div></div>"
"<div class='card' id='u'><h3>🔌 USB</h3><div class='st' id='us'>未连接</div><div class='dt' id='ud'>-</div></div>"
"<div class='card' id='n'><h3>🌐 Network</h3><div class='st' id='ns'>未连接</div><div class='dt' id='nd'>-</div></div>"
"</div>"
"<div class='panel'><h3 style='margin:0'>🎵 测试音符</h3>"
"<button class='btn test-btn' id='tb' onclick='testNote()' disabled>发送 A4</button>"
"<div id='tn' style='font-size:12px;color:#666;text-align:center'>需要 Network MIDI 连接</div>"
"</div>"
"<div class='panel'><h2>📶 WiFi</h2>"
"<div class='fg'><label>SSID</label><select id='ssid'><option>扫描...</option></select></div>"
"<div class='fg'><label>密码</label><input type='password' id='pwd'></div>"
"<button class='btn' onclick='connectWifi()'>连接</button>"
"</div>"
"<div class='panel'><h2>⚙️ 配置</h2>"
"<div class='fg'><label>设备名</label><input type='text' id='dn'></div>"
"<div class='fg'><label>端口</label><input type='number' id='port' min='1024' max='65535'></div>"
"<button class='btn' onclick='save()'>保存</button>"
"</div>"
"<div class='panel'><h2>🔧 系统</h2>"
"<button class='btn' onclick='info()'>信息</button>"
"<button class='btn' onclick='restart()'>重启</button>"
"<button class='btn danger' onclick='reset()'>恢复</button>"
"</div>"
"</div>"
"<div class='msg' id='msg'></div>"
"<script>"
"let testOn=false,lastNm2=false;"
"function show(t,ok){var m=document.getElementById('msg');m.textContent=t;m.className='msg '+(ok?'ok':'')+' show';setTimeout(()=>m.className='msg',3000)}"
"function upd(d){if(!d)return;"
"var w=d.wifi,u=d.usb,n=d.nm2;"
"document.getElementById('ws').textContent=w.status==='connected'?'已连接':'未连接';document.getElementById('wd').textContent=w.ip||'-';document.getElementById('w').className='card '+(w.status==='connected'?'on':'');"
"document.getElementById('us').textContent=u.status==='connected'?'已连接':'未连接';document.getElementById('ud').textContent='设备:'+(u.device_count||0);document.getElementById('u').className='card '+(u.status==='connected'?'on':'');"
"var nc=n.status==='connected';document.getElementById('ns').textContent=nc?'已连接':'未连接';document.getElementById('nd').textContent=n.remote_name||'-';document.getElementById('n').className='card '+(nc?'on':'');"
"if(lastNm2&&!nc)testOn=false;lastNm2=nc;document.getElementById('tb').disabled=!nc;document.getElementById('tn').textContent=nc?'点击发送 Note':'需要连接';"
"}"
"function ref(){fetch('/api/status').then(r=>r.json()).then(upd).catch(e=>console.log(e))}"
"function scan(){fetch('/api/wifi/scan').then(r=>r.json()).then(d=>{var s=document.getElementById('ssid');s.innerHTML='<option value=\"\">选择...</option>';if(d&&d.length)d.forEach(x=>{var o=document.createElement('option');o.value=x.ssid;o.textContent=x.ssid+' ('+x.rssi+')';s.appendChild(o)})}).catch(e=>show('扫描失败'))}"
"function connectWifi(){var ss=document.getElementById('ssid').value,ps=document.getElementById('pwd').value;if(!ss){show('选择WiFi');return}var btn=event.target;btn.disabled=true;fetch('/api/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ss,password:ps})}).then(r=>r.json()).then(d=>{btn.disabled=false;if(d.success){show('连接成功',1);setTimeout(()=>location.reload(),2000)}else show(d.message||'失败')}).catch(e=>{btn.disabled=false;show(e.message)})}"
"function load(){fetch('/api/config').then(r=>r.json()).then(d=>{document.getElementById('dn').value=d.device_name||'';document.getElementById('port').value=d.listen_port||5506}).catch(e=>console.log(e))}"
"function save(){var dn=document.getElementById('dn').value,p=parseInt(document.getElementById('port').value);if(!dn){show('输入设备名');return}if(isNaN(p)||p<1024){show('端口无效');return}var btn=event.target;btn.disabled=true;fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({device_name:dn,listen_port:p})}).then(r=>r.json()).then(d=>{btn.disabled=false;if(d.success)show('已保存',1);else show(d.message||'失败')}).catch(e=>{btn.disabled=false;show(e.message)})}"
"function testNote(){var btn=document.getElementById('tb');if(btn.disabled)return;testOn=!testOn;fetch('/api/midi/test',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({note:69,velocity:100,on:testOn})}).then(r=>r.json()).then(d=>{if(!d.success){testOn=!testOn;show(d.message||'失败')}}).catch(e=>{testOn=!testOn;show(e.message)})}"
"function info(){fetch('/api/system/info').then(r=>r.json()).then(d=>alert('设备:'+d.device_name+'\\n版本:'+d.version+'\\nIP:'+d.ip+'\\n内存:'+Math.round(d.free_heap/1024)+'KB')).catch(e=>show(e.message))}"
"function restart(){if(confirm('重启?'))fetch('/api/system/restart',{method:'POST'})}"
"function reset(){if(confirm('恢复出厂?所有配置会清除!'))fetch('/api/system/factory_reset',{method:'POST'})}"
"window.onload=function(){"
"ref();load();scan();"
"setInterval(ref,2000);"
"}"
"</script>"
"</body></html>";

/* ============================================================================
 * HTTP处理函数
 * ============================================================================ */

static esp_err_t http_get_index(httpd_req_t *req) {
    size_t html_len = strlen(html_index);
    ESP_LOGI(TAG, "Sending HTML page, length: %d bytes", html_len);
    
    // 设置响应头
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "identity");
    
    // 使用 httpd_resp_send 一次性发送，ESP-IDF会自动处理分块编码
    // 这种方式比手动分块更可靠
    esp_err_t err = httpd_resp_send(req, html_index, html_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send HTML: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "HTML page sent successfully");
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

/* ============================================================================
 * 新增API: 状态查询与实时推送
 * ============================================================================ */

/**
 * @brief 获取所有状态 (WiFi, USB, NM2)
 */
static esp_err_t http_get_status(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    
    // WiFi 状态 (使用回调)
    cJSON *wifi = cJSON_CreateObject();
    int mode = 0;
    bool wifi_connected = false;
    
    ESP_LOGI(TAG, "[STATUS] Checking WiFi status...");
    ESP_LOGI(TAG, "[STATUS] wifi_get_mode callback = %p", g_status_callbacks.wifi_get_mode);
    ESP_LOGI(TAG, "[STATUS] wifi_is_connected callback = %p", g_status_callbacks.wifi_is_connected);
    
    if (g_status_callbacks.wifi_get_mode) {
        mode = g_status_callbacks.wifi_get_mode();
        ESP_LOGI(TAG, "[STATUS] WiFi mode = %d", mode);
    }
    if (g_status_callbacks.wifi_is_connected) {
        wifi_connected = g_status_callbacks.wifi_is_connected();
        ESP_LOGI(TAG, "[STATUS] WiFi connected = %d", wifi_connected);
    }
    
    cJSON_AddStringToObject(wifi, "status", 
        mode == 2 ? "connected" :      // WIFI_RUN_MODE_STA_CONNECTED
        mode == 1 ? "connecting" :     // WIFI_RUN_MODE_STA_TRYING
        mode == 3 ? "ap_mode" : "disconnected");  // WIFI_RUN_MODE_AP
    
    if (wifi_connected) {
        char ip_str[16] = "unknown";
        if (g_status_callbacks.wifi_get_ip) {
            g_status_callbacks.wifi_get_ip(ip_str, sizeof(ip_str));
        }
        cJSON_AddStringToObject(wifi, "ip", ip_str);
        
        // 获取 SSID 和 RSSI
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            cJSON_AddStringToObject(wifi, "ssid", (char*)ap_info.ssid);
            cJSON_AddNumberToObject(wifi, "rssi", ap_info.rssi);
        }
    }
    cJSON_AddItemToObject(root, "wifi", wifi);
    
    // USB 状态 (使用回调)
    cJSON *usb = cJSON_CreateObject();
    uint8_t usb_dev_count = 0;
    bool usb_connected = false;
    
    if (g_status_callbacks.usb_get_device_count) {
        usb_dev_count = g_status_callbacks.usb_get_device_count();
        usb_connected = usb_dev_count > 0;
    }
    
    cJSON_AddStringToObject(usb, "status", usb_connected ? "connected" : "disconnected");
    cJSON_AddNumberToObject(usb, "device_count", usb_dev_count);
    
    if (g_status_callbacks.usb_is_running) {
        cJSON_AddBoolToObject(usb, "running", g_status_callbacks.usb_is_running());
    }
    cJSON_AddItemToObject(root, "usb", usb);
    
    // NM2 会话状态 (使用回调)
    cJSON *nm2 = cJSON_CreateObject();
    bool nm2_active = false;
    
    if (g_status_callbacks.nm2_is_session_active) {
        nm2_active = g_status_callbacks.nm2_is_session_active();
    }
    
    cJSON_AddStringToObject(nm2, "status", nm2_active ? "connected" : "disconnected");
    
    if (nm2_active) {
        if (g_status_callbacks.nm2_get_remote_name) {
            const char* remote_name = g_status_callbacks.nm2_get_remote_name();
            if (remote_name) {
                cJSON_AddStringToObject(nm2, "remote_name", remote_name);
            }
        }
    }
    cJSON_AddItemToObject(root, "nm2", nm2);
    
    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(root);
    
    return ESP_OK;
}

/**
 * @brief 构建状态JSON对象 (避免重复代码)
 */
static cJSON* build_status_json(void) {
    cJSON *root = cJSON_CreateObject();
    
    // WiFi 状态
    cJSON *wifi = cJSON_CreateObject();
    int mode = 0;
    if (g_status_callbacks.wifi_get_mode) {
        mode = g_status_callbacks.wifi_get_mode();
    }
    cJSON_AddStringToObject(wifi, "status", 
        mode == 2 ? "connected" :
        mode == 1 ? "connecting" :
        mode == 3 ? "ap_mode" : "disconnected");
    if (mode == 2) {
        char ip_str[16] = "unknown";
        if (g_status_callbacks.wifi_get_ip) {
            g_status_callbacks.wifi_get_ip(ip_str, sizeof(ip_str));
        }
        cJSON_AddStringToObject(wifi, "ip", ip_str);
    }
    cJSON_AddItemToObject(root, "wifi", wifi);
    
    // USB 状态
    cJSON *usb = cJSON_CreateObject();
    uint8_t usb_count = 0;
    if (g_status_callbacks.usb_get_device_count) {
        usb_count = g_status_callbacks.usb_get_device_count();
    }
    cJSON_AddStringToObject(usb, "status", usb_count > 0 ? "connected" : "disconnected");
    cJSON_AddNumberToObject(usb, "device_count", usb_count);
    cJSON_AddItemToObject(root, "usb", usb);
    
    // NM2 状态
    cJSON *nm2 = cJSON_CreateObject();
    bool nm2_active = false;
    if (g_status_callbacks.nm2_is_session_active) {
        nm2_active = g_status_callbacks.nm2_is_session_active();
    }
    cJSON_AddStringToObject(nm2, "status", nm2_active ? "connected" : "disconnected");
    if (nm2_active && g_status_callbacks.nm2_get_remote_name) {
        const char* name = g_status_callbacks.nm2_get_remote_name();
        if (name) {
            cJSON_AddStringToObject(nm2, "remote_name", name);
        }
    }
    cJSON_AddItemToObject(root, "nm2", nm2);
    
    return root;
}

/**
 * @brief SSE 实时状态推送 (优化版本)
 */
static esp_err_t http_get_status_stream(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_set_hdr(req, "X-Accel-Buffering", "no");
    
    ESP_LOGI(TAG, "SSE client connected");
    
    // 发送初始状态
    cJSON *json = build_status_json();
    char *json_str = cJSON_Print(json);
    char sse_buf[1024];
    snprintf(sse_buf, sizeof(sse_buf), "data: %s\n\n", json_str);
    
    esp_err_t err = httpd_resp_send_chunk(req, sse_buf, strlen(sse_buf));
    free(json_str);
    cJSON_Delete(json);
    
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send initial SSE data");
        return err;
    }
    
    // 持续推送状态更新 (每秒检查一次)
    int heartbeat_count = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // 每5秒发送完整状态 (心跳计数到5次)
        if (heartbeat_count % 5 == 0) {
            json = build_status_json();
            json_str = cJSON_Print(json);
            snprintf(sse_buf, sizeof(sse_buf), "data: %s\n\n", json_str);
            
            err = httpd_resp_send_chunk(req, sse_buf, strlen(sse_buf));
            free(json_str);
            cJSON_Delete(json);
            
            if (err != ESP_OK) {
                ESP_LOGI(TAG, "SSE client disconnected");
                break;
            }
        } else {
            // 心跳消息 (注释行，客户端会忽略)
            err = httpd_resp_send_chunk(req, ": keep-alive\n\n", 14);
            if (err != ESP_OK) {
                ESP_LOGI(TAG, "SSE client disconnected (heartbeat)");
                break;
            }
        }
        
        heartbeat_count++;
    }
    
    httpd_resp_send_chunk(req, NULL, 0);  // 结束响应
    return ESP_OK;
}

/**
 * @brief 发送测试音符
 */
static esp_err_t http_post_midi_test(httpd_req_t *req) {
    char buf[128];
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
    
    // 解析参数
    cJSON *note_json = cJSON_GetObjectItem(root, "note");
    cJSON *velocity_json = cJSON_GetObjectItem(root, "velocity");
    cJSON *on_json = cJSON_GetObjectItem(root, "on");  // true=Note On, false=Note Off
    
    uint8_t note = note_json ? note_json->valueint : 69;  // 默认 A4 (69)
    uint8_t velocity = velocity_json ? velocity_json->valueint : 100;
    bool note_on = on_json ? on_json->valueint : true;
    
    // 构建 MIDI 消息
    uint8_t status = note_on ? 0x90 : 0x80;  // Note On/Off (channel 0)
    
    ESP_LOGI(TAG, "Test MIDI: %s note=%d vel=%d", note_on ? "Note On" : "Note Off", note, velocity);
    
    // 发送到网络 (使用回调)
    cJSON *resp = cJSON_CreateObject();
    
    bool nm2_active = false;
    if (g_status_callbacks.nm2_is_session_active) {
        nm2_active = g_status_callbacks.nm2_is_session_active();
    }
    
    if (nm2_active && g_status_callbacks.nm2_send_midi) {
        bool success = g_status_callbacks.nm2_send_midi(status, note, note_on ? velocity : 0);
        if (success) {
            cJSON_AddBoolToObject(resp, "success", true);
            cJSON_AddStringToObject(resp, "message", "MIDI sent to network");
        } else {
            cJSON_AddBoolToObject(resp, "success", false);
            cJSON_AddStringToObject(resp, "message", "Failed to send MIDI");
        }
    } else {
        cJSON_AddBoolToObject(resp, "success", false);
        cJSON_AddStringToObject(resp, "message", "No active NM2 session");
    }
    
    char *json_str = cJSON_Print(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(resp);
    cJSON_Delete(root);
    
    return ESP_OK;
}

/**
 * @brief 获取 USB 设备状态 (使用回调)
 */
static esp_err_t http_get_usb_status(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    
    uint8_t dev_count = 0;
    bool running = false;
    
    if (g_status_callbacks.usb_get_device_count) {
        dev_count = g_status_callbacks.usb_get_device_count();
    }
    if (g_status_callbacks.usb_is_running) {
        running = g_status_callbacks.usb_is_running();
    }
    
    cJSON_AddNumberToObject(root, "device_count", dev_count);
    cJSON_AddBoolToObject(root, "running", running);
    cJSON_AddStringToObject(root, "status", dev_count > 0 ? "connected" : "disconnected");
    
    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(root);
    
    return ESP_OK;
}

/**
 * @brief 获取 NM2 会话状态 (使用回调)
 */
static esp_err_t http_get_nm2_status(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    
    bool active = false;
    if (g_status_callbacks.nm2_is_session_active) {
        active = g_status_callbacks.nm2_is_session_active();
    }
    
    cJSON_AddBoolToObject(root, "active", active);
    cJSON_AddStringToObject(root, "status", active ? "connected" : "disconnected");
    
    if (active) {
        if (g_status_callbacks.nm2_get_remote_name) {
            const char* remote_name = g_status_callbacks.nm2_get_remote_name();
            if (remote_name) {
                cJSON_AddStringToObject(root, "remote_name", remote_name);
            }
        }
    }
    
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

midi_error_t web_config_server_register_callbacks(const web_status_callbacks_t* callbacks) {
    if (callbacks) {
        g_status_callbacks = *callbacks;
        ESP_LOGI(TAG, "Status callbacks registered: mode=%p, connected=%p, ip=%p", 
            callbacks->wifi_get_mode, callbacks->wifi_is_connected, callbacks->wifi_get_ip);
    }
    return MIDI_OK;
}

midi_error_t web_config_server_start(void) {
    if (g_running) {
        return MIDI_ERR_ALREADY_INITIALIZED;
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = g_port;
    config.max_uri_handlers = 20;  // 增加以支持新端点
    
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
    
    // 新增API端点
    httpd_uri_t uri_status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = http_get_status,
    };
    httpd_register_uri_handler(g_server, &uri_status);
    
    httpd_uri_t uri_status_stream = {
        .uri = "/api/status/stream",
        .method = HTTP_GET,
        .handler = http_get_status_stream,
    };
    httpd_register_uri_handler(g_server, &uri_status_stream);
    
    httpd_uri_t uri_midi_test = {
        .uri = "/api/midi/test",
        .method = HTTP_POST,
        .handler = http_post_midi_test,
    };
    httpd_register_uri_handler(g_server, &uri_midi_test);
    
    httpd_uri_t uri_usb_status = {
        .uri = "/api/usb/status",
        .method = HTTP_GET,
        .handler = http_get_usb_status,
    };
    httpd_register_uri_handler(g_server, &uri_usb_status);
    
    httpd_uri_t uri_nm2_status = {
        .uri = "/api/nm2/status",
        .method = HTTP_GET,
        .handler = http_get_nm2_status,
    };
    httpd_register_uri_handler(g_server, &uri_nm2_status);
    
    g_running = true;
    ESP_LOGI(TAG, "Web server started on port %d with %d handlers", g_port, 15);
    
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