# LinkMIDI - MIDI 网络桥接器

<div align="center">

![ESP32-S3](https://img.shields.io/badge/ESP32--S3-N8R8-blue)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-5.5.3-orange)
![License](https://img.shields.io/badge/License-MIT-green)

**基于 ESP32-S3 的 USB MIDI 到 Network MIDI 2.0 桥接器**

[功能特性](#功能特性) • [硬件要求](#硬件要求) • [快速开始](#快速开始) • [配置说明](#配置说明) • [开发指南](#开发指南)

</div>

---

## 📋 项目简介

LinkMIDI 是一个基于 ESP32-S3 的 MIDI 网络桥接器，能够将 USB MIDI 设备转换为 Network MIDI 2.0 协议，实现 MIDI 信号的无线传输。支持 MIDI 录制、Web 配置、OTA 升级等功能，适用于音乐制作、现场演出等场景。

## ✨ 功能特性

### 核心功能
- 🎹 **USB MIDI Host** - 支持 USB MIDI 设备接入
- 🌐 **Network MIDI 2.0** - 通过 WiFi 传输 MIDI 信号
- 🎵 **MIDI 录制** - 支持 MIDI 信号录制与上传
- 📡 **WiFi 配网** - 支持 AP/STA 模式，Web 配置界面

### 扩展功能
- 🔄 **OTA 升级** - 通过 Web 页面进行固件升级
- ⚙️ **Web 配置** - 配置 WiFi、WebDAV、MIDI 控制等参数
- 📱 **蓝牙翻页** - 支持蓝牙 HID 翻页功能
- 🖥️ **SH1106 屏幕** - 显示连接状态、录制状态等信息

## 🔧 硬件要求

| 组件 | 规格 |
|------|------|
| **MCU** | ESP32-S3-N8R8 (8MB Flash + 8MB PSRAM) |
| **显示屏** | SH1106 OLED (SPI 接口) |
| **USB** | USB Type-C (支持 USB MIDI Host) |
| **存储** | 支持 SD 卡扩展（可选） |

### 引脚配置

```
SH1106 OLED (SPI):
  - MOSI: GPIO 11
  - CLK:  GPIO 12
  - CS:   GPIO 10
  - DC:   GPIO 9
  - RST:  GPIO 8

按键:
  - 功能键: GPIO 0
  - 确认键: GPIO 1
```

## 🚀 快速开始

### 环境准备

1. **安装 ESP-IDF 5.5.3**
   ```bash
   # 参考 ESP-IDF 官方文档
   https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/
   ```

2. **克隆项目**
   ```bash
   git clone https://github.com/yourusername/LinkMIDI.git
   cd LinkMIDI
   ```

### 编译与烧录

使用 ESP-IDF 工具进行编译和烧录：

```bash
# 设置目标芯片
idf.py set-target esp32s3

# 编译项目
idf.py build

# 烧录固件
idf.py -p COM端口 flash

# 查看串口输出
idf.py -p COM端口 monitor
```

### 首次使用

1. **WiFi 配网**
   - 设备启动后会创建热点 `LinkMidi`（密码：`linkmidi`）
   - 连接热点后访问 `http://192.168.4.1` 进入配置页面
   - 配置要连接的 WiFi 信息

2. **连接 MIDI 设备**
   - 将 USB MIDI 设备连接到 ESP32-S3
   - 设备会自动识别并开始转发 MIDI 信号

3. **Network MIDI 接收**
   - 在电脑上使用支持 Network MIDI 2.0 的软件
   - 连接到设备广播的 MIDI 端口

## ⚙️ 配置说明

### Web 配置界面

访问设备 IP 地址进入 Web 配置界面，支持以下配置：

| 配置项 | 说明 |
|--------|------|
| **WiFi 设置** | 配置要连接的 WiFi SSID 和密码 |
| **WebDAV 设置** | 配置 MIDI 文件上传服务器 |
| **MIDI 控制** | 设置控制录制的 MIDI 信号 |
| **文件命名** | 设置录制文件的命名规则 |
| **蓝牙功能** | 配置蓝牙翻页或蓝牙 MIDI |
| **OTA 升级** | 上传新固件进行升级 |

### 默认配置

```
WiFi SSID: LinkMidi
WiFi Password: linkmidi
Web Server Port: 80
Network MIDI Port: 5004
```

## 🏗️ 项目结构

```
LinkMIDI/
├── firmware_src/           # 固件源码
│   ├── main/              # 主程序
│   │   ├── main.c         # 程序入口
│   │   ├── app_core.c     # 应用核心
│   │   ├── app_core.h     # 核心头文件
│   │   └── wifi_manager.c # WiFi 管理
│   ├── components/        # 自定义组件
│   │   ├── common/        # 公共模块
│   │   ├── config_manager/    # 配置管理
│   │   ├── network_midi2/     # Network MIDI 2.0
│   │   ├── usb_midi_host/     # USB MIDI Host
│   │   ├── web_config_server/ # Web 配置服务器
│   │   └── ota_manager/       # OTA 升级
│   ├── partitions_8mb.csv # 8MB Flash 分区表
│   └── sdkconfig          # ESP-IDF 配置
├── Document/              # 项目文档
├── 3DModels/              # 3D 外壳模型
├── PCB/                   # PCB 设计文件
└── README.md              # 本文件
```

## 📖 开发指南

### 组件说明

#### 1. USB MIDI Host (`usb_midi_host`)
处理 USB MIDI 设备的连接和数据传输。

#### 2. Network MIDI 2.0 (`network_midi2`)
实现 Network MIDI 2.0 协议，支持 MIDI 信号的网络传输。

#### 3. 配置管理 (`config_manager`)
管理设备配置，支持 NVS 存储和运行时修改。

#### 4. Web 配置服务器 (`web_config_server`)
提供 Web 界面进行设备配置和固件升级。

#### 5. OTA 管理器 (`ota_manager`)
支持通过 Web 或网络进行固件升级。

### 编译选项

在 `menuconfig` 中可以配置：

```bash
idf.py menuconfig
```

主要配置项：
- PSRAM 配置
- WiFi 参数
- 日志级别
- 组件开关

## 🐛 故障排除

### 常见问题

**Q: 设备无法连接 WiFi**
- 检查 WiFi 信号强度
- 确认 SSID 和密码正确
- 尝试重置 WiFi 配置（长按功能键 5 秒）

**Q: USB MIDI 设备无法识别**
- 确认设备支持 USB MIDI
- 检查 USB 连接是否正常
- 查看串口日志确认错误信息

**Q: Network MIDI 无法连接**
- 确认设备与电脑在同一局域网
- 检查防火墙设置
- 确认 Network MIDI 端口未被占用

## 📝 更新日志

### v1.0.0 (2026-04-05)
- ✨ 初始版本发布
- ✨ 支持 USB MIDI 到 Network MIDI 2.0 桥接
- ✨ 支持 WiFi 配网
- ✨ 支持 Web 配置界面
- ✨ 支持 OTA 升级

## 🤝 贡献指南

欢迎提交 Issue 和 Pull Request！

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 提交 Pull Request

## 📄 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件

## 🙏 致谢

- [ESP-IDF](https://github.com/espressif/esp-idf) - ESP32 开发框架
- [Network MIDI 2.0](https://www.midi.org/) - MIDI 协议标准

## 📧 联系方式

项目维护者: [Your Name]

---

<div align="center">
Made with ❤️ for musicians and developers
</div>
