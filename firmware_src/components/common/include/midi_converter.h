/**
 * @file midi_converter.h
 * @brief MIDI 协议转换器 - MIDI 1.0 ↔ UMP (MIDI 2.0)
 * 
 * 基于 MIDI 2.0 规范 (M2-104-UM v1.1.2) 实现：
 * - MT=0x2: MIDI 1.0 Protocol in UMP Format
 * - MT=0x4: MIDI 2.0 Channel Voice Messages
 * 
 * @see https://midi.org/universal-midi-packet-ump-and-midi-2-0-protocol-specification
 */

#ifndef MIDI_CONVERTER_H
#define MIDI_CONVERTER_H

#include <stdint.h>
#include <stdbool.h>
#include "midi_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * MIDI 2.0 UMP 常量定义
 * ============================================================================ */

/** UMP Message Types */
#define UMP_MT_UTILITY              0x0     ///< Utility Messages (32-bit)
#define UMP_MT_SYSTEM               0x1     ///< System Common & Real Time (32-bit)
#define UMP_MT_MIDI1_CV             0x2     ///< MIDI 1.0 Channel Voice (32-bit)
#define UMP_MT_DATA_64              0x3     ///< Data Message (64-bit) - SysEx
#define UMP_MT_MIDI2_CV             0x4     ///< MIDI 2.0 Channel Voice (64-bit)
#define UMP_MT_DATA_128             0x5     ///< Data Message (128-bit) - SysEx
#define UMP_MT_FLEX_DATA            0xD     ///< Flex Data (128-bit)
#define UMP_MT_STREAM               0xF     ///< Stream Messages (128-bit)

/** UMP Group 数量 */
#define UMP_MAX_GROUPS              16

/** UMP 最大包大小 (bytes) */
#define UMP_MAX_PACKET_SIZE         16

/* ============================================================================
 * MIDI 1.0 状态字节定义
 * ============================================================================ */

/** MIDI 1.0 Channel Voice Message Status */
#define MIDI1_STATUS_NOTE_OFF       0x80    ///< Note Off
#define MIDI1_STATUS_NOTE_ON        0x90    ///< Note On
#define MIDI1_STATUS_POLY_PRESSURE  0xA0    ///< Polyphonic Key Pressure (Aftertouch)
#define MIDI1_STATUS_CONTROL_CHANGE 0xB0    ///< Control Change
#define MIDI1_STATUS_PROGRAM_CHANGE 0xC0    ///< Program Change
#define MIDI1_STATUS_CHANNEL_PRESSURE 0xD0  ///< Channel Pressure (Aftertouch)
#define MIDI1_STATUS_PITCH_BEND     0xE0    ///< Pitch Bend

/** MIDI 1.0 System Common Message Status */
#define MIDI1_STATUS_SYSEX_START    0xF0    ///< System Exclusive Start
#define MIDI1_STATUS_MTC_QUARTER    0xF1    ///< MTC Quarter Frame
#define MIDI1_STATUS_SONG_POSITION  0xF2    ///< Song Position Pointer
#define MIDI1_STATUS_SONG_SELECT    0xF3    ///< Song Select
#define MIDI1_STATUS_TUNE_REQUEST   0xF6    ///< Tune Request
#define MIDI1_STATUS_SYSEX_END      0xF7    ///< System Exclusive End

/** MIDI 1.0 Real Time Message Status */
#define MIDI1_STATUS_TIMING_CLOCK   0xF8    ///< Timing Clock
#define MIDI1_STATUS_START          0xFA    ///< Start
#define MIDI1_STATUS_CONTINUE       0xFB    ///< Continue
#define MIDI1_STATUS_STOP           0xFC    ///< Stop
#define MIDI1_STATUS_ACTIVE_SENSE   0xFE    ///< Active Sensing
#define MIDI1_STATUS_RESET          0xFF    ///< Reset

/** MIDI 1.0 数据范围 */
#define MIDI1_MAX_CHANNEL           15      ///< 最大通道号 (0-15)
#define MIDI1_MAX_NOTE              127     ///< 最大音符号
#define MIDI1_MAX_VELOCITY          127     ///< 最大力度 (7-bit)
#define MIDI1_MAX_CC_VALUE          127     ///< 最大 CC 值 (7-bit)
#define MIDI1_MAX_PROGRAM           127     ///< 最大程序号 (7-bit)
#define MIDI1_MAX_PRESSURE          127     ///< 最大压力值 (7-bit)

/** MIDI 2.0 数据范围 */
#define MIDI2_MAX_VELOCITY          0xFFFF  ///< 最大力度 (16-bit)
#define MIDI2_MAX_CC_VALUE          0xFFFFFFFF  ///< 最大 CC 值 (32-bit)
#define MIDI2_MAX_PITCH_BEND        0xFFFFFFFF  ///< 最大 Pitch Bend (32-bit, unsigned)

/* ============================================================================
 * 类型定义
 * ============================================================================ */

/**
 * @brief UMP 包结构 (最多 128 bits)
 */
typedef struct {
    uint8_t data[UMP_MAX_PACKET_SIZE];  ///< UMP 数据
    uint8_t length;                      ///< 有效数据长度 (4, 8, 12, 16)
} ump_packet_t;

/**
 * @brief MIDI 1.0 消息结构
 */
typedef struct {
    uint8_t status;      ///< 状态字节
    uint8_t data1;       ///< 数据字节 1
    uint8_t data2;       ///< 数据字节 2 (可选)
    uint8_t length;      ///< 消息长度 (1-3 bytes)
} midi1_message_t;

/**
 * @brief MIDI 2.0 Channel Voice Message 结构
 */
typedef struct {
    uint8_t group;       ///< UMP Group (0-15)
    uint8_t opcode;      ///< Opcode (高4位与 status 相关)
    uint8_t channel;     ///< MIDI Channel (0-15)
    uint16_t index;      ///< Note Number / CC Number
    uint32_t data;       ///< Velocity / Value / etc.
} midi2_cv_message_t;

/**
 * @brief 转换方向
 */
typedef enum {
    CONVERT_MIDI1_TO_UMP,   ///< MIDI 1.0 → UMP
    CONVERT_UMP_TO_MIDI1,   ///< UMP → MIDI 1.0
    CONVERT_MIDI2_TO_UMP,   ///< MIDI 2.0 → UMP
    CONVERT_UMP_TO_MIDI2,   ///< UMP → MIDI 2.0
} convert_direction_t;

/* ============================================================================
 * MIDI 1.0 → UMP 转换函数
 * ============================================================================ */

/**
 * @brief 将 MIDI 1.0 Channel Voice Message 转换为 UMP (MT=0x2)
 * 
 * @param status MIDI 状态字节 (0x80-0xEF)
 * @param data1 数据字节 1
 * @param data2 数据字节 2
 * @param group UMP Group (0-15)
 * @param ump_out 输出 UMP 数据 (至少 4 字节)
 * @return MIDI_OK 成功
 * 
 * @note UMP MT=0x2 格式:
 *       [MT:4][Group:4][Status:8][Data1:8][Data2:8]
 */
midi_error_t midi1_to_ump_cv(uint8_t status, uint8_t data1, uint8_t data2,
                              uint8_t group, uint8_t* ump_out);

/**
 * @brief 将 MIDI 1.0 System Message 转换为 UMP (MT=0x1)
 * 
 * @param status 系统消息状态字节 (0xF0-0xFF)
 * @param data 数据字节 (可选)
 * @param data_len 数据长度
 * @param ump_out 输出 UMP 数据
 * @param ump_len 输出 UMP 长度
 * @return MIDI_OK 成功
 */
midi_error_t midi1_to_ump_system(uint8_t status, const uint8_t* data, uint8_t data_len,
                                  uint8_t* ump_out, uint8_t* ump_len);

/* ============================================================================
 * UMP → MIDI 1.0 转换函数
 * ============================================================================ */

/**
 * @brief 将 UMP (MT=0x2) 转换为 MIDI 1.0 Channel Voice Message
 * 
 * @param ump UMP 数据 (4 bytes)
 * @param status_out 输出状态字节
 * @param data1_out 输出数据字节 1
 * @param data2_out 输出数据字节 2
 * @return MIDI_OK 成功, MIDI_ERR_MIDI_UMP_INVALID 如果不是 MT=0x2
 */
midi_error_t ump_to_midi1_cv(const uint8_t* ump,
                              uint8_t* status_out, uint8_t* data1_out, uint8_t* data2_out);

/**
 * @brief 将 UMP 转换为 MIDI 1.0 消息 (自动检测类型)
 * 
 * @param ump UMP 数据
 * @param ump_len UMP 长度
 * @param msg_out 输出 MIDI 1.0 消息
 * @return MIDI_OK 成功
 */
midi_error_t ump_to_midi1(const uint8_t* ump, uint8_t ump_len, midi1_message_t* msg_out);

/* ============================================================================
 * MIDI 2.0 ↔ UMP 转换函数
 * ============================================================================ */

/**
 * @brief 将 MIDI 2.0 Channel Voice Message 转换为 UMP (MT=0x4)
 * 
 * @param msg MIDI 2.0 CV 消息
 * @param ump_out 输出 UMP 数据 (至少 8 字节)
 * @return MIDI_OK 成功
 * 
 * @note UMP MT=0x4 格式:
 *       [MT:4][Group:4][Opcode:4][Channel:4][Index:16][Data:32]
 */
midi_error_t midi2_to_ump_cv(const midi2_cv_message_t* msg, uint8_t* ump_out);

/**
 * @brief 将 UMP (MT=0x4) 转换为 MIDI 2.0 Channel Voice Message
 * 
 * @param ump UMP 数据 (8 bytes)
 * @param msg_out 输出 MIDI 2.0 CV 消息
 * @return MIDI_OK 成功
 */
midi_error_t ump_to_midi2_cv(const uint8_t* ump, midi2_cv_message_t* msg_out);

/* ============================================================================
 * MIDI 1.0 ↔ MIDI 2.0 转换函数
 * ============================================================================ */

/**
 * @brief 将 MIDI 1.0 Channel Voice Message 转换为 MIDI 2.0 格式
 * 
 * @param status MIDI 1.0 状态字节
 * @param data1 数据字节 1
 * @param data2 数据字节 2
 * @param msg_out 输出 MIDI 2.0 消息
 * @return MIDI_OK 成功
 * 
 * @note 值扩展规则 (MIDI 2.0 Bit Scaling):
 *       - 7-bit → 16-bit: value << 9 (e.g., velocity 127 → 0x7F00)
 *       - 7-bit → 32-bit: value << 25
 *       - 14-bit → 32-bit: value << 18
 */
midi_error_t midi1_to_midi2_cv(uint8_t status, uint8_t data1, uint8_t data2,
                                midi2_cv_message_t* msg_out);

/**
 * @brief 将 MIDI 2.0 Channel Voice Message 缩减为 MIDI 1.0 格式
 * 
 * @param msg MIDI 2.0 CV 消息
 * @param status_out 输出 MIDI 1.0 状态字节
 * @param data1_out 输出数据字节 1
 * @param data2_out 输出数据字节 2
 * @return MIDI_OK 成功
 * 
 * @note 值缩减规则:
 *       - 16-bit → 7-bit: value >> 9
 *       - 32-bit → 7-bit: value >> 25
 *       - 32-bit → 14-bit: value >> 18
 */
midi_error_t midi2_to_midi1_cv(const midi2_cv_message_t* msg,
                                uint8_t* status_out, uint8_t* data1_out, uint8_t* data2_out);

/* ============================================================================
 * 便捷函数
 * ============================================================================ */

/**
 * @brief 获取 UMP Message Type
 */
static inline uint8_t ump_get_message_type(const uint8_t* ump) {
    return (ump[0] >> 4) & 0x0F;
}

/**
 * @brief 获取 UMP Group
 */
static inline uint8_t ump_get_group(const uint8_t* ump) {
    return ump[0] & 0x0F;
}

/**
 * @brief 设置 UMP Group
 */
static inline void ump_set_group(uint8_t* ump, uint8_t group) {
    ump[0] = (ump[0] & 0xF0) | (group & 0x0F);
}

/**
 * @brief 获取 MIDI 1.0 Channel Voice Message Status
 */
static inline uint8_t midi1_get_status_type(uint8_t status) {
    return status & 0xF0;
}

/**
 * @brief 获取 MIDI 1.0 Channel
 */
static inline uint8_t midi1_get_channel(uint8_t status) {
    return status & 0x0F;
}

/**
 * @brief 检查是否为 MIDI 1.0 Channel Voice Message
 */
static inline bool midi1_is_channel_voice(uint8_t status) {
    uint8_t type = status & 0xF0;
    return (type >= 0x80 && type <= 0xE0);
}

/**
 * @brief 检查是否为 MIDI 1.0 System Real Time Message
 */
static inline bool midi1_is_real_time(uint8_t status) {
    return (status >= 0xF8);
}

/**
 * @brief 检查是否为 MIDI 1.0 System Common Message
 */
static inline bool midi1_is_system_common(uint8_t status) {
    return (status >= 0xF0 && status < 0xF8);
}

/**
 * @brief 7-bit 值扩展为 16-bit (MIDI 2.0 Velocity)
 */
static inline uint16_t midi_scale_7_to_16(uint8_t value) {
    if (value == 0) return 0;
    if (value == 127) return 0xFFFF;
    return ((uint16_t)value << 9) | 0x0100;
}

/**
 * @brief 16-bit 值缩减为 7-bit
 */
static inline uint8_t midi_scale_16_to_7(uint16_t value) {
    return (value >> 9) & 0x7F;
}

/**
 * @brief 7-bit 值扩展为 32-bit (MIDI 2.0 CC Value)
 */
static inline uint32_t midi_scale_7_to_32(uint8_t value) {
    if (value == 0) return 0;
    if (value == 127) return 0xFFFFFFFF;
    return ((uint32_t)value << 25);
}

/**
 * @brief 32-bit 值缩减为 7-bit
 */
static inline uint8_t midi_scale_32_to_7(uint32_t value) {
    return (value >> 25) & 0x7F;
}

/**
 * @brief 14-bit 值扩展为 32-bit (MIDI 2.0 Pitch Bend)
 */
static inline uint32_t midi_scale_14_to_32(uint16_t value) {
    return ((uint32_t)value << 18);
}

/**
 * @brief 32-bit 值缩减为 14-bit
 */
static inline uint16_t midi_scale_32_to_14(uint32_t value) {
    return (value >> 18) & 0x3FFF;
}

#ifdef __cplusplus
}
#endif

#endif // MIDI_CONVERTER_H