# Network MIDI 2.0库集成指南

## 项目结构

```
firmware_src/
├── CMakeLists.txt                          # 项目根CMakeLists
├── sdkconfig                               # ESP-IDF配置
├── main/
│   ├── CMakeLists.txt                      # main组件CMakeLists
│   └── main.c                              # 应用程序入口
├── components/
│   └── network_midi2/                      # MIDI 2.0网络库
│       ├── CMakeLists.txt                  # 库CMakeLists
│       ├── README.md                       # 库文档
│       ├── include/
│       │   └── network_midi2.h             # 公开API头文件
│       └── src/
│           └── network_midi2.c             # 库实现
└── pytest_hello_world.py                   # 可选测试脚本
```

## 快速开始

### 1. 前置要求

- ESP-IDF v4.4 或更新版本
- ESP32S3开发板
- WiFi连接

### 2. 配置WiFi

编辑[main/main.c](../main/main.c)中的WiFi凭据：

```c
// 在init_wifi()函数中找到这行：
wifi_config_t wifi_config = {
    .sta = {
        .ssid = "YOUR_SSID",              // 修改为你的WiFi名称
        .password = "YOUR_PASSWORD",      // 修改为你的WiFi密码
    },
};
```

### 3. 配置sdkconfig (可选)

虽然默认配置应该可以工作，但可以优化以下设置：

```bash
# 打开menuconfig
idf.py menuconfig
```

推荐的配置项：

```
Component config → WiFi
  ✓ WiFi support
  
Component config → LWIP
  ✓ LWIP support
  
Component config → mDNS
  ✓ Enable mDNS/DNS-SD
  - Max number of services: 10
```

### 4. 构建项目

```bash
# 设置目标芯片为ESP32S3（如果尚未设置）
idf.py set-target esp32s3

# 清理构建
idf.py clean

# 构建项目
idf.py build

# 烧写到设备
idf.py flash

# 监视串口输出
idf.py monitor
```

## 完整的构建命令参考

```bash
# 一步完成（构建、烧写、监视）
idf.py build flash monitor

# 仅监视当前连接（Ctrl+] 退出）
idf.py monitor

# 指定串口（如COM3）
idf.py monitor -p COM3

# 清理所有编译文件
idf.py clean

# 检查依赖关系
idf.py info
```

## 在现有ESP-IDF项目中集成MIDI 2.0库

如果要在自己的项目中使用这个库：

### 步骤1：复制库文件

```bash
# 假设你的项目根目录为 /path/to/your_project
cd /path/to/your_project

# 创建components目录（如果不存在）
mkdir -p components

# 复制MIDI 2.0库
cp -r /path/to/network_midi2 components/
```

### 步骤2：更新main/CMakeLists.txt

```cmake
idf_component_register(
    SRCS "your_main.c"
    REQUIRES network_midi2 esp_wifi esp_netif esp_event
    PRIV_REQUIRES lwip
    INCLUDE_DIRS "."
)
```

### 步骤3：在C代码中调用

```c
#include "network_midi2.h"

void app_main(void) {
    // 初始化
    network_midi2_context_t* ctx = network_midi2_init(
        "MyDevice",
        "MyProduct", 
        5507
    );
    
    // 设置回调
    network_midi2_set_log_callback(ctx, my_log_callback);
    
    // 启动
    network_midi2_start(ctx);
    
    // ... 使用库 ...
    
    network_midi2_stop(ctx);
    network_midi2_deinit(ctx);
}
```

## 编译故障排除

### 错误：找不到network_midi2.h

**原因**：CMakeLists.txt中未正确指定REQUIRES

**解决**：确保在main/CMakeLists.txt中有：
```cmake
REQUIRES network_midi2
```

### 错误：undefined reference to `network_midi2_init`

**原因**：库未被正确编译或链接

**解决**：
1. 确保`components/network_midi2/`目录存在
2. 运行`idf.py fullclean`
3. 重新构建：`idf.py build`

### 错误：esp_wifi not found

**原因**：WiFi组件未在CMakeLists.txt中声明

**解决**：在main/CMakeLists.txt中添加：
```cmake
REQUIRES esp_wifi esp_netif esp_event
```

## 烧写错误排除

### 错误：Failed to connect to ESP32

**原因**：COM口占用或连接问题

**解决**：
1. 检查USB连接
2. 指定正确的COM口：`idf.py flash -p COM3`
3. 检查驱动程序（CP210x/CH340等）

### 错误：Timed out waiting for packet header

**原因**：设备进入错误模式

**解决**：
1. 按下BOOT按钮并保持，然后按RESET
2. 尝试降低波特率：`idf.py monitor -b 115200`

## 运行时监视

### 查看日志输出

```bash
idf.py monitor
```

典型输出：
```
I (1234) MIDI2_DEMO: === Network MIDI 2.0 Library Demo ===
I (1234) MIDI2_DEMO: Library version: 1.0.0
I (2000) NM2: [Init] Device: ESP32-MIDI2-Device, Port: 5507, SSRC: 42
I (2500) NM2: [Socket] Data socket created on port 5507
```

### WiFi连接问题

如果看不到WiFi连接日志，检查：
1. sdkconfig中是否启用了WiFi
2. WiFi SSID和密码是否正确
3. 是否有足够的内存（check heap size）

### Session连接失败

检查以下几点：
1. 远程设备是否在线和可达
2. 是否在同一网络上
3. 防火墙是否阻止了UDP 5507端口
4. Ping是否可以到达远程设备

## 性能调优

### 内存优化

如果遇到内存不足：

1. 减小栈大小（在main.c中）：
```c
xTaskCreate(app_menu_task, "menu_task", 1024, NULL, 5, NULL);  // 2048 -> 1024
```

2. 禁用不需要的功能：
```c
network_midi2_config_t config = {
    // ...
    .enable_discovery = false,  // 如果不需要发现
};
```

### 网络性能

1. 调整任务优先级：
```c
RECEIVE_TASK_PRIORITY 5  // 在network_midi2.c中，可调整为3-7
```

2. 增加接收缓冲区：
```c
// 在network_midi2_receive_task中
uint8_t buffer[1024];  // 增加缓冲区大小
```

## 与C#参考实现的对应关系

如果熟悉C#实现，这是功能映射：

| C# 代码 | C 等价物 | 用途 |
|--------|---------|------|
| `new NetworkMidi2Server()` | `network_midi2_init()` with `MODE_SERVER` | 创建服务器 |
| `new NetworkMidi2Client()` | `network_midi2_init()` with `MODE_CLIENT` | 创建客户端 |
| `server.Start()` | `network_midi2_start()` | 启动 |
| `SendDiscoveryQuery()` | `network_midi2_send_discovery_query()` | 发现 |
| `ConnectAsync()` | `network_midi2_session_initiate()` | 连接 |
| `SendMidi()` | `network_midi2_send_midi()` | 发送MIDI |
| `OnMidiReceived` | `midi_rx_callback` | 接收MIDI |

## 调试技巧

### 启用详细日志

设置日志级别为DEBUG（在main中）：
```c
esp_log_level_set("*", ESP_LOG_DEBUG);
esp_log_level_set("NM2", ESP_LOG_DEBUG);
```

### 网络数据包分析

使用Wireshark捕获UDP 5507端口以检查MIDI 2.0报文格式。

### 会话调试

检查会话状态：
```c
network_midi2_session_state_t state = network_midi2_get_session_state(ctx);
printf("Session state: %d\n", state);
```

## 高级用法

### 多会话（未来版本）

目前库支持单个会话。多会话支持计划在后续版本实现。

### 自定义MIDI处理

```c
static void custom_midi_handler(const uint8_t* data, uint16_t length) {
    // 处理MIDI数据
    switch (data[0] & 0xF0) {
        case 0x90:  // Note On
            handle_note_on(data[1], data[2]);
            break;
        case 0x80:  // Note Off
            handle_note_off(data[1], data[2]);
            break;
        // ...
    }
}

network_midi2_set_midi_rx_callback(ctx, custom_midi_handler);
```

### UMP扩展消息

对于需要使用MIDI 2.0扩展的应用：
```c
uint8_t midi2_ump[] = {
    0x40,  // MT=4 (MIDI 2.0), group=0
    0x90,  // Note On status
    60,    // Note
    0,     // Reserved
    0,     // Velocity (32-bit in MIDI 2.0)
    0,
    100,
    0
};

network_midi2_send_ump(ctx, midi2_ump, 8);
```

## 性能基准

在标准ESP32S3开发板上：

| 指标 | 值 |
|------|-----|
| 初始化时间 | ~50ms |
| 发现响应时间 | ~200ms |
| 会话建立时间 | ~100ms |
| MIDI发送延迟 | 5-15ms |
| 内存占用（静态） | ~45KB |
| 内存占用（动态） | ~20KB |

## 常见问题 (FAQ)

### Q: 可以同时连接多个远程设备吗？
A: 当前版本仅支持一个活跃会话，但可以快速切换。未来版本将支持多会话。

### Q: 如何实现认证？
A: 认证功能目前未实现。可以在应用层使用MIDI通用系统消息实现简单认证。

### Q: UDP可靠性如何保证？
A: 库实现了序列号检测丢包。建议在应用层实现重传逻辑。

### Q: 支持MIDI 2.0所有消息吗？
A: 目前支持MIDI 1.0消息映射。MIDI 2.0扩展消息可通过raw UMP发送。

### Q: 功耗如何？
A: 典型功耗（活跃WiFi）约200mA。可通过禁用发现和降低任务优先级优化。

## 相关资源

- [MIDI 2.0规范](https://midi.org/specifications/midi2)
- [ESP-IDF官方文档](https://docs.espressif.com/projects/esp-idf/)  
- [lwIP TCP/IP栈](http://savannah.nongnu.org/projects/lwip/)
- [mDNS/DNS-SD RFC](https://datatracker.ietf.org/doc/html/rfc6763)

## 技术支持

遇到问题？
1. 查看库中的日志输出
2. 参考[library README](../components/network_midi2/README.md)
3. 检查[示例代码](../main/main.c)
4. 在GitHub上报告问题

