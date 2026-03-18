# LinkMIDI 系统架构

## 系统概述

LinkMIDI 是一个嵌入式 MIDI 网络桥接设备，将 USB MIDI 键盘的数据转发到局域网上的 MIDI 2.0 设备。

## 硬件架构

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32-S3-WROOM-1-N16R8                   │
├─────────────────────────────────────────────────────────────┤
│  CPU: Xtensa LX7 双核 @ 240MHz                              │
│  Flash: 16MB | PSRAM: 8MB                                   │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   ┌─────────┐    ┌─────────┐    ┌─────────┐               │
│   │  WiFi   │    │  USB    │    │  GPIO   │               │
│   │ Module  │    │  OTG    │    │  Ports  │               │
│   └────┬────┘    └────┬────┘    └─────────┘               │
│        │              │                                    │
└────────┼──────────────┼────────────────────────────────────┘
         │              │
    ┌────┴────┐    ┌────┴────┐
    │ 2.4GHz  │    │USB MIDI │
    │ Antenna │    │ Keyboard│
    └─────────┘    └─────────┘
```

## 软件架构

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│                      main.c / app_main()                     │
├─────────────────────────────────────────────────────────────┤
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │  WiFi    │  │ Network  │  │  mDNS    │  │   USB    │   │
│  │ Manager  │  │  MIDI2   │  │Discovery │  │  MIDI    │   │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘   │
├───────┼─────────────┼─────────────┼─────────────┼──────────┤
│       │             │             │             │          │
│  ┌────┴────┐   ┌────┴────┐   ┌────┴────┐   ┌────┴────┐   │
│  │esp_wifi │   │  lwIP   │   │  mDNS   │   │USB Host │   │
│  └─────────┘   └─────────┘   └─────────┘   └─────────┘   │
├─────────────────────────────────────────────────────────────┤
│                    FreeRTOS Kernel                          │
├─────────────────────────────────────────────────────────────┤
│                    ESP-IDF Framework                         │
└─────────────────────────────────────────────────────────────┘
```

## 数据流

### USB → Network (当前实现)

```
USB MIDI 键盘
     │
     ▼
┌─────────────────┐
│ USB MIDI Host   │ ← 接收 MIDI 1.0 数据
│ (usb_midi_host) │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ usb_midi_rx_    │ ← 回调函数处理
│ callback()      │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Network MIDI 2.0│ ← 转发到网络
│ (network_midi2) │
└────────┬────────┘
         │
         ▼
    UDP/WiFi
         │
         ▼
  局域网 MIDI 2.0 设备
```

### Network → USB (计划实现)

```
局域网 MIDI 2.0 设备
         │
         ▼
    UDP/WiFi
         │
         ▼
┌─────────────────┐
│ Network MIDI 2.0│ ← 接收 UMP 数据
│ (network_midi2) │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ UMP → MIDI 1.0  │ ← 协议转换
│ (待实现)        │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ USB MIDI Device │ ← 发送到 USB 设备
│ (待实现)        │
└─────────────────┘
```

## 模块说明

### WiFi Manager (`main/wifi_manager.c`)

**职责**: WiFi 连接管理、NVS 存储、自动重连

**API**:
```c
bool wifi_manager_init(void);
bool wifi_manager_connect(void);
bool wifi_manager_wait_for_connection(uint32_t timeout_ms);
bool wifi_manager_is_connected(void);
void wifi_manager_deinit(void);
```

### Network MIDI 2.0 (`components/network_midi2`)

**职责**: MIDI 2.0 网络协议实现

**子模块**:
- `network_midi2.c` - 会话管理、数据传输
- `mdns_discovery.c` - 设备发现

**设备模式**:
- `MODE_CLIENT` - 仅发起会话
- `MODE_SERVER` - 仅接受会话
- `MODE_PEER` - 双向模式

### USB MIDI Host (`components/usb_midi_host`)

**职责**: USB 主机驱动、MIDI 设备枚举、数据接收

**特性**:
- 最多 4 个并发设备
- 热插拔支持
- 回调驱动架构

## 内存布局

```
┌────────────────────────────────────┐
│  Flash (16 MB)                     │
├────────────────────────────────────┤
│  Application Code    ~1 MB         │
│  ESP-IDF Libraries   ~2 MB         │
│  FAT/Spiffs          ~2 MB         │
│  OTA Partition       ~4 MB         │
│  Reserved            ~7 MB         │
└────────────────────────────────────┘

┌────────────────────────────────────┐
│  Internal RAM (~512 KB)            │
├────────────────────────────────────┤
│  Static Data         ~100 KB       │
│  Heap (Free)         ~200 KB       │
│  WiFi Stack          ~100 KB       │
│  USB Stack           ~50 KB        │
│  Other               ~62 KB        │
└────────────────────────────────────┘

┌────────────────────────────────────┐
│  PSRAM (8 MB)                      │
├────────────────────────────────────┤
│  Network Buffers     ~2 MB         │
│  USB Buffers         ~1 MB         │
│  Application Heap    ~5 MB         │
└────────────────────────────────────┘
```

## 任务优先级

| 任务 | 优先级 | 栈大小 | 说明 |
|------|--------|--------|------|
| WiFi Event | 24 | 系统定义 | WiFi 事件处理 |
| MIDI Send | 10 | 3 KB | MIDI 数据发送 |
| Session Monitor | 5 | 2 KB | 会话状态监控 |
| USB Event | 6 | 4 KB | USB 事件处理 |
| IDLE | 0 | 系统定义 | 空闲任务 |

## 启动流程

```
app_main()
    │
    ├─→ wifi_manager_init()      // 初始化 NVS + WiFi
    │       │
    │       └─→ wifi_manager_connect()
    │               │
    │               └─→ wifi_manager_wait_for_connection()
    │
    ├─→ network_midi2_init_with_config()  // 初始化 MIDI 2.0
    │       │
    │       └─→ network_midi2_start()
    │
    ├─→ mdns_discovery_init()    // 初始化 mDNS
    │       │
    │       └─→ mdns_discovery_start()
    │
    ├─→ usb_midi_host_init()     // 初始化 USB
    │       │
    │       └─→ usb_midi_host_start()
    │
    └─→ xTaskCreate()            // 创建应用任务
            │
            ├─→ session_monitor_task
            └─→ midi_send_task
```

## 网络协议

### MIDI 2.0 over UDP

```
┌─────────────────────────────────────┐
│           UDP Packet                │
├─────────────────────────────────────┤
│ Session Header (16 bytes)           │
│   - Signature: 0x4D503032 ("MP02")  │
│   - Protocol Version: 0x0000        │
│   - Sender SSRC: 4 bytes            │
│   - Receiver SSRC: 4 bytes           │
├─────────────────────────────────────┤
│ UMP Header (4 bytes)                │
│   - Sequence Number: 16 bits        │
│   - UMP Count: 8 bits               │
│   - Reserved: 8 bits                │
├─────────────────────────────────────┤
│ UMP Data (variable)                 │
│   - 4 bytes per UMP message         │
└─────────────────────────────────────┘
```

### 会话状态机

```
          INV Request
IDLE ───────────────────→ INV_PENDING
  ↑                            │
  │                     ACCEPT/REJECT
  │                            │
  │         ┌──────────────────┼──────────────────┐
  │         │                  │                  │
  │         ▼                  ▼                  │
  │      ACTIVE             REJECTED             │
  │         │                                     │
  │         │ END                                 │
  │         │                                     │
  └─────────┴─────────────────────────────────────┘
```

## 扩展计划

### Phase 3: MIDI 路由层

- MIDI 1.0 → UMP 转换
- 双向数据流
- 多设备路由

### Phase 4: 增强功能

- Web 配置界面
- OTA 固件更新
- 蓝牙 MIDI 支持

## 参考资料

- [MIDI 2.0 Specification](https://midi.org/specifications/midi2)
- [Network MIDI 2.0 (UDP)](https://midi.org/network-midi-2-0)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/)
- [USB MIDI Class Specification](https://www.usb.org/document-library/usb-audio-specification)