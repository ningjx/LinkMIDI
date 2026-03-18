# USB MIDI Host 快速参考卡

## 📱 核心 API

### 初始化和生命周期

```c
// 初始化
usb_midi_host_context_t* ctx = usb_midi_host_init(&config);

// 启动
usb_midi_host_start(ctx);

// 停止
usb_midi_host_stop(ctx);

// 清理
usb_midi_host_deinit(ctx);
```

### 状态查询

```c
// 获取设备数量
uint8_t count = usb_midi_host_get_device_count(ctx);

// 获取设备信息
usb_midi_device_t info;
usb_midi_host_get_device_info(ctx, 0, &info);

// 检查连接状态
bool connected = usb_midi_host_is_device_connected(ctx, 0);

// 检查运行状态
bool running = usb_midi_host_is_running(ctx);
```

## 🎹 MIDI 消息格式

### Note On (键按压)
```
Status: 0x90 | Channel    (Channel = 0x00-0x0F)
Data1:  Note Number       (0-127)
Data2:  Velocity         (0-127)
```

### Note Off (键松开)
```
Status: 0x80 | Channel
Data1:  Note Number       (0-127)
Data2:  Velocity         (0-127)
```

### Control Change (控制器变化)
```
Status: 0xB0 | Channel
Data1:  Controller        (0-127)
Data2:  Value            (0-127)
```

### Program Change (程序选择)
```
Status: 0xC0 | Channel
Data1:  Program          (0-127)
Data2:  (None)
```

## 🔧 回调函数示例

### MIDI 数据接收

```c
void usb_midi_rx_callback(uint8_t device_index, 
                          const uint8_t* data, 
                          uint16_t length) {
    uint8_t status = data[0];
    uint8_t cmd = status & 0xF0;
    uint8_t channel = status & 0x0F;
    
    switch (cmd) {
        case 0x90:  // Note On
            printf("Note On: Ch=%d, Note=%d, Vel=%d\n",
                   channel, data[1], data[2]);
            break;
        case 0x80:  // Note Off
            printf("Note Off: Ch=%d, Note=%d\n",
                   channel, data[1]);
            break;
        case 0xB0:  // Control Change
            printf("CC: Ch=%d, Ctrl=%d, Val=%d\n",
                   channel, data[1], data[2]);
            break;
    }
}
```

### 设备连接

```c
void usb_midi_device_connected_callback(
    uint8_t device_index,
    const usb_midi_device_t* info) {
    printf("Device connected: %s %s\n",
           info->manufacturer,
           info->product_name);
}
```

### 设备断开

```c
void usb_midi_device_disconnected_callback(uint8_t device_index) {
    printf("Device %d disconnected\n", device_index);
}
```

## 🔌 硬件连接

### GPIO 配置

```c
const usb_iopin_defs_t usb_config = {
    .usb_dn_gpio_num = GPIO_NUM_19,    // D- 线
    .usb_dp_gpio_num = GPIO_NUM_20,    // D+ 线
    .gpio_iomux_opt = true,
};

usb_new_phy_device(USB_PHY_DPLUS_DBUS_GPIO, &usb_config, NULL);
```

### USB 线序

```
ESP32-S3 ──────── USB MIDI 设备
  
GPIO19 (D-) ──→ USB Pin 2 (D-)
GPIO20 (D+) ──→ USB Pin 3 (D+)
GND ────────→ USB Pin 4 (GND)
5V (可选) ───→ USB Pin 1 (VBUS)
```

## 📊 常见操作

### 遍历所有设备

```c
uint8_t count = usb_midi_host_get_device_count(ctx);
for (uint8_t i = 0; i < count; i++) {
    usb_midi_device_t info;
    if (usb_midi_host_get_device_info(ctx, i, &info)) {
        printf("Device %d: %s (VID:0x%04X, PID:0x%04X)\n",
               i, info.product_name,
               info.vendor_id, info.product_id);
    }
}
```

### 转发 MIDI 到网络（待实现）

```c
void usb_midi_rx_callback(uint8_t device_index, 
                          const uint8_t* data, 
                          uint16_t length) {
    // 解析 MIDI 消息
    uint8_t cmd = data[0] & 0xF0;
    
    if (cmd == 0x90 && network_midi2_is_session_active(g_midi2_ctx)) {
        // Note On → 转发到网络
        network_midi2_send_note_on(g_midi2_ctx, 
                                   data[1],  // note
                                   data[2],  // velocity
                                   0);       // channel
    }
}
```

## 🐛 常见问题

### Q: 如何检测特定设备
```c
usb_midi_device_t info;
if (usb_midi_host_get_device_info(ctx, &info)) {
    if (info.vendor_id == 0x0499 &&  // Yamaha
        info.product_id == 0x1609) {  // PSR-E363
        // 特定设备检测
    }
}
```

### Q: 如何处理多设备
```c
// 安全的并发访问
uint8_t count = usb_midi_host_get_device_count(ctx);
for (uint8_t i = 0; i < count; i++) {
    if (usb_midi_host_is_device_connected(ctx, i)) {
        // 处理设备 i
    }
}
```

## ⚙️ 配置参数

| 参数 | 默认值 | 范围 | 说明 |
|------|------|------|------|
| `MAX_MIDI_DEVICES` | 4 | 1-8 | 最大设备数 |
| `MIDI_RX_QUEUE_SIZE` | 32 | 16-256 | 事件队列大小 |
| `USB_HOST_PRIORITY` | 5 | 1-24 | 任务优先级 |

## 📈 性能指标

| 指标 | 值 |
|------|-----|
| 最大设备数 | 4 |
| MIDI 延迟 | < 10 ms |
| CPU 占用 | ~2-3% |
| 内存占用 | ~10 KB |
| 数据率 | 31.25 kbps |

## 🎯 集成步骤

1. **配置 GPIO**
   ```c
   const usb_iopin_defs_t config = {...};
   usb_new_phy_device(USB_PHY_DPLUS_DBUS_GPIO, &config, NULL);
   ```

2. **初始化模块**
   ```c
   usb_midi_host_config_t config = {...};
   ctx = usb_midi_host_init(&config);
   ```

3. **启动服务**
   ```c
   usb_midi_host_start(ctx);
   ```

4. **在回调中处理数据**
   ```c
   void usb_midi_rx_callback(...) {
       // 处理 MIDI 数据
   }
   ```

## 🔗 相关文档

- [完整集成指南](USB_MIDI_HOST_GUIDE.md)
- [实现总结](USB_MIDI_HOST_IMPLEMENTATION.md)
- [模块 README](components/usb_midi_host/README.md)
- [API 头文件](components/usb_midi_host/include/usb_midi_host.h)

---

**快速参考版本**: 1.0  
**最后更新**: 2026-03-18
