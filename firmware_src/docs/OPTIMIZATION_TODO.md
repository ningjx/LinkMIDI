# LinkMIDI 项目优化清单

> 本文档记录项目待优化的项目，按优先级排序，逐一完成。

---

## 优先级说明

- 🔴 **P0** - 核心功能缺失，必须立即处理
- 🟠 **P1** - 架构问题，影响可维护性和扩展性
- 🟡 **P2** - 功能增强，提升用户体验
- 🟢 **P3** - 可选优化，后续迭代处理

---

## 优化清单

### 🔴 P0-01: MIDI 数据路由层

**问题**: USB MIDI 数据未真正转发到网络，`main.c` 中只有 TODO 注释

**目标**: 
- 创建 `midi_router` 组件
- 实现端口注册和路由规则
- 打通 USB → Network 数据通路

**验收标准**:
- [ ] USB MIDI 键盘数据能通过网络发送到 MIDI 2.0 设备
- [ ] 支持基本的路由规则配置

---

### 🔴 P0-02: USB MIDI Host 功能完善

**问题**: 当前实现是简化版本，缺少核心功能
- 设备枚举未实现
- 事件回调为空占位符
- 无实际 MIDI 数据接收

**目标**:
- 实现完整的 USB 设备枚举
- 解析 MIDI 类描述符
- 实现端点数据传输
- 处理热插拔事件

**验收标准**:
- [ ] 能识别并连接 USB MIDI 键盘
- [ ] 能正确接收 MIDI 数据并触发回调
- [ ] 支持设备热插拔

---

### 🟠 P1-01: MIDI 1.0 ↔ UMP 协议转换层 ✅

**问题**: 当前转换逻辑简化，不完全符合 MIDI 2.0 规范

**目标**:
- 创建 `midi_converter` 组件
- 实现 MIDI 1.0 → UMP 转换（符合规范）
- 实现 UMP → MIDI 1.0 转换
- 支持 MIDI 2.0 Channel Voice Messages

**验收标准**:
- [x] 转换结果符合 MIDI 2.0 Protocol Specification
- [x] 支持 MT2 (MIDI 1.0 in UMP) 和 MT4 (MIDI 2.0)

**实施内容** (2026-03-18):
- 创建 `components/common/include/midi_converter.h` - 协议转换接口
- 创建 `components/common/src/midi_converter.c` - 协议转换实现
- 基于 MIDI 2.0 规范 (M2-104-UM v1.1.2) 实现
- 支持 UMP Message Types: MT=0x1 (System), MT=0x2 (MIDI 1.0 CV), MT=0x4 (MIDI 2.0 CV)
- 实现完整的值缩放函数 (7-bit ↔ 16-bit, 7-bit ↔ 32-bit, 14-bit ↔ 32-bit)
- 更新 `nm2_transport.c` 使用新的转换器

---

### 🟠 P1-02: 事件驱动架构 ✅

**问题**: 模块间通过全局变量和直接回调耦合

**目标**:
- 创建 `event_bus` 模块
- 定义统一的事件类型
- 各模块通过事件通信

**验收标准**:
- [x] 移除 main.c 中的全局变量
- [x] 各模块通过事件总线通信
- [x] 支持事件订阅/发布机制

**实施内容** (2026-03-18):
- 创建 `components/common/include/event_bus.h` - 事件总线接口
- 创建 `components/common/src/event_bus.c` - 事件总线实现
- 创建 `main/app_core.h` - 应用核心模块接口
- 创建 `main/app_core.c` - 应用核心模块实现
- 创建 `main/app_config.h` - 应用配置定义
- 重构 `main/main.c` - 使用事件驱动架构
- 支持的事件类型: MIDI数据、USB设备、会话、WiFi、发现等
- 实现了 USB → Network 数据转发功能（通过事件触发）

---

### 🟠 P1-03: network_midi2 模块拆分 ✅

**问题**: 模块职责过重，包含传输、会话、发现等多个职责

**目标**:
- 拆分为 `nm2_session`、`nm2_transport`、`nm2_discovery` 子模块
- 合并 `mdns_discovery` 功能（删除重复代码）
- 使用 ESP-IDF 内置 mDNS 组件

**验收标准**:
- [x] 各子模块职责单一
- [x] 无功能重复代码
- [x] API 保持向后兼容

**实施内容** (2026-03-18):
- 创建 `nm2_session.h/c` - 会话生命周期管理
- 创建 `nm2_transport.h/c` - UDP 数据传输
- 创建 `nm2_discovery.h/c` - mDNS 设备发现
- 使用 ESP-IDF 官方 mDNS 托管组件 (espressif/mdns)
- 修复 ESP-IDF 5.x API 变更兼容性问题

---

### 🟠 P1-04: 错误处理标准化 ✅

**问题**: 错误码不统一，日志记录不一致

**目标**:
- 定义统一的错误码枚举
- 创建 `common` 公共模块
- 统一返回值类型和错误处理宏

**验收标准**:
- [x] 所有公共 API 使用统一错误码
- [x] 提供错误码转字符串函数

**实施内容** (2026-03-18):
- 创建 `components/common/` 模块
- 定义 `midi_error_t` 错误码枚举（26个错误码覆盖所有场景）
- 实现 `midi_error_str()` 转字符串函数
- 创建 `MIDI_RETURN_*` 宏简化错误处理
- 更新 `network_midi2` 和 `usb_midi_host` 使用统一错误码
- 保持向后兼容（`bool` 函数保留）

---

### 🟡 P2-01: 配置持久化

**问题**: WiFi 和 MIDI 配置无法保存，重启后丢失

**目标**:
- 创建 `config_manager` 组件
- 使用 NVS 存储配置
- 支持配置导入/导出

**验收标准**:
- [ ] WiFi 凭证可持久化保存
- [ ] 设备名称、端口等配置可保存
- [ ] 支持恢复出厂设置

---

### 🟡 P2-02: 双向数据流（Network → USB）

**问题**: 当前只支持 USB → Network 单向

**目标**:
- 实现网络接收的 MIDI 数据转发到 USB
- 需要 USB MIDI Device 模式支持（或保持 Host 模式的发送功能）

**验收标准**:
- [ ] 网络 MIDI 数据能发送到 USB MIDI 设备

---

### 🟡 P2-03: WiFi Manager 增强

**问题**: 仅支持 STA 模式，无配网功能

**目标**:
- 支持 Smart Config 或 SoftAP 配网
- WiFi 状态事件通知
- 断线自动重连优化

**验收标准**:
- [ ] 支持首次配网流程
- [ ] WiFi 断线能自动恢复

---

### 🟡 P2-04: OTA 固件更新

**问题**: 无远程更新能力

**目标**:
- 配置 OTA 分区
- 实现基础 OTA 更新功能
- 支持回滚保护

**验收标准**:
- [ ] 可通过 Web 或 API 触发 OTA
- [ ] 更新失败能自动回滚

---

### 🟢 P3-01: Web 配置界面

**目标**: 提供网页配置界面

**验收标准**:
- [ ] 可通过网页配置 WiFi
- [ ] 可查看设备状态

---

### 🟢 P3-02: BLE MIDI 支持

**目标**: 支持蓝牙 MIDI 连接

**验收标准**:
- [ ] 支持 BLE MIDI GATT Profile
- [ ] 可与 iOS/macOS 设备连接

---

### 🟢 P3-03: 单元测试框架

**目标**: 添加关键模块的单元测试

**验收标准**:
- [ ] MIDI 转换逻辑有测试覆盖
- [ ] 路由逻辑有测试覆盖

---

## 进度跟踪

| 编号 | 任务 | 状态 | 完成日期 |
|------|------|------|----------|
| P0-01 | MIDI 路由层 | ⬜ 待开始 | - |
| P0-02 | USB MIDI Host 完善 | ⬜ 待开始 | - |
| P1-01 | 协议转换层 | ⬜ 待开始 | - |
| P1-02 | 事件驱动架构 | ⬜ 待开始 | - |
| P1-03 | network_midi2 拆分 | ⬜ 待开始 | - |
| P1-04 | 错误处理标准化 | ⬜ 待开始 | - |
| P2-01 | 配置持久化 | ⬜ 待开始 | - |
| P2-02 | 双向数据流 | ⬜ 待开始 | - |
| P2-03 | WiFi 增强 | ⬜ 待开始 | - |
| P2-04 | OTA 更新 | ⬜ 待开始 | - |
| P3-01 | Web 界面 | ⬜ 待开始 | - |
| P3-02 | BLE MIDI | ⬜ 待开始 | - |
| P3-03 | 单元测试 | ⬜ 待开始 | - |

---

## 开发顺序建议

```
Phase 1 (核心功能):
  P0-01 → P0-02 → P1-01

Phase 2 (架构重构):
  P1-02 → P1-04 → P1-03

Phase 3 (功能增强):
  P2-01 → P2-03 → P2-02 → P2-04

Phase 4 (体验优化):
  P3-01 → P3-02 → P3-03
```

---

## 相关文档

- [系统架构](ARCHITECTURE.md)
- [API 参考](API.md)
- [MIDI 2.0 Specification](https://midi.org/specifications/midi2)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/)