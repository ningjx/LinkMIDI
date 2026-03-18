# Network MIDI 2.0 优化实现指南

## 一、优化概述

本项目根据以下建议进行了全面优化：

### 优化建议（已全部实施）

1. ✅ **将 mDNS 拆分为独立模块** - 提高代码可维护性和可复用性
2. ✅ **添加 WiFi 凭据的构建时配置** - 支持 Kconfig 配置，避免硬编码
3. ✅ **WiFi 初始化添加 NVS 支持** - 实现可靠的 WiFi 凭据存储

---

## 二、主要改进

### 2.1 mDNS 发现模块独立化

**工作内容**：
- 从 `network_midi2.c` 中提取所有 mDNS 相关代码
- 创建独立的 `mdns_discovery.{h,c}` 模块
- 定义清晰的模块 API 接口

**核心 API**：
```c
// 初始化 mDNS 发现
mdns_discovery_context_t* mdns_discovery_init(
    const char* device_name,
    const char* product_id,
    uint16_t port);

// 启动/停止服务
bool mdns_discovery_start(mdns_discovery_context_t* ctx);
void mdns_discovery_stop(mdns_discovery_context_t* ctx);

// 查询和获取设备
bool mdns_discovery_send_query(mdns_discovery_context_t* ctx);
bool mdns_discovery_get_device(mdns_discovery_context_t* ctx, int index, ...);
int mdns_discovery_get_device_count(mdns_discovery_context_t* ctx);
```

**优势**：
- 🎯 **单一职责** - mDNS 模块只负责发现功能
- 🔧 **可测试性** - 可独立编写单元测试
- 📦 **可复用性** - 可用于其他 UDP 网络应用
- 🚀 **可扩展性** - 后续可集成官方 mDNS 库

---

### 2.2 WiFi 管理模块

**工作内容**：
- 创建 `wifi_manager.{h,c}` 模块
- 集成 NVS（非易失存储）初始化
- 实现 WiFi 连接状态管理
- 添加自动重连机制

**核心功能**：
```c
// WiFi 初始化（包括 NVS）
bool wifi_manager_init(void);

// 连接到配置的 WiFi
bool wifi_manager_connect(void);

// 等待连接建立（支持超时）
bool wifi_manager_wait_for_connection(uint32_t timeout_ms);

// 检查连接状态
bool wifi_manager_is_connected(void);

// 清理资源
void wifi_manager_deinit(void);
```

**改进详情**：

| 改进项 | 说明 | 代码位置 |
|------|------|---------|
| NVS 初始化 | 存储 WiFi 凭据 | `wifi_manager_init()` |
| 自动恢复 | NVS 损坏时自动修复 | `nvs_flash_erase()` |
| 事件管理 | 使用 FreeRTOS EventGroup | `g_wifi_event_group` |
| 重连机制 | 失败自动重试 | `g_retry_num` 计数 |
| 超时控制 | 设置合理的超时时间 | 10000ms 默认值 |

---

### 2.3 Kconfig 构建时配置

**工作内容**：
- 创建 `main/Kconfig.projbuild`
- 定义 8 个可配置参数
- 支持 `idf.py menuconfig` 配置

**配置参数**：
```
✓ CONFIG_WIFI_SSID              - WiFi 网络名称
✓ CONFIG_WIFI_PASSWORD          - WiFi 密码
✓ CONFIG_WIFI_MAXIMUM_RETRY     - 最大重连次数
✓ CONFIG_WIFI_SCAN_METHOD       - WiFi 扫描方式
✓ CONFIG_WIFI_SORT_METHOD       - WiFi 排序方式
✓ CONFIG_MIDI_DEVICE_NAME       - MIDI 设备名
✓ CONFIG_MIDI_PRODUCT_ID        - MIDI 产品 ID
✓ CONFIG_MIDI_LISTEN_PORT       - MIDI UDP 端口
```

**优势**：
- 🔐 **安全性** - 凭据不再硬编码
- 🛠️ **易配置** - 图形化菜单配置
- 📱 **多部署** - 同一源代码支持多种配置
- 🔄 **可维护** - 集中管理所有参数

---

## 三、代码变更详情

### 3.1 新建文件

#### 📄 `components/network_midi2/include/mdns_discovery.h`
- mDNS 发现模块的公开 API
- 定义发现设备数据结构
- 包含完整的 doxygen 文档

#### 📄 `components/network_midi2/src/mdns_discovery.c`
- mDNS 发现功能实现 (~350 行)
- 包含 DNS 编码/解码
- mDNS 查询和声明包生成
- 发现任务和事件处理

#### 📄 `main/wifi_manager.h`
- WiFi 管理 API 定义
- 包含事件处理机制

#### 📄 `main/wifi_manager.c`
- WiFi + NVS 初始化实现 (~120 行)
- 连接管理和超时控制
- 事件处理器和错误恢复

#### 📄 `main/Kconfig.projbuild`
- 项目配置菜单定义
- 8 个配置参数定义

### 3.2 修改的文件

#### 🔧 `components/network_midi2/CMakeLists.txt`
```diff
  idf_component_register(
      SRCS 
          "src/network_midi2.c"
+         "src/mdns_discovery.c"    ← 新增
      ...
  )
```

#### 🔧 `main/CMakeLists.txt`
```diff
  idf_component_register(
      SRCS "main.c"
+             "wifi_manager.c"      ← 新增
      PRIV_REQUIRES spi_flash esp_wifi esp_netif esp_event
+                   nvs_flash         ← 新增依赖
      ...
  )
```

#### 🔧 `main/main.c`
- 移除 `init_wifi()` 硬编码函数
- 替换为 `wifi_manager_init()` + `wifi_manager_connect()`
- 移除 `wifi_event_handler()` 实现
- 集成 `mdns_discovery` 模块初始化
- 使用 `CONFIG_*` 宏替换硬编码值

**变更前后对比**：
```c
// 之前：硬编码 WiFi 凭据
init_wifi();  // 自定义函数，硬编码凭据

// 之后：使用 WiFi 管理器 + Kconfig
wifi_manager_init();
wifi_manager_connect();
wifi_manager_wait_for_connection(10000);
```

---

## 四、部署和配置流程

### 4.1 获取当前配置

```bash
cd firmware_src
idf.py reconfigure
```

### 4.2 配置参数（使用 menuconfig）

```bash
idf.py menuconfig
```

**菜单导航**：
1. 选择 `"Network MIDI 2.0 Configuration"` 菜单
2. 配置 WiFi 参数：
   - `WiFi SSID` - 您的网络名称
   - `WiFi Password` - 您的网络密码
   - `WiFi Maximum Retry` - 重连次数（默认5）
   - `WiFi Scan Method` - 扫描方式
3. 配置 MIDI 参数：
   - `MIDI Device Name` - 设备显示名称
   - `MIDI Product ID` - 产品标识
   - `MIDI Listen Port` - UDP 端口（默认5507）

### 4.3 编译

```bash
idf.py build
```

### 4.4 刷写

```bash
idf.py -p <PORT> flash
```

例如：
```bash
idf.py -p COM3 flash        # Windows
idf.py -p /dev/ttyUSB0 flash # Linux
```

### 4.5 监控

```bash
idf.py -p <PORT> monitor
```

---

## 五、架构对比

### 优化前的架构

```
┌─────────────────────────┐
│   main.main()           │
├─────────────────────────┤
│ ├─ init_wifi()          │
│ │  └─ 硬编码 WiFi 凭据  │
│ └─ network_midi2        │
│    └─ 包含内嵌 mDNS    │
└─────────────────────────┘
```

**问题**：
- ❌ WiFi 凭据硬编码，需修改源代码
- ❌ mDNS 与 network_midi2 耦合
- ❌ 没有 NVS 初始化
- ❌ 代码难以测试和复用

### 优化后的架构

```
┌──────────────────────────────────┐
│      main.app_main()             │
├──────────────────────────────────┤
│ ┌────────────────────────┐       │
│ │  wifi_manager 模块     │       │
│ │ ┌────────────────────┐ │       │
│ │ │ • NVS 初始化       │ │       │
│ │ │ • WiFi 连接管理    │ │       │
│ │ │ • 事件处理         │ │       │
│ │ │ • 自动重连         │ │       │
│ │ └────────────────────┘ │       │
│ └────────────────────────┘       │
│                                  │
│ ┌────────────────────────┐       │
│ │  mdns_discovery 模块   │       │
│ │ ┌────────────────────┐ │       │
│ │ │ • 设备发现         │ │       │
│ │ │ • 设备声明         │ │       │
│ │ │ • mDNS 查询/响应   │ │       │
│ │ └────────────────────┘ │       │
│ └────────────────────────┘       │
│                                  │
│ ┌────────────────────────┐       │
│ │  network_midi2 模块    │       │
│ │ ┌────────────────────┐ │       │
│ │ │ • 会话管理         │ │       │
│ │ │ • 数据传输         │ │       │
│ │ │ • INV/END/PING     │ │       │
│ │ └────────────────────┘ │       │
│ └────────────────────────┘       │
│                                  │
│ Kconfig 配置参数提供支持         │
└──────────────────────────────────┘
```

**优势**：
- ✅ 模块化设计，职责明确
- ✅ 参数由 Kconfig 提供
- ✅ 支持 NVS 凭据存储
- ✅ 易于测试和扩展
- ✅ 代码复用性高

---

## 六、使用示例

### 6.1 基本启动流程

```c
void app_main(void) {
    // 1. 初始化 WiFi（包括 NVS）
    if (!wifi_manager_init()) {
        ESP_LOGE(TAG, "WiFi init failed");
        return;
    }
    
    // 2. 配置并连接 WiFi
    if (!wifi_manager_connect()) {
        ESP_LOGE(TAG, "WiFi config failed");
        return;
    }
    
    // 3. 等待 WiFi 连接（最多 10 秒）
    if (!wifi_manager_wait_for_connection(10000)) {
        ESP_LOGW(TAG, "WiFi connection timeout");
        // 继续，本地操作可能仍能工作
    }
    
    // 4. 初始化 mDNS 发现
    mdns_discovery_context_t* mdns = mdns_discovery_init(
        CONFIG_MIDI_DEVICE_NAME,
        CONFIG_MIDI_PRODUCT_ID,
        CONFIG_MIDI_LISTEN_PORT);
    
    // 5. 启动 mDNS 发现
    mdns_discovery_start(mdns);
    
    // 6. 初始化 network_midi2
    network_midi2_config_t config = {
        .device_name = CONFIG_MIDI_DEVICE_NAME,
        .product_id = CONFIG_MIDI_PRODUCT_ID,
        .listen_port = CONFIG_MIDI_LISTEN_PORT,
        .mode = MODE_SERVER,
        .enable_discovery = true,
    };
    
    network_midi2_context_t* midi2 = 
        network_midi2_init_with_config(&config);
    
    // 7. 启动 MIDI 2.0 服务
    network_midi2_start(midi2);
    
    // ... 创建应用任务 ...
}
```

### 6.2 查询已发现设备

```c
void discover_devices_task(void* arg) {
    mdns_discovery_context_t* mdns = (mdns_discovery_context_t*)arg;
    
    vTaskDelay(pdMS_TO_TICKS(2000));  // 等待发现
    
    // 发送 mDNS 查询
    mdns_discovery_send_query(mdns);
    
    vTaskDelay(pdMS_TO_TICKS(1000));  // 等待响应
    
    // 获取已发现的设备数量
    int count = mdns_discovery_get_device_count(mdns);
    ESP_LOGI(TAG, "Found %d devices", count);
    
    // 遍历所有设备
    for (int i = 0; i < count; i++) {
        char dev_name[64];
        uint32_t ip_addr;
        uint16_t port;
        
        if (mdns_discovery_get_device(mdns, i, dev_name, &ip_addr, &port)) {
            ESP_LOGI(TAG, "Device %d: %s [IP: %08X:%d]", 
                    i, dev_name, ip_addr, port);
        }
    }
}
```

---

## 七、编译验证清单

- ✅ `cmake` 配置通过
- ✅ 所有头文件正确包含
- ✅ 无编译警告（针对 `-Wall -Wextra`）
- ✅ 所有新模块函数已实现
- ✅ CMakeLists.txt 已更新
- ✅ 依赖项正确指定

---

## 八、测试清单

### 单元测试（建议）

- [ ] `test_wifi_manager_init()` - WiFi 初始化测试
- [ ] `test_wifi_manager_connect()` - 连接测试
- [ ] `test_mdns_discovery_init()` - mDNS 初始化测试
- [ ] `test_mdns_discovery_send_query()` - mDNS 查询测试
- [ ] `test_mdns_discovery_get_device()` - 设备获取测试

### 集成测试

- [ ] WiFi 连接成功
- [ ] mDNS 设备声明生成
- [ ] mDNS 设备发现功能
- [ ] network_midi2 正常启动
- [ ] MIDI 会话建立
- [ ] 数据传输正常

---

## 九、故障排查

| 问题 | 原因 | 解决方案 |
|-----|-----|--------|
| WiFi 连不上 | 凭据错误或无线信号差 | 检查 `idf.py menuconfig` 中的 WiFi 配置 |
| mDNS 查询无结果 | 设备未公布或网络隔离 | 检查 `mdns_discovery_start()` 是否成功 |
| NVS 初始化失败 | flash 损坏 | 使用 `idf.py erase_flash` 恢复 |
| 编译错误 | IDF 版本不兼容 | 更新 ESP-IDF 到最新版本 |

---

## 十、后续优化方向

1. **官方 mDNS 集成** - 迁移到 esp-mDNS 库
2. **蓝牙配置** - 添加蓝牙配置界面
3. **Web 界面** - 提供 Web 配置页面
4. **OTA 更新** - 无线固件更新功能
5. **性能监控** - 添加性能指标收集

---

**生成日期**: 2026-03-18  
**版本**: 1.0  
**状态**: 完成 ✅
