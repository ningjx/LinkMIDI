# USB MIDI Host Driver

ESP32 USB 主机 MIDI 键盘驱动程序

## 功能

- **USB 主机模式** - 将 ESP32 作为 USB 主机，支持连接 MIDI 键盘和控制器
- **自动设备发现** - 自动检测和枚举连接的 MIDI 设备
- **热插拔支持** - 设备连接/断开时自动处理
- **MIDI 数据接收** - 实时接收来自 USB 设备的 MIDI 数据
- **多设备支持** - 最多支持 4 个并发 MIDI 设备
- **事件回调** - 设备连接、断开、数据接收的回调机制
- **线程安全** - 使用互斥锁保护共享资源

## 硬件要求

- ESP32 或 ESP32-S3（具有 USB OTG 功能）
- USB 电缆和配接器（连接 MIDI 键盘）
- 可选：USB 总线电源（用于为 MIDI 设备供电）

## API 文档

### 初始化

```c
usb_midi_host_context_t* usb_midi_host_init(
    const usb_midi_host_config_t* config);
```

初始化 USB MIDI 主机驱动程序。

**参数:**
- `config` - 配置结构体，包含回调函数

**返回值:** 上下文句柄或 NULL（出错时）

### 启动/停止

```c
bool usb_midi_host_start(usb_midi_host_context_t* ctx);
void usb_midi_host_stop(usb_midi_host_context_t* ctx);
```

启动或停止 USB MIDI 主机服务。

### 设备信息

```c
uint8_t usb_midi_host_get_device_count(usb_midi_host_context_t* ctx);

bool usb_midi_host_get_device_info(usb_midi_host_context_t* ctx,
                                   uint8_t device_index,
                                   usb_midi_device_t* device_info);

bool usb_midi_host_is_device_connected(usb_midi_host_context_t* ctx,
                                       uint8_t device_index);
```

获取连接的 MIDI 设备信息。

### 回调函数

```c
// MIDI 数据接收回调
typedef void (*usb_midi_rx_callback_t)(uint8_t device_index, 
                                        const uint8_t* data, 
                                        uint16_t length);

// 设备连接回调
typedef void (*usb_midi_device_connected_callback_t)(
    uint8_t device_index,
    const usb_midi_device_t* device_info);

// 设备断开回调
typedef void (*usb_midi_device_disconnected_callback_t)(uint8_t device_index);
```

## 使用示例

### 基本使用

```c
// 定义回调函数
void on_midi_data(uint8_t device_index, const uint8_t* data, uint16_t length) {
    ESP_LOGI("MIDI", "Device %d: %d bytes", device_index, length);
}

void on_device_connected(uint8_t device_index, const usb_midi_device_t* info) {
    ESP_LOGI("MIDI", "Device connected: %s %s", 
             info->manufacturer, info->product_name);
}

void on_device_disconnected(uint8_t device_index) {
    ESP_LOGI("MIDI", "Device %d disconnected", device_index);
}

// 初始化
usb_midi_host_config_t config = {
    .midi_rx_callback = on_midi_data,
    .device_connected_callback = on_device_connected,
    .device_disconnected_callback = on_device_disconnected,
};

usb_midi_host_context_t* midi_host = usb_midi_host_init(&config);

// 启动
usb_midi_host_start(midi_host);

// 等待设备连接...
vTaskDelay(pdMS_TO_TICKS(2000));

// 查询连接的设备
uint8_t count = usb_midi_host_get_device_count(midi_host);
for (uint8_t i = 0; i < count; i++) {
    usb_midi_device_t info;
    if (usb_midi_host_get_device_info(midi_host, i, &info)) {
        printf("Device %d: %s (VID: 0x%04X, PID: 0x%04X)\n",
               i, info.product_name, info.vendor_id, info.product_id);
    }
}

// 清理
usb_midi_host_stop(midi_host);
usb_midi_host_deinit(midi_host);
```

## 集成步骤

1. 在 `main/CMakeLists.txt` 中添加 `usb_midi_host` 依赖
2. 在应用代码中包含 `<usb_midi_host.h>`
3. 配置 USB GPIO（详见下文）
4. 实现回调函数并初始化驱动程序

## USB GPIO 配置

对于 ESP32-S3，需要配置以下 GPIO：

```c
// 在应用初始化中
const usb_iopin_defs_t usb_io_conf = {
    .usb_dn_gpio_num = GPIO_NUM_19,  // USB D- 引脚
    .usb_dp_gpio_num = GPIO_NUM_20,  // USB D+ 引脚
    .gpio_iomux_opt = true,
};

usb_new_phy_device(USB_PHY_DPLUS_DBUS_GPIO, &usb_io_conf, NULL);
```

各 ESP32 变体的 GPIO 引脚可能不同，请参考硬件文档。

## 限制

- 最多支持 4 个并发 MIDI 设备
- 事件队列大小为 10（可配置）
- 字符串描述符最大 128 字节
- 每个设备分配 4KB 栈用于数据接收任务

## 线程安全

所有公开 API 都是线程安全的。内部使用互斥锁保护设备列表。

## 日志

可通过设置日志级别查看详细的 USB 和 MIDI 处理日志：

```
idf.py menuconfig
-> Component config -> Log level -> USB MIDI Host -> Debug
```

## 后续功能

- [ ] 支持主观 MIDI 输出（USB MIDI OUT）
- [ ] UMP (Universal MIDI Packet) 支持
- [ ] 设备特定的控制接口支持
- [ ] 电源管理和节能模式

## 技术参考

- [USB MIDI 规范](https://www.usb.org/document-library/usb-audio-specification)
- [MIDI 1.0 规范](https://www.midi.org/)
- [ESP-IDF USB 主机](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_host.html)
