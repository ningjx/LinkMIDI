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
