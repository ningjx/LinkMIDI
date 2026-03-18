# Network MIDI 2.0 API快速参考

## 初始化 & 清理

```c
// 创建上下文 - 简单方式
context_t* ctx = network_midi2_init("DeviceName", "ProductID", 5507);

// 创建上下文 - 详细方式
network_midi2_config_t cfg = {
    .device_name = "DeviceName",
    .product_id = "ProductID",
    .listen_port = 5507,
    .mode = MODE_PEER,
    .enable_discovery = true,
    .log_callback = log_fn,
    .midi_rx_callback = midi_rx_fn,
    .ump_rx_callback = ump_rx_fn,
};
context_t* ctx = network_midi2_init_with_config(&cfg);

// 启动设备
network_midi2_start(ctx);

// 停止并清理
network_midi2_stop(ctx);
network_midi2_deinit(ctx);
```

## 发现 (Discovery)

```c
// 发送发现查询
network_midi2_send_discovery_query(ctx);
vTaskDelay(pdMS_TO_TICKS(3000));  // 等待响应

// 获取发现的设备
int count = network_midi2_get_device_count(ctx);
for (int i = 0; i < count; i++) {
    char name[64];
    uint32_t ip;
    uint16_t port;
    network_midi2_get_discovered_device(ctx, i, name, &ip, &port);
    printf("%s: %d.%d.%d.%d:%d\n", name, /* IP展开 */, port);
}
```

## 会话 (Session)

```c
// 发起会话（客户端）
bool success = network_midi2_session_initiate(ctx, 0xC0A80101, 5507, "RemoteDevice");

// 接受会话（服务器）
network_midi2_session_accept(ctx);

// 拒绝会话（服务器）
network_midi2_session_reject(ctx);

// 检查会话状态
if (network_midi2_is_session_active(ctx)) {
    // 会话已建立
}

// 获取会话详细信息
network_midi2_session_t info;
network_midi2_get_session_info(ctx, &info);
printf("Connected to: %s (SSRC: %02X <-> %02X)\n", 
       info.device_name, info.local_ssrc, info.remote_ssrc);

// 保持会话活跃
network_midi2_send_ping(ctx);

// 关闭会话
network_midi2_session_terminate(ctx);
```

## MIDI数据发送 (High-Level)

```c
// Note On (音符开始)
network_midi2_send_note_on(ctx, 60, 100, 0);  // note, velocity, channel

// Note Off (音符结束)
network_midi2_send_note_off(ctx, 60, 0, 0);

// Control Change (控制器)
network_midi2_send_control_change(ctx, 7, 100, 0);  // CC, value, channel

// Program Change (程序切换)
network_midi2_send_program_change(ctx, 3, 0);  // program, channel

// Pitch Bend (弯音)
network_midi2_send_pitch_bend(ctx, 2000, 0);  // bend (-8192..8191), channel

// 原始MIDI消息
network_midi2_send_midi(ctx, 0x90, 60, 100);  // status, data1, data2
```

## MIDI数据接收 (Callbacks)

```c
// 设置MIDI接收回调
void my_midi_rx(const uint8_t* data, uint16_t len) {
    if (len >= 3) {
        printf("MIDI Received: %02X %02X %02X\n", data[0], data[1], data[2]);
    }
}
network_midi2_set_midi_rx_callback(ctx, my_midi_rx);

// 设置UMP接收回调（原始格式）
void my_ump_rx(const uint8_t* data, uint16_t len) {
    printf("UMP Received: %d bytes\n", len);
}
network_midi2_set_ump_rx_callback(ctx, my_ump_rx);

// 设置日志回调
void my_log(const char* msg) {
    printf("[Log] %s\n", msg);
}
network_midi2_set_log_callback(ctx, my_log);
```

## 常用MIDI号参考

### Note Numbers
- C4 (Middle C): 60
- C3: 48, C5: 72
- A4 (440Hz): 69

### Common CC Numbers
- Volume (Master): 7
- Pan: 10
- Sustain Pedal: 64
- Reverb: 91
- Chorus: 93

### MIDI Channels  
- 0-15对应MIDI通道1-16

## 设备模式

```c
MODE_CLIENT  = 0  // 仅发起会话（客户端）
MODE_SERVER  = 1  // 仅接收会话（服务器）
MODE_PEER    = 2  // 既可发起也可接收（推荐）
```

## 会话状态

```c
SESSION_STATE_IDLE        // 无活跃会话
SESSION_STATE_INV_PENDING // 等待邀请响应
SESSION_STATE_ACTIVE      // 会话已建立
SESSION_STATE_CLOSING     // 会话关闭中
```

## 错误处理

```c
// 大多数函数返回bool或整数
if (!network_midi2_send_note_on(ctx, 60, 100, 0)) {
    printf("Failed to send Note On\n");
}

// 检查会话活跃状态
if (network_midi2_is_session_active(ctx)) {
    network_midi2_send_ping(ctx);
} else {
    printf("No active session\n");
}

// 检查会话状态
network_midi2_session_state_t state = network_midi2_get_session_state(ctx);
if (state == SESSION_STATE_ACTIVE) {
    // 发送数据
}
```

## 实用代码片段

### WiFi初始化示例

```c
void init_wifi(const char* ssid, const char* password) {
    esp_netif_init();
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ssid,
            .password = password,
        },
    };
    
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}
```

### 简单应用框架

```c
static network_midi2_context_t* g_ctx;

void app_main(void) {
    // 初始化
    init_wifi("SSID", "PASSWORD");
    
    g_ctx = network_midi2_init("MyDevice", "ESP32S3", 5507);
    network_midi2_set_log_callback(g_ctx, my_log_fn);
    network_midi2_set_midi_rx_callback(g_ctx, my_midi_rx_fn);
    
    network_midi2_start(g_ctx);
    
    // 主循环
    while (1) {
        if (network_midi2_is_session_active(g_ctx)) {
            // 发送周期性MIDI
            network_midi2_send_note_on(g_ctx, 60, 100, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            network_midi2_send_note_off(g_ctx, 60, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
        } else {
            // 发送发现查询
            network_midi2_send_discovery_query(g_ctx);
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
}
```

### MIDI消息解析

```c
void analyze_midi(uint8_t status, uint8_t data1, uint8_t data2) {
    uint8_t cmd = status & 0xF0;
    uint8_t ch = status & 0x0F;
    
    switch (cmd) {
        case 0x80:  // Note Off
            printf("Note Off: note=%d, velocity=%d, channel=%d\n",
                   data1, data2, ch);
            break;
        case 0x90:  // Note On
            printf("Note On: note=%d, velocity=%d, channel=%d\n",
                   data1, data2, ch);
            break;
        case 0xB0:  // CC
            printf("CC: controller=%d, value=%d, channel=%d\n",
                   data1, data2, ch);
            break;
        case 0xC0:  // Program Change
            printf("Program Change: program=%d, channel=%d\n",
                   data1, ch);
            break;
        case 0xE0:  // Pitch Bend
            int bend = ((data2 & 0x7F) << 7) | (data1 & 0x7F);
            printf("Pitch Bend: value=%d, channel=%d\n", bend, ch);
            break;
        default:
            printf("Unknown: %02X %02X %02X\n", status, data1, data2);
    }
}
```

## 调试输出示例

```
I (1234) MIDI2_DEMO: === Network MIDI 2.0 Library Demo ===
I (1234) NM2: [Init] Device: ESP32-MIDI2-Device, Port: 5507, SSRC: 42
I (2500) NM2: [Socket] Data socket created on port 5507
I (3000) NM2: [Discovery] Sent mDNS query for _midi2._udp
I (6000) NM2: [Discovery] Found: RemoteDevice._midi2._udp.local at 192.168.1.100:5507
I (7000) NM2: [Session] Sent INV to C0A80164:5507 (SSRC: 42 -> ?)
I (8000) NM2: [Session] INV ACCEPTED! SSRC: 42 <-> 3C
I (9000) NM2: [Send] UMP send: Note On note=60, velocity=100
```

## 性能提示

- 避免在中断中调用库函数，使用队列传递消息
- 使用`network_midi2_send_ping()`保持长时间连接
- 定期检查`is_session_active()`以检测连接断开
- 对于批量MIDI消息，在单个UDP包中发送多个UMP
- 使用MODE_PEER允许设备同时作为客户端和服务器

## 常见陷阱

❌ 不要在未启动的设备上发送MIDI
```c
// 错误：
g_ctx = network_midi2_init(...);
network_midi2_send_note_on(g_ctx, 60, 100, 0);  // 会失败！

// 正确：
g_ctx = network_midi2_init(...);
network_midi2_start(g_ctx);
network_midi2_send_note_on(g_ctx, 60, 100, 0);  // OK
```

❌ 不要忘记设置WiFi
```c
// 错误 - 无网络连接
network_midi2_send_discovery_query(ctx);

// 正确 - WiFi先连接
init_wifi("SSID", "PASSWORD");
vTaskDelay(pdMS_TO_TICKS(5000));  // 等待连接
network_midi2_send_discovery_query(ctx);
```

❌ 不要在没有活跃会话时发送MIDI
```c
// 错误
network_midi2_send_note_on(ctx, 60, 100, 0);  // 可能返回false

// 正确
if (network_midi2_is_session_active(ctx)) {
    network_midi2_send_note_on(ctx, 60, 100, 0);
}
```

✅ 推荐的应用流程

1. 初始化WiFi和库
2. 启动MIDI设备
3. 设置必要的回调
4. 定期发送发现查询
5. 建立会话连接
6. 发送/接收MIDI数据
7. 处理连接中断和重新连接

