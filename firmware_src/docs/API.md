# LinkMIDI API 参考

## WiFi Manager

```c
#include "wifi_manager.h"
```

### 函数

| 函数 | 说明 | 返回值 |
|------|------|--------|
| `wifi_manager_init()` | 初始化 WiFi 和 NVS | `bool` |
| `wifi_manager_connect()` | 连接 WiFi | `bool` |
| `wifi_manager_wait_for_connection(timeout_ms)` | 等待连接 | `bool` |
| `wifi_manager_is_connected()` | 检查连接状态 | `bool` |
| `wifi_manager_deinit()` | 释放资源 | `void` |

### 示例

```c
wifi_manager_init();
wifi_manager_connect();
if (wifi_manager_wait_for_connection(10000)) {
    printf("WiFi connected!\n");
}
```

---

## Network MIDI 2.0

```c
#include "network_midi2.h"
```

### 初始化

```c
// 简单方式
network_midi2_context_t* ctx = network_midi2_init(
    "DeviceName",    // 设备名
    "ProductID",     // 产品ID
    5507             // 端口
);

// 详细配置
network_midi2_config_t config = {
    .device_name = "MyDevice",
    .product_id = "ESP32S3",
    .listen_port = 5507,
    .mode = MODE_PEER,           // CLIENT/SERVER/PEER
    .enable_discovery = true,
    .log_callback = my_log_fn,
    .midi_rx_callback = my_midi_fn,
    .ump_rx_callback = my_ump_fn,
};
ctx = network_midi2_init_with_config(&config);
```

### 生命周期

| 函数 | 说明 |
|------|------|
| `network_midi2_init(name, product, port)` | 简单初始化 |
| `network_midi2_init_with_config(&config)` | 详细初始化 |
| `network_midi2_start(ctx)` | 启动服务 |
| `network_midi2_stop(ctx)` | 停止服务 |
| `network_midi2_deinit(ctx)` | 释放资源 |

### 发现

| 函数 | 说明 |
|------|------|
| `network_midi2_send_discovery_query(ctx)` | 发送 mDNS 查询 |
| `network_midi2_get_device_count(ctx)` | 获取发现设备数 |
| `network_midi2_get_discovered_device(ctx, index, ...)` | 获取设备信息 |

### 会话

| 函数 | 说明 |
|------|------|
| `network_midi2_session_initiate(ctx, ip, port, name)` | 发起会话 |
| `network_midi2_session_accept(ctx)` | 接受会话 |
| `network_midi2_session_reject(ctx)` | 拒绝会话 |
| `network_midi2_session_terminate(ctx)` | 终止会话 |
| `network_midi2_is_session_active(ctx)` | 检查会话状态 |
| `network_midi2_send_ping(ctx)` | 发送心跳 |

### MIDI 发送

| 函数 | 说明 |
|------|------|
| `network_midi2_send_note_on(ctx, note, vel, ch)` | Note On |
| `network_midi2_send_note_off(ctx, note, vel, ch)` | Note Off |
| `network_midi2_send_control_change(ctx, cc, val, ch)` | Control Change |
| `network_midi2_send_program_change(ctx, pgm, ch)` | Program Change |
| `network_midi2_send_pitch_bend(ctx, bend, ch)` | Pitch Bend |
| `network_midi2_send_midi(ctx, status, d1, d2)` | 原始 MIDI |
| `network_midi2_send_ump(ctx, data, len)` | 原始 UMP |

### 回调类型

```c
typedef void (*network_midi2_log_callback_t)(const char* message);
typedef void (*network_midi2_midi_rx_callback_t)(const uint8_t* data, uint16_t length);
typedef void (*network_midi2_ump_rx_callback_t)(const uint8_t* data, uint16_t length);
```

---

## mDNS Discovery

```c
#include "mdns_discovery.h"
```

### 函数

| 函数 | 说明 |
|------|------|
| `mdns_discovery_init(name, product, port)` | 初始化 |
| `mdns_discovery_start(ctx)` | 启动服务 |
| `mdns_discovery_stop(ctx)` | 停止服务 |
| `mdns_discovery_send_query(ctx)` | 发送查询 |
| `mdns_discovery_get_device_count(ctx)` | 设备数量 |
| `mdns_discovery_get_device(ctx, index, ...)` | 获取设备 |
| `mdns_discovery_deinit(ctx)` | 释放资源 |

---

## USB MIDI Host

```c
#include "usb_midi_host.h"
```

### 初始化

```c
usb_midi_host_config_t config = {
    .midi_rx_callback = on_midi_data,
    .device_connected_callback = on_device_connect,
    .device_disconnected_callback = on_device_disconnect,
};

usb_midi_host_context_t* ctx = usb_midi_host_init(&config);
usb_midi_host_start(ctx);
```

### 生命周期

| 函数 | 说明 |
|------|------|
| `usb_midi_host_init(&config)` | 初始化 |
| `usb_midi_host_start(ctx)` | 启动 |
| `usb_midi_host_stop(ctx)` | 停止 |
| `usb_midi_host_deinit(ctx)` | 释放资源 |

### 设备查询

| 函数 | 说明 |
|------|------|
| `usb_midi_host_get_device_count(ctx)` | 设备数量 |
| `usb_midi_host_get_device_info(ctx, index, &info)` | 设备信息 |
| `usb_midi_host_is_device_connected(ctx, index)` | 连接状态 |
| `usb_midi_host_is_running(ctx)` | 运行状态 |

### 回调类型

```c
// MIDI 数据接收
typedef void (*usb_midi_rx_callback_t)(
    uint8_t device_index,
    const uint8_t* data,
    uint16_t length
);

// 设备连接
typedef void (*usb_midi_device_connected_callback_t)(
    uint8_t device_index,
    const usb_midi_device_t* device_info
);

// 设备断开
typedef void (*usb_midi_device_disconnected_callback_t)(
    uint8_t device_index
);
```

### 设备信息结构

```c
typedef struct {
    uint16_t vendor_id;        // USB VID
    uint16_t product_id;       // USB PID
    char manufacturer[128];    // 制造商
    char product_name[128];    // 产品名
    uint8_t device_address;    // USB 地址
    uint8_t midi_in_endpoint;  // MIDI IN 端点
    uint16_t max_packet_size;  // 最大包大小
    bool is_connected;         // 连接状态
} usb_midi_device_t;
```

---

## MIDI 消息格式

### MIDI 1.0 状态字节

| 状态 | 说明 | 数据字节 |
|------|------|----------|
| `0x80-0x8F` | Note Off | 2 |
| `0x90-0x9F` | Note On | 2 |
| `0xA0-0xAF` | Poly Pressure | 2 |
| `0xB0-0xBF` | Control Change | 2 |
| `0xC0-0xCF` | Program Change | 1 |
| `0xD0-0xDF` | Channel Pressure | 1 |
| `0xE0-0xEF` | Pitch Bend | 2 |

### 常用 MIDI 值

**音符编号**:
- C4 (中央C): 60
- A4 (440Hz): 69
- C5: 72

**控制器编号**:
- 7: Volume
- 10: Pan
- 64: Sustain Pedal
- 91: Reverb

---

## 配置选项 (Kconfig)

| 选项 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `CONFIG_WIFI_SSID` | string | - | WiFi 名称 |
| `CONFIG_WIFI_PASSWORD` | string | - | WiFi 密码 |
| `CONFIG_WIFI_MAXIMUM_RETRY` | int | 5 | 最大重试次数 |
| `CONFIG_WIFI_SCAN_METHOD` | int | 0 | 扫描方式 |
| `CONFIG_MIDI_DEVICE_NAME` | string | ESP32-MIDI2 | 设备名 |
| `CONFIG_MIDI_PRODUCT_ID` | string | ESP32S3 | 产品ID |
| `CONFIG_MIDI_LISTEN_PORT` | int | 5507 | UDP 端口 |

---

## 错误处理

大多数函数返回 `bool`，`true` 表示成功：

```c
if (!network_midi2_send_note_on(ctx, 60, 100, 0)) {
    ESP_LOGE(TAG, "Failed to send Note On");
}

if (!usb_midi_host_start(ctx)) {
    ESP_LOGE(TAG, "USB MIDI Host start failed");
}
```