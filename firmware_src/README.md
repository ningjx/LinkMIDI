# LinkMIDI Firmware

基于 ESP32-S3 的网络 MIDI 2.0 设备固件，支持 USB MIDI 输入和 WiFi 网络传输。

## 硬件规格

| 项目 | 规格 |
|------|------|
| **MCU** | ESP32-S3-WROOM-1-N16R8 |
| **Flash** | 16 MB |
| **PSRAM** | 8 MB (OPI) |
| **CPU** | Xtensa LX7 双核 240 MHz |
| **WiFi** | 802.11 b/g/n |
| **USB** | USB OTG (Host/Device) |
| **GPIO** | GPIO19(D-), GPIO20(D+) |

## 功能特性

- **USB MIDI Host** - 连接 USB MIDI 键盘/控制器，接收 MIDI 数据
- **Network MIDI 2.0** - 通过 WiFi 传输 MIDI 2.0 协议
- **mDNS 发现** - 自动发现网络上的 MIDI 2.0 设备
- **会话管理** - 支持客户端/服务器/对等模式
- **多设备支持** - 最多 4 个 USB MIDI 设备

## 项目结构

```
firmware_src/
├── main/
│   ├── main.c              # 应用入口
│   ├── wifi_manager.c/h    # WiFi 管理
│   └── Kconfig.projbuild   # 配置选项
├── components/
│   ├── network_midi2/      # Network MIDI 2.0 协议
│   │   ├── include/
│   │   │   ├── network_midi2.h
│   │   │   └── mdns_discovery.h
│   │   └── src/
│   └── usb_midi_host/      # USB MIDI 主机驱动
│       ├── include/
│       └── src/
└── docs/                   # 文档
    ├── ARCHITECTURE.md
    └── API.md
```

## 快速开始

⚠️ **重要提示**: 所有 ESP-IDF 相关命令（如 `idf.py build`、`idf.py flash` 等）必须通过 ESP-IDF Shell 环境执行。详细说明请参考 [ESP-IDF Shell 使用指南](docs/ESP-IDF-SHELL.md)。

### 1. 环境准备

```bash
# 安装 ESP-IDF v5.5+
# 参考: https://docs.espressif.com/projects/esp-idf/

# 设置环境变量
. $HOME/esp/esp-idf/export.sh  # Linux/macOS
# 或
%USERPROFILE%\esp\esp-idf\export.bat  # Windows
```

### 2. 编译和刷写

使用 ESP-IDF Shell 编译和刷写固件：

```powershell
# 编译项目
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'; cd d:\WorkSpace\LinkMIDI\firmware_src; idf.py build}"

# 烧录固件（替换 COM3 为您的串口）
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'; cd d:\WorkSpace\LinkMIDI\firmware_src; idf.py -p COM3 flash}"

# 监控串口输出
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'; cd d:\WorkSpace\LinkMIDI\firmware_src; idf.py -p COM3 monitor}"
```

或使用简化命令（完整流程）：
```powershell
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'; cd d:\WorkSpace\LinkMIDI\firmware_src; idf.py -p COM3 build flash monitor}"
```

**配置项目**（可选）：
```powershell
# 设置目标芯片
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'; cd d:\WorkSpace\LinkMIDI\firmware_src; idf.py set-target esp32s3}"

# 打开配置菜单
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'; cd d:\WorkSpace\LinkMIDI\firmware_src; idf.py menuconfig}"
# 导航到 "Network MIDI 2.0 Configuration" 配置 WiFi 和 MIDI 参数
```

### 4. 验证运行

正常启动日志：
```
===== Network MIDI 2.0 Service Test =====
Initializing NVS...
WiFi connected!
Got IP: 192.168.1.100
MIDI 2.0 Service is RUNNING
Device: ESP32-MIDI2, Port: 5507
USB MIDI Host started
```

## GPIO 连接

### USB MIDI Host

```
ESP32-S3              USB MIDI 设备
────────              ─────────────
GPIO19 (D-) ────────> USB D-
GPIO20 (D+) ────────> USB D+
GND          ────────> USB GND
5V (可选)    ────────> USB VBUS
```

## 配置选项

通过 `idf.py menuconfig` 配置：

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `CONFIG_WIFI_SSID` | - | WiFi 网络名称 |
| `CONFIG_WIFI_PASSWORD` | - | WiFi 密码 |
| `CONFIG_WIFI_MAXIMUM_RETRY` | 5 | 最大重试次数 |
| `CONFIG_MIDI_DEVICE_NAME` | ESP32-MIDI2 | 设备名称 |
| `CONFIG_MIDI_LISTEN_PORT` | 5507 | UDP 监听端口 |

## 文档

- [系统架构](docs/ARCHITECTURE.md)
- [API 参考](docs/API.md)

## 依赖

- ESP-IDF >= 5.5
- FreeRTOS
- lwIP

## 许可证

MIT License