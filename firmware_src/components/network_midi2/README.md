# Network MIDI 2.0 Component

ESP32 上的 Network MIDI 2.0 (UDP) 协议实现。

## 功能

- mDNS/DNS-SD 设备发现
- 会话管理 (INV/END/PING)
- UMP 数据传输
- 支持 Client/Server/Peer 模式

## API 快速参考

```c
#include "network_midi2.h"
#include "mdns_discovery.h"

// 初始化
network_midi2_context_t* ctx = network_midi2_init("Device", "Product", 5507);
network_midi2_start(ctx);

// 发送 MIDI
network_midi2_send_note_on(ctx, 60, 100, 0);  // note, velocity, channel

// 接收 MIDI (设置回调)
network_midi2_set_midi_rx_callback(ctx, my_callback);

// 清理
network_midi2_stop(ctx);
network_midi2_deinit(ctx);
```

## 配置

```c
network_midi2_config_t config = {
    .device_name = "MyDevice",
    .product_id = "ESP32S3",
    .listen_port = 5507,
    .mode = MODE_PEER,            // MODE_CLIENT / MODE_SERVER / MODE_PEER
    .enable_discovery = true,
    .midi_rx_callback = on_midi,
    .ump_rx_callback = on_ump,
};
```

## 文件

```
components/network_midi2/
├── include/
│   ├── network_midi2.h      # 主 API
│   └── mdns_discovery.h     # mDNS 发现 API
├── src/
│   ├── network_midi2.c      # 会话和数据传输
│   └── mdns_discovery.c     # mDNS 实现
└── CMakeLists.txt
```

## 依赖

- ESP-IDF >= 5.0
- lwIP
- mDNS (可选)

## 参考资料

- [MIDI 2.0 Network (UDP) Specification](https://midi.org/network-midi-2-0)
- [mDNS RFC 6762](https://tools.ietf.org/html/rfc6762)