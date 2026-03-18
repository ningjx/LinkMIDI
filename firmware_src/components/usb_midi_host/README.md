# USB MIDI Host Component

ESP32 USB 主机 MIDI 设备驱动。

## 功能

- USB 主机模式支持 MIDI 键盘/控制器
- 自动设备枚举和识别
- 热插拔支持
- 最多 4 个并发设备

## 硬件要求

- ESP32-S3 或 ESP32-S2 (USB OTG)
- GPIO19 (D-), GPIO20 (D+)

## API 快速参考

```c
#include "usb_midi_host.h"

// 回调函数
void on_midi(uint8_t dev_idx, const uint8_t* data, uint16_t len) {
    // 处理 MIDI 数据
}

void on_connect(uint8_t dev_idx, const usb_midi_device_t* info) {
    printf("Device connected: %s\n", info->product_name);
}

void on_disconnect(uint8_t dev_idx) {
    printf("Device disconnected\n");
}

// 初始化
usb_midi_host_config_t config = {
    .midi_rx_callback = on_midi,
    .device_connected_callback = on_connect,
    .device_disconnected_callback = on_disconnect,
};

usb_midi_host_context_t* ctx = usb_midi_host_init(&config);
usb_midi_host_start(ctx);

// 查询设备
uint8_t count = usb_midi_host_get_device_count(ctx);
usb_midi_device_t info;
usb_midi_host_get_device_info(ctx, 0, &info);

// 清理
usb_midi_host_stop(ctx);
usb_midi_host_deinit(ctx);
```

## MIDI 数据格式

USB MIDI 数据为标准 MIDI 1.0 格式 (3 字节):

| 状态 | 说明 |
|------|------|
| `0x90` | Note On |
| `0x80` | Note Off |
| `0xB0` | Control Change |
| `0xC0` | Program Change |
| `0xE0` | Pitch Bend |

## 文件

```
components/usb_midi_host/
├── include/usb_midi_host.h
├── src/usb_midi_host.c
└── CMakeLists.txt
```

## 依赖

- ESP-IDF >= 5.0
- USB Host 库

## 参考资料

- [USB MIDI Class Spec](https://www.usb.org/document-library/usb-audio-specification)
- [MIDI 1.0 Spec](https://www.midi.org/)
- [ESP-IDF USB Host](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_host.html)