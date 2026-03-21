# Network MIDI 2.0 协议符合性分析与重构方案

## 文档信息
- **创建日期**: 2026-03-21
- **参考规范**: M2-124-UM_v1-0-1_Network-MIDI-2-0-UDP.pdf
- **参考实现**: D:\WorkSpace\MidiBridge\Test (C# 已验证功能正常)

---

## 一、协议概述

Network MIDI 2.0 (NM2) 是 MIDI 协会定义的基于 UDP 的 MIDI 2.0 网络传输协议，主要包含三个部分：

1. **发现 (Discovery)** - 使用 mDNS/DNS-SD 发现网络上的 MIDI 2.0 设备
2. **会话管理 (Session Management)** - 建立和维护设备间的连接
3. **数据传输 (Data Transmission)** - 传输 UMP (Universal MIDI Packets) 数据

---

## 二、当前实现与规范的差异分析

### 2.1 UDP 包格式

#### 规范要求
```
+----------------+----------------+----------------+----------------+
|  Signature (0x4D494449 = "MIDI") - 4 bytes                    |
+----------------+----------------+----------------+----------------+
|  Command Packet 1                                              |
|  ...                                                           |
+----------------+----------------+----------------+----------------+
|  Command Packet N                                              |
+----------------+----------------+----------------+----------------+
```

每个 UDP 包必须以 4 字节签名 `0x4D494449` (ASCII: "MIDI") 开头，后跟一个或多个命令包。

#### 当前实现问题
- ❌ **未实现签名**: `network_midi2.c` 和 `nm2_transport.c` 中发送的数据包没有包含 "MIDI" 签名
- ❌ **无法与标准实现互操作**: 缺少签名会导致其他标准实现拒绝接收数据包

### 2.2 命令包格式

#### 规范要求
```
+--------+--------+--------+--------+--------+--------+--------+--------+
| CmdCode|PayLen  |Specific1|Specific2|      Payload (PayLen * 4 bytes) |
+--------+--------+--------+--------+--------+--------+--------+--------+
```

- **CmdCode (1 byte)**: 命令码
- **PayLen (1 byte)**: 负载长度（以 4 字节字为单位）
- **Specific1 (1 byte)**: 命令特定字段 1
- **Specific2 (1 byte)**: 命令特定字段 2
- **Payload**: 可变长度负载

#### 当前实现问题
- ⚠️ **格式不一致**: 不同模块使用不同的包格式
- ❌ **缺少统一的包构建/解析函数**

### 2.3 命令码对比

| 功能 | 规范命令码 | 当前实现 | 状态 |
|------|-----------|---------|------|
| **会话管理** |
| INV (邀请) | 0x01 | 0x01 | ✅ 正确 |
| INV_WITH_AUTH (带认证邀请) | 0x02 | - | ❌ 未实现 |
| INV_WITH_USER_AUTH (带用户认证邀请) | 0x03 | - | ❌ 未实现 |
| INV_ACCEPTED (邀请接受) | 0x10 | 0x02 | ❌ 错误 |
| INV_PENDING (邀请等待) | 0x11 | - | ❌ 未实现 |
| INV_AUTH_REQUIRED (需要认证) | 0x12 | - | ❌ 未实现 |
| INV_USER_AUTH_REQUIRED (需要用户认证) | 0x13 | - | ❌ 未实现 |
| **心跳与维护** |
| PING | 0x20 | 0x00 | ❌ 错误 |
| PING_REPLY (PONG) | 0x21 | - | ❌ 未实现 |
| **重传机制** |
| RETRANSMIT_REQUEST | 0x80 | - | ❌ 未实现 |
| RETRANSMIT_ERROR | 0x81 | - | ❌ 未实现 |
| SESSION_RESET | 0x82 | - | ❌ 未实现 |
| SESSION_RESET_REPLY | 0x83 | - | ❌ 未实现 |
| **错误处理** |
| NAK (否定确认) | 0x8F | - | ❌ 未实现 |
| **会话终止** |
| BYE | 0xF0 | 0x04 | ❌ 错误 |
| BYE_REPLY | 0xF1 | - | ❌ 未实现 |
| **数据传输** |
| UMP Data | 0xFF | 0x10 | ❌ 错误 |

### 2.4 会话管理功能

#### 规范要求的会话状态
```
IDLE -> INVITING -> PENDING -> ESTABLISHED -> TERMINATING -> IDLE
```

#### 当前实现问题

| 功能 | 规范要求 | 当前实现 | 状态 |
|------|---------|---------|------|
| 基本邀请流程 | INV -> INV_ACCEPTED | INV -> OK | ⚠️ 命令码错误 |
| 邀请拒绝 | INV -> INV_REJECTED (NO) | NO (0x03) | ⚠️ 规范无此命令 |
| 认证邀请 | INV_WITH_AUTH | - | ❌ 未实现 |
| 用户认证邀请 | INV_WITH_USER_AUTH | - | ❌ 未实现 |
| 等待状态 | INV_PENDING | - | ❌ 未实现 |
| 会话重置 | SESSION_RESET | - | ❌ 未实现 |
| 正常终止 | BYE + BYE_REPLY | BYE only | ⚠️ 缺少回复 |

### 2.5 认证机制

#### 规范要求
1. **共享密钥认证**:
   - 服务端发送 INV_AUTH_REQUIRED + cryptoNonce (16字节)
   - 客户端发送 INV_WITH_AUTH + SHA256(nonce + sharedSecret)

2. **用户名/密码认证**:
   - 服务端发送 INV_USER_AUTH_REQUIRED + cryptoNonce
   - 客户端发送 INV_WITH_USER_AUTH + SHA256(nonce + username + password)

#### 当前实现
- ❌ **完全未实现认证机制**

### 2.6 重传机制

#### 规范要求
- 发送方维护重传缓冲区
- 接收方检测丢包并发送 RETRANSMIT_REQUEST
- 发送方重传或发送 RETRANSMIT_ERROR

#### 当前实现
- ❌ **完全未实现重传机制**
- ❌ **无序列号跟踪**
- ❌ **无丢包检测**

### 2.7 数据传输

#### 规范要求
```
UMP Data Command (0xFF):
+--------+--------+--------+--------+
|  0xFF  | PayLen | SeqHi  | SeqLo  |
+--------+--------+--------+--------+
|        UMP Data (PayLen * 4 bytes)       |
+--------+--------+--------+--------+
```

- 序列号用于重传和丢包检测
- 序列号应连续递增

#### 当前实现问题
- ❌ **命令码错误**: 使用 0x10 而非 0xFF
- ⚠️ **序列号处理不完整**: 缺少接收端序列号验证

### 2.8 mDNS 发现

#### 规范要求
- 服务类型: `_midi2._udp.local`
- TXT 记录应包含: `productInstanceId`

#### 当前实现
- ✅ 服务类型正确
- ⚠️ TXT 记录实现不完整

---

## 三、详细功能清单

### 3.1 已实现功能 ✅

| 功能 | 实现位置 | 备注 |
|------|---------|------|
| UDP Socket 创建 | `network_midi2.c` | 基本功能正常 |
| mDNS 服务注册 | `nm2_discovery.c` | 使用 ESP-IDF mDNS 组件 |
| mDNS 查询 | `nm2_discovery.c` | 基本功能正常 |
| 基本邀请发送 | `nm2_session.c` | 命令码正确，格式需修正 |
| UMP 数据发送 | `nm2_transport.c` | 命令码需修正 |
| MIDI 1.0 到 UMP 转换 | `midi_converter.c` | 需验证正确性 |

### 3.2 部分实现功能 ⚠️

| 功能 | 当前状态 | 需要修改 |
|------|---------|---------|
| 邀请接受 | 命令码错误 (0x02→0x10) | 修改命令码 |
| 邀请拒绝 | 使用非标准命令 | 改用 INV_REJECTED 或直接关闭 |
| 会话终止 | 缺少 BYE_REPLY | 添加回复处理 |
| PING | 命令码错误 (0x00→0x20) | 修改命令码 |
| 序列号管理 | 仅发送端递增 | 添加接收端验证 |

### 3.3 未实现功能 ❌

| 功能 | 优先级 | 说明 |
|------|--------|------|
| UDP 签名 | **高** | 必须实现才能互操作 |
| 正确的命令码 | **高** | 必须修改 |
| 认证机制 | 中 | 共享密钥和用户认证 |
| 重传机制 | 中 | 丢包恢复 |
| 会话重置 | 中 | 序列号重置 |
| NAK 错误响应 | 中 | 错误处理 |
| INV_PENDING | 低 | 用户体验优化 |
| 完整的 TXT 记录 | 低 | 设备信息展示 |

---

## 四、重构方案

### 4.1 架构设计

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│  (MIDI Router, USB Host, etc.)                              │
├─────────────────────────────────────────────────────────────┤
│                    network_midi2.h API                       │
├──────────────┬──────────────┬──────────────┬───────────────┤
│ nm2_discovery│ nm2_session  │ nm2_transport│ nm2_protocol  │
│   (mDNS)     │ (Session)    │ (Data TX/RX) │ (Packet Fmt)  │
├──────────────┴──────────────┴──────────────┴───────────────┤
│                    UDP Socket Layer                          │
└─────────────────────────────────────────────────────────────┘
```

### 4.2 新增模块

#### 4.2.1 `nm2_protocol.h/c` - 协议格式处理

```c
#pragma once

#include <stdint.h>
#include <stdbool.h>

// UDP 包签名
#define NM2_SIGNATURE 0x4D494449  // "MIDI"

// 命令码定义 (符合规范)
typedef enum {
    NM2_CMD_INV                    = 0x01,  // 邀请
    NM2_CMD_INV_WITH_AUTH          = 0x02,  // 带认证邀请
    NM2_CMD_INV_WITH_USER_AUTH     = 0x03,  // 带用户认证邀请
    NM2_CMD_INV_ACCEPTED           = 0x10,  // 邀请接受
    NM2_CMD_INV_PENDING            = 0x11,  // 邀请等待
    NM2_CMD_INV_AUTH_REQUIRED      = 0x12,  // 需要认证
    NM2_CMD_INV_USER_AUTH_REQUIRED = 0x13,  // 需要用户认证
    NM2_CMD_PING                   = 0x20,  // 心跳请求
    NM2_CMD_PING_REPLY             = 0x21,  // 心跳回复
    NM2_CMD_RETRANSMIT_REQUEST     = 0x80,  // 重传请求
    NM2_CMD_RETRANSMIT_ERROR       = 0x81,  // 重传错误
    NM2_CMD_SESSION_RESET          = 0x82,  // 会话重置
    NM2_CMD_SESSION_RESET_REPLY    = 0x83,  // 会话重置回复
    NM2_CMD_NAK                    = 0x8F,  // 否定确认
    NM2_CMD_BYE                    = 0xF0,  // 会话终止
    NM2_CMD_BYE_REPLY              = 0xF1,  // 会话终止回复
    NM2_CMD_UMP_DATA               = 0xFF,  // UMP 数据
} nm2_command_code_t;

// BYE 原因
typedef enum {
    NM2_BYE_UNKNOWN                  = 0x00,
    NM2_BYE_USER_TERMINATED          = 0x01,
    NM2_BYE_POWER_DOWN               = 0x02,
    NM2_BYE_TOO_MANY_MISSING_PACKETS = 0x03,
    NM2_BYE_TIMEOUT                  = 0x04,
    NM2_BYE_SESSION_NOT_ESTABLISHED  = 0x05,
    NM2_BYE_NO_PENDING_SESSION       = 0x06,
    NM2_BYE_PROTOCOL_ERROR           = 0x07,
    NM2_BYE_INV_TOO_MANY_SESSIONS    = 0x40,
    NM2_BYE_INV_AUTH_REJECTED        = 0x41,
    NM2_BYE_INV_REJECTED_BY_USER     = 0x42,
    NM2_BYE_AUTH_FAILED              = 0x43,
    NM2_BYE_USERNAME_NOT_FOUND       = 0x44,
    NM2_BYE_NO_MATCHING_AUTH_METHOD  = 0x45,
    NM2_BYE_INV_CANCELED             = 0x80,
} nm2_bye_reason_t;

// NAK 原因
typedef enum {
    NM2_NAK_OTHER               = 0x00,
    NM2_NAK_CMD_NOT_SUPPORTED   = 0x01,
    NM2_NAK_CMD_NOT_EXPECTED    = 0x02,
    NM2_NAK_CMD_MALFORMED       = 0x03,
    NM2_NAK_BAD_PING_REPLY      = 0x20,
} nm2_nak_reason_t;

// 邀请能力标志
typedef enum {
    NM2_CAP_NONE           = 0x00,
    NM2_CAP_SUPPORTS_AUTH  = 0x01,
    NM2_CAP_SUPPORTS_USER  = 0x02,
    NM2_CAP_ALL            = 0x03,
} nm2_capabilities_t;

// 命令包结构
typedef struct {
    nm2_command_code_t command;
    uint8_t payload_words;
    uint8_t specific1;
    uint8_t specific2;
    const uint8_t* payload;
    uint16_t payload_len;
} nm2_command_packet_t;

// UDP 包构建
int nm2_build_udp_packet(uint8_t* buffer, int max_len, 
                         const nm2_command_packet_t* commands, int count);

// UDP 包解析
int nm2_parse_udp_packet(const uint8_t* data, int len,
                         nm2_command_packet_t* commands, int max_count);

// 各命令构建函数
int nm2_build_inv(uint8_t* buffer, const char* name, const char* product_id, 
                  nm2_capabilities_t caps);
int nm2_build_inv_accepted(uint8_t* buffer, const char* name, const char* product_id);
int nm2_build_inv_auth_required(uint8_t* buffer, const char* nonce, 
                                 const char* name, const char* product_id);
int nm2_build_inv_with_auth(uint8_t* buffer, const uint8_t* auth_digest);
int nm2_build_ping(uint8_t* buffer, uint32_t ping_id);
int nm2_build_ping_reply(uint8_t* buffer, uint32_t ping_id);
int nm2_build_bye(uint8_t* buffer, nm2_bye_reason_t reason, const char* message);
int nm2_build_bye_reply(uint8_t* buffer);
int nm2_build_nak(uint8_t* buffer, nm2_nak_reason_t reason, 
                  const uint8_t* original_header);
int nm2_build_session_reset(uint8_t* buffer);
int nm2_build_session_reset_reply(uint8_t* buffer);
int nm2_build_ump_data(uint8_t* buffer, uint16_t seq, const uint8_t* ump, int len);
int nm2_build_retransmit_request(uint8_t* buffer, uint16_t seq, uint16_t count);
int nm2_build_retransmit_error(uint8_t* buffer, uint16_t seq);

// 认证相关
void nm2_generate_nonce(char* nonce, int len);
void nm2_compute_auth_digest(uint8_t* digest, const char* nonce, 
                              const char* secret);
void nm2_compute_user_auth_digest(uint8_t* digest, const char* nonce,
                                   const char* username, const char* password);
```

### 4.3 修改计划

#### 阶段 1: 核心协议修复 (高优先级)

1. **创建 `nm2_protocol.c`**
   - 实现所有命令的构建和解析函数
   - 实现 UDP 包签名处理
   - 实现正确的命令码

2. **修改 `nm2_session.c`**
   - 使用新的协议函数
   - 修正 INV_ACCEPTED 命令码 (0x02 → 0x10)
   - 添加 BYE_REPLY 处理
   - 添加 NAK 处理

3. **修改 `nm2_transport.c`**
   - 修正 UMP_DATA 命令码 (0x10 → 0xFF)
   - 添加 UDP 签名
   - 完善序列号管理

4. **修改 `network_midi2.c`**
   - 集成新的协议模块
   - 修正 PING 命令码 (0x00 → 0x20)
   - 添加 PING_REPLY 处理

#### 阶段 2: 会话管理增强 (中优先级)

1. **添加重传机制**
   - 实现发送缓冲区
   - 实现丢包检测
   - 实现 RETRANSMIT_REQUEST/ERROR

2. **添加会话重置**
   - 实现 SESSION_RESET/REPLY
   - 序列号重置逻辑

3. **完善错误处理**
   - NAK 响应
   - 错误恢复

#### 阶段 3: 认证功能 (中优先级)

1. **共享密钥认证**
   - INV_AUTH_REQUIRED
   - INV_WITH_AUTH
   - SHA256 摘要计算

2. **用户认证**
   - INV_USER_AUTH_REQUIRED
   - INV_WITH_USER_AUTH

#### 阶段 4: 优化与完善 (低优先级)

1. **INV_PENDING 支持**
2. **完整的 TXT 记录**
3. **性能优化**

---

## 五、具体代码修改

### 5.1 nm2_protocol.c 核心实现

```c
#include "nm2_protocol.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "mbedtls/sha256.h"

static const char* TAG = "NM2_PROTO";

// UDP 包构建
int nm2_build_udp_packet(uint8_t* buffer, int max_len, 
                         const nm2_command_packet_t* commands, int count) {
    if (!buffer || !commands || count <= 0) return -1;
    
    int offset = 0;
    
    // 写入签名
    if (max_len < 4) return -1;
    buffer[offset++] = (NM2_SIGNATURE >> 24) & 0xFF;
    buffer[offset++] = (NM2_SIGNATURE >> 16) & 0xFF;
    buffer[offset++] = (NM2_SIGNATURE >> 8) & 0xFF;
    buffer[offset++] = NM2_SIGNATURE & 0xFF;
    
    // 写入命令包
    for (int i = 0; i < count; i++) {
        const nm2_command_packet_t* cmd = &commands[i];
        int cmd_len = 4 + cmd->payload_words * 4;
        
        if (offset + cmd_len > max_len) return -1;
        
        buffer[offset++] = (uint8_t)cmd->command;
        buffer[offset++] = cmd->payload_words;
        buffer[offset++] = cmd->specific1;
        buffer[offset++] = cmd->specific2;
        
        if (cmd->payload && cmd->payload_len > 0) {
            memcpy(&buffer[offset], cmd->payload, cmd->payload_len);
            // 填充到 4 字节边界
            int pad = (cmd->payload_words * 4) - cmd->payload_len;
            if (pad > 0) {
                memset(&buffer[offset + cmd->payload_len], 0, pad);
            }
            offset += cmd->payload_words * 4;
        }
    }
    
    return offset;
}

// UDP 包解析
int nm2_parse_udp_packet(const uint8_t* data, int len,
                         nm2_command_packet_t* commands, int max_count) {
    if (!data || len < 8 || !commands || max_count <= 0) return -1;
    
    // 验证签名
    uint32_t sig = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                   ((uint32_t)data[2] << 8) | data[3];
    if (sig != NM2_SIGNATURE) {
        ESP_LOGW(TAG, "Invalid signature: 0x%08X", sig);
        return -1;
    }
    
    int offset = 4;
    int count = 0;
    
    while (offset < len && count < max_count) {
        if (offset + 4 > len) break;
        
        nm2_command_packet_t* cmd = &commands[count];
        cmd->command = (nm2_command_code_t)data[offset];
        cmd->payload_words = data[offset + 1];
        cmd->specific1 = data[offset + 2];
        cmd->specific2 = data[offset + 3];
        
        int payload_len = cmd->payload_words * 4;
        offset += 4;
        
        if (offset + payload_len > len) break;
        
        cmd->payload = &data[offset];
        cmd->payload_len = payload_len;
        offset += payload_len;
        count++;
    }
    
    return count;
}

// 构建 INV 命令
int nm2_build_inv(uint8_t* buffer, const char* name, const char* product_id, 
                  nm2_capabilities_t caps) {
    if (!buffer || !name) return -1;
    
    int name_len = strlen(name);
    int product_len = product_id ? strlen(product_id) : 0;
    int name_words = (name_len + 3) / 4;
    int product_words = (product_len + 3) / 4;
    int payload_words = name_words + product_words;
    
    int offset = 0;
    buffer[offset++] = NM2_CMD_INV;
    buffer[offset++] = payload_words;
    buffer[offset++] = name_words;
    buffer[offset++] = caps;
    
    // 名称
    memcpy(&buffer[offset], name, name_len);
    memset(&buffer[offset + name_len], 0, name_words * 4 - name_len);
    offset += name_words * 4;
    
    // 产品 ID
    if (product_len > 0) {
        memcpy(&buffer[offset], product_id, product_len);
        memset(&buffer[offset + product_len], 0, product_words * 4 - product_len);
    }
    offset += product_words * 4;
    
    return offset;
}

// 构建 INV_ACCEPTED 命令
int nm2_build_inv_accepted(uint8_t* buffer, const char* name, const char* product_id) {
    if (!buffer || !name) return -1;
    
    int name_len = strlen(name);
    int product_len = product_id ? strlen(product_id) : 0;
    int name_words = (name_len + 3) / 4;
    int product_words = (product_len + 3) / 4;
    int payload_words = name_words + product_words;
    
    int offset = 0;
    buffer[offset++] = NM2_CMD_INV_ACCEPTED;  // 0x10
    buffer[offset++] = payload_words;
    buffer[offset++] = name_words;
    buffer[offset++] = 0;
    
    memcpy(&buffer[offset], name, name_len);
    memset(&buffer[offset + name_len], 0, name_words * 4 - name_len);
    offset += name_words * 4;
    
    if (product_len > 0) {
        memcpy(&buffer[offset], product_id, product_len);
        memset(&buffer[offset + product_len], 0, product_words * 4 - product_len);
    }
    offset += product_words * 4;
    
    return offset;
}

// 构建 PING 命令
int nm2_build_ping(uint8_t* buffer, uint32_t ping_id) {
    if (!buffer) return -1;
    
    buffer[0] = NM2_CMD_PING;  // 0x20
    buffer[1] = 1;  // payload_words
    buffer[2] = 0;
    buffer[3] = 0;
    buffer[4] = (ping_id >> 24) & 0xFF;
    buffer[5] = (ping_id >> 16) & 0xFF;
    buffer[6] = (ping_id >> 8) & 0xFF;
    buffer[7] = ping_id & 0xFF;
    
    return 8;
}

// 构建 PING_REPLY 命令
int nm2_build_ping_reply(uint8_t* buffer, uint32_t ping_id) {
    if (!buffer) return -1;
    
    buffer[0] = NM2_CMD_PING_REPLY;  // 0x21
    buffer[1] = 1;
    buffer[2] = 0;
    buffer[3] = 0;
    buffer[4] = (ping_id >> 24) & 0xFF;
    buffer[5] = (ping_id >> 16) & 0xFF;
    buffer[6] = (ping_id >> 8) & 0xFF;
    buffer[7] = ping_id & 0xFF;
    
    return 8;
}

// 构建 BYE 命令
int nm2_build_bye(uint8_t* buffer, nm2_bye_reason_t reason, const char* message) {
    if (!buffer) return -1;
    
    int msg_len = message ? strlen(message) : 0;
    int msg_words = (msg_len + 3) / 4;
    
    buffer[0] = NM2_CMD_BYE;  // 0xF0
    buffer[1] = msg_words;
    buffer[2] = reason;
    buffer[3] = 0;
    
    int offset = 4;
    if (msg_len > 0) {
        memcpy(&buffer[offset], message, msg_len);
        memset(&buffer[offset + msg_len], 0, msg_words * 4 - msg_len);
        offset += msg_words * 4;
    }
    
    return offset;
}

// 构建 BYE_REPLY 命令
int nm2_build_bye_reply(uint8_t* buffer) {
    if (!buffer) return -1;
    
    buffer[0] = NM2_CMD_BYE_REPLY;  // 0xF1
    buffer[1] = 0;
    buffer[2] = 0;
    buffer[3] = 0;
    
    return 4;
}

// 构建 UMP_DATA 命令
int nm2_build_ump_data(uint8_t* buffer, uint16_t seq, const uint8_t* ump, int len) {
    if (!buffer || !ump || len <= 0) return -1;
    
    int ump_words = (len + 3) / 4;
    
    buffer[0] = NM2_CMD_UMP_DATA;  // 0xFF
    buffer[1] = ump_words;
    buffer[2] = (seq >> 8) & 0xFF;
    buffer[3] = seq & 0xFF;
    
    memcpy(&buffer[4], ump, len);
    memset(&buffer[4 + len], 0, ump_words * 4 - len);
    
    return 4 + ump_words * 4;
}

// 生成随机 nonce
void nm2_generate_nonce(char* nonce, int len) {
    const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < len && i < 16; i++) {
        nonce[i] = chars[rand() % (sizeof(chars) - 1)];
    }
}

// 计算认证摘要
void nm2_compute_auth_digest(uint8_t* digest, const char* nonce, const char* secret) {
    char data[128];
    snprintf(data, sizeof(data), "%s%s", nonce, secret);
    mbedtls_sha256((unsigned char*)data, strlen(data), digest, 0);
}

// 计算用户认证摘要
void nm2_compute_user_auth_digest(uint8_t* digest, const char* nonce,
                                   const char* username, const char* password) {
    char data[256];
    snprintf(data, sizeof(data), "%s%s%s", nonce, username, password);
    mbedtls_sha256((unsigned char*)data, strlen(data), digest, 0);
}
```

### 5.2 修改 nm2_session.c

主要修改点：
1. 使用 `NM2_CMD_INV_ACCEPTED (0x10)` 替代 `0x02`
2. 使用 `NM2_CMD_BYE (0xF0)` 替代 `0x04`
3. 添加 BYE_REPLY 处理
4. 集成 nm2_protocol 模块

### 5.3 修改 nm2_transport.c

主要修改点：
1. 使用 `NM2_CMD_UMP_DATA (0xFF)` 替代 `0x10`
2. 添加 UDP 签名
3. 完善序列号验证

---

## 六、测试计划

### 6.1 单元测试

1. **协议格式测试**
   - UDP 包签名验证
   - 命令包构建/解析
   - 边界条件测试

2. **会话管理测试**
   - 邀请流程
   - 终止流程
   - 超时处理

### 6.2 集成测试

1. **与 C# Test 项目互操作测试**
   - ESP32 作为客户端连接 C# 服务端
   - C# 客户端连接 ESP32 服务端
   - 双向 MIDI 数据传输

2. **与 MidiBridge 互操作测试**
   - 完整会话流程
   - 认证流程
   - 重传机制

### 6.3 压力测试

1. 高频率 MIDI 数据传输
2. 长时间运行稳定性
3. 网络异常恢复

---

## 七、实施时间表

| 阶段 | 任务 | 预计时间 |
|------|------|---------|
| 1 | 创建 nm2_protocol 模块 | 1 天 |
| 2 | 修改 nm2_session.c | 0.5 天 |
| 3 | 修改 nm2_transport.c | 0.5 天 |
| 4 | 修改 network_midi2.c | 0.5 天 |
| 5 | 单元测试 | 1 天 |
| 6 | 集成测试 | 1 天 |
| 7 | 重传机制实现 | 2 天 |
| 8 | 认证机制实现 | 2 天 |
| 9 | 完善与优化 | 1 天 |

**总计**: 约 9-10 个工作日

---

## 八、风险与注意事项

1. **向后兼容性**: 修改后可能与旧版本 ESP32 固件不兼容
2. **内存使用**: 重传缓冲区会增加内存消耗
3. **性能影响**: SHA256 计算可能影响实时性能
4. **测试覆盖**: 需要充分测试各种边界情况

---

## 九、参考资料

1. **MIDI 2.0 规范**: M2-124-UM_v1-0-1_Network-MIDI-2-0-UDP.pdf
2. **参考实现**: D:\WorkSpace\MidiBridge\MidiBridge\Services\NetworkMidi2\
3. **ESP-IDF 文档**: mDNS, UDP Socket 编程