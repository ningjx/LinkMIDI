# Network MIDI 2.0 协议重构计划

## 文档信息
- **创建日期**: 2026-04-01
- **参考规范**: M2-124-UM_v1-0-1_Network-MIDI-2-0-UDP.pdf
- **参考实现**: D:\WorkSpace\MidiBridge (C# 完整实现)
- **测试方式**: ESP32-S3 硬件烧录测试

---

## 一、当前实现状态分析

### 1.1 已完成功能 ✅

| 功能 | 文件 | 状态 |
|------|------|------|
| UDP签名验证 | nm2_protocol.c | ✅ 正确实现 0x4D494449 |
| 命令码定义 | nm2_protocol.h | ✅ 完整定义 |
| INV邀请 | nm2_protocol.c | ✅ 基本实现 |
| INV_ACCEPTED | nm2_protocol.c | ✅ 基本实现 |
| PING/PING_REPLY | nm2_protocol.c | ✅ 基本实现 |
| BYE/BYE_REPLY | nm2_protocol.c | ✅ 基本实现 |
| UMP_DATA | nm2_transport.c | ✅ 基本实现 |
| SESSION_RESET | nm2_session.c | ✅ 已实现 |
| 重传缓冲区结构 | nm2_protocol.h | ✅ 结构已定义 |
| 序列号工具 | nm2_protocol.c | ✅ 已实现 |
| mDNS发现 | nm2_discovery.c | ✅ 基本实现 |
| 重传请求响应框架 | nm2_transport.c | ✅ 基本框架 |

### 1.2 待完善功能

| 功能 | 优先级 | 说明 |
|------|--------|------|
| ~~接收端丢包检测~~ | ~~P0~~ | ✅ 已实现 |
| ~~发送端重传缓冲区集成~~ | ~~P0~~ | ✅ 已实现 |
| ~~NAK错误响应~~ | ~~P1~~ | ✅ 已实现 |
| ~~RETRANSMIT_ERROR处理~~ | ~~P1~~ | ✅ 已实现 |
| ~~完整会话状态机~~ | ~~P1~~ | ✅ 超时处理已实现 |
| INV_PENDING | P2 | 异步邀请响应 |
| 认证机制 | P3 | 后续版本实现 |

---

## 二、重构阶段规划

### 阶段 1: 重传机制完善 (P0 - 必须)

**目标**: 实现完整的丢包检测和重传机制

#### 1.1 发送端重传缓冲区集成
- 文件: `nm2_transport.c`
- 状态: ✅ 已基本实现 (发送UMP时存入缓冲区)
- 待完善: 缓冲区老化清理机制

#### 1.2 接收端丢包检测
- 文件: `nm2_transport.c`, `nm2_session.c`
- 状态: ❌ 未实现
- 任务:
  1. 检测序列号间隙
  2. 记录缺失的序列号
  3. 发送RETRANSMIT_REQUEST

#### 1.3 重传请求处理
- 文件: `nm2_transport.c`
- 状态: ✅ 基本框架已实现
- 待完善: RETRANSMIT_ERROR响应

### 阶段 2: 序列号管理完善 (P0)

**目标**: 完善序列号处理逻辑

- 文件: `nm2_session.c`, `nm2_transport.c`
- 状态: ✅ 基本实现
- 待完善: 丢包统计

### 阶段 3: 会话状态机完善 (P1)

**目标**: 实现完整的状态管理

- 文件: `nm2_session.c`
- 状态: ⚠️ 部分实现
- 待完善: 超时处理、状态转换回调

### 阶段 4: 错误处理 (P1)

**目标**: 提高协议健壮性

#### 4.1 NAK错误响应
- 文件: `nm2_protocol.c`, `nm2_transport.c`
- 状态: ❌ 未实现
- 任务:
  1. 收到未知命令码时发送NAK
  2. 收到格式错误包时发送NAK
  3. 状态不匹配时发送NAK

#### 4.2 RETRANSMIT_ERROR处理
- 文件: `nm2_transport.c`
- 状态: ❌ 未实现
- 任务:
  1. 解析RETRANSMIT_ERROR
  2. 记录重传失败
  3. 触发会话重置或终止

### 阶段 5: 硬件测试验证 (P0)

**目标**: 确保实现正确性

- 任务:
  1. 烧录固件到ESP32-S3硬件
  2. 与MidiBridge进行互操作测试
  3. 验证会话建立/终止流程
  4. 验证数据传输
  5. 验证重传机制

---

## 三、详细实现步骤

### 步骤 1: 接收端丢包检测 (优先级最高)

**修改文件**:
- `components/network_midi2/src/nm2_transport.c`
- `components/network_midi2/src/nm2_session.c`

**实现要点**:
1. 接收UMP时检查序列号: `nm2_protocol_is_sequence_newer(seq, last_received)`
2. 检测间隙: 如果 `seq > expected_seq`，记录缺失序列号
3. 发送RETRANSMIT_REQUEST: 包含缺失的首序列号和数量
4. 更新LastReceivedSeqNum

**参考代码** (MidiBridge CheckSequenceNumber):
```csharp
private void CheckSequenceNumber(ref SessionInfo session, ushort sequenceNumber)
{
    ushort expectedSeq = (ushort)(session.ReceiveSequence + 1);
    
    if (sequenceNumber == expectedSeq) {
        // 正常顺序
        session.ReceiveSequence = sequenceNumber;
    } else if (IsSequenceNewer(sequenceNumber, session.ReceiveSequence)) {
        // 检测丢包
        int lost = CountPacketsLost(session.ReceiveSequence, sequenceNumber);
        session.PacketsLost += lost;
        RequestRetransmitInternal(sessionId, missingSeq);
    }
}
```

### 步骤 2: NAK错误响应

**修改文件**:
- `components/network_midi2/src/nm2_transport.c`

**实现要点**:
1. 收到未知命令码时发送NAK (NM2_NAK_CMD_NOT_SUPPORTED)
2. 收到格式错误包时发送NAK (NM2_NAK_CMD_MALFORMED)
3. 状态不匹配时发送NAK (NM2_NAK_CMD_NOT_EXPECTED)

### 步骤 3: RETRANSMIT_ERROR处理

**修改文件**:
- `components/network_midi2/src/nm2_transport.c`

**实现要点**:
1. 解析RETRANSMIT_ERROR命令
2. 记录重传失败统计
3. 触发SESSION_RESET或BYE

### 步骤 4: 会话状态机完善

**修改文件**:
- `components/network_midi2/src/nm2_session.c`

**实现要点**:
1. 添加邀请超时处理 (30秒)
2. 添加保活超时处理 (3次PING失败)
3. 状态转换事件回调完善

---

## 四、关键参考代码

### 4.1 MidiBridge核心参考

| 文件 | 关键函数 | 参考用途 |
|------|---------|---------|
| `NetworkMidi2Service.cs` | `CheckSequenceNumber()` | 丢包检测逻辑 |
| `NetworkMidi2Service.cs` | `HandleRetransmitRequest()` | 重传响应 |
| `NetworkMidi2Service.cs` | `RequestRetransmitInternal()` | 重传请求发送 |
| `NetworkMidi2Protocol.cs` | `CreateRetransmitRequest()` | 命令构建 |

### 4.2 关键算法

**序列号比较** (处理uint16回绕):
```c
// 已在nm2_protocol.c实现
bool nm2_protocol_is_sequence_newer(uint16_t new_seq, uint16_t old_seq) {
    int16_t diff = (int16_t)(new_seq - old_seq);
    return diff > 0;  // 正数表示newer
}
```

**丢包检测**:
```c
void check_sequence(nm2_session_t* session, uint16_t seq) {
    uint16_t expected = session->last_received_seq + 1;
    if (seq == expected) {
        // 正常顺序
        session->last_received_seq = seq;
    } else if (nm2_protocol_is_sequence_newer(seq, session->last_received_seq)) {
        // 检测到丢包
        int lost = nm2_protocol_sequence_diff(seq, expected);
        // 发送RETRANSMIT_REQUEST
    }
}
```

---

## 五、验证清单

### 5.1 协议符合性
- [x] UDP签名正确 (0x4D494449)
- [x] 所有命令码符合规范
- [x] 包格式符合规范
- [x] 序列号处理正确 (丢包检测已实现)

### 5.2 硬件测试
- [ ] 与MidiBridge建立会话
- [ ] 传输MIDI数据正常
- [ ] 丢包重传机制工作
- [ ] 会话终止流程正常

### 5.3 健壮性
- [x] 处理异常输入 (NAK已实现)
- [x] 处理网络中断 (超时处理已实现)
- [x] 处理协议错误 (NAK已实现)
- [x] 资源正确释放

---

## 六、时间估算

| 阶段 | 工作量 | 优先级 |
|------|--------|--------|
| 阶段1: 重传机制 | 4-6小时 | P0 |
| 阶段2: 序列号管理 | 1-2小时 | P0 |
| 阶段3: 会话状态机 | 2-3小时 | P1 |
| 阶段4: 错误处理 | 2-3小时 | P1 |
| 阶段5: 硬件测试 | 2-4小时 | P0 |
| **总计** | **11-18小时** | - |

---

## 七、变更历史

| 日期 | 版本 | 变更内容 |
|------|------|---------|
| 2026-04-01 | 1.0 | 创建重构计划文档 |
| 2026-04-01 | 1.1 | 完成接收端丢包检测实现 |
| 2026-04-01 | 1.2 | 完成NAK错误响应实现 |
| 2026-04-01 | 1.3 | 完成RETRANSMIT_ERROR处理 |
| 2026-04-01 | 1.4 | 完成会话状态机超时处理 |