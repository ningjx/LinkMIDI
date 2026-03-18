/**
 * @file midi_converter.c
 * @brief MIDI 协议转换器实现
 * 
 * 基于 MIDI 2.0 规范 (M2-104-UM v1.1.2)
 */

#include "midi_converter.h"
#include <string.h>
#include "esp_log.h"

static const char* TAG = "MIDI_CONV";

/* ============================================================================
 * MIDI 1.0 → UMP 转换
 * ============================================================================ */

midi_error_t midi1_to_ump_cv(uint8_t status, uint8_t data1, uint8_t data2,
                              uint8_t group, uint8_t* ump_out) {
    if (!ump_out) return MIDI_ERR_NULL_PTR;
    
    uint8_t status_type = status & 0xF0;
    
    // 验证是否为 Channel Voice Message
    if (status_type < 0x80 || status_type > 0xE0) {
        ESP_LOGW(TAG, "Invalid MIDI 1.0 CV status: 0x%02X", status);
        return MIDI_ERR_MIDI_INVALID_MSG;
    }
    
    // UMP MT=0x2 格式: [MT:4][Group:4][Status:8][Data1:8][Data2:8]
    ump_out[0] = (UMP_MT_MIDI1_CV << 4) | (group & 0x0F);
    ump_out[1] = status;
    ump_out[2] = data1;
    ump_out[3] = data2;
    
    return MIDI_OK;
}

midi_error_t midi1_to_ump_system(uint8_t status, const uint8_t* data, uint8_t data_len,
                                  uint8_t* ump_out, uint8_t* ump_len) {
    if (!ump_out || !ump_len) return MIDI_ERR_NULL_PTR;
    
    // System Real-Time Messages (单字节)
    if (status >= 0xF8) {
        // MT=0x1 System Real-Time
        ump_out[0] = (UMP_MT_SYSTEM << 4) | 0x00;  // Group=0
        ump_out[1] = status;
        ump_out[2] = 0x00;
        ump_out[3] = 0x00;
        *ump_len = 4;
        return MIDI_OK;
    }
    
    // System Common Messages
    switch (status) {
        case MIDI1_STATUS_SYSEX_START:
            // SysEx 使用 MT=0x3 或 0x5，需要特殊处理
            // 这里简化处理，实际需要完整的 SysEx 状态机
            ESP_LOGW(TAG, "SysEx conversion not fully implemented");
            return MIDI_ERR_NOT_SUPPORTED;
            
        case MIDI1_STATUS_MTC_QUARTER:
            // MTC Quarter Frame: 2 bytes
            ump_out[0] = (UMP_MT_SYSTEM << 4) | 0x00;
            ump_out[1] = status;
            ump_out[2] = data ? data[0] : 0x00;
            ump_out[3] = 0x00;
            *ump_len = 4;
            break;
            
        case MIDI1_STATUS_SONG_POSITION:
            // Song Position Pointer: 3 bytes
            ump_out[0] = (UMP_MT_SYSTEM << 4) | 0x00;
            ump_out[1] = status;
            ump_out[2] = data ? data[0] : 0x00;
            ump_out[3] = data && data_len > 1 ? data[1] : 0x00;
            *ump_len = 4;
            break;
            
        case MIDI1_STATUS_SONG_SELECT:
            // Song Select: 2 bytes
            ump_out[0] = (UMP_MT_SYSTEM << 4) | 0x00;
            ump_out[1] = status;
            ump_out[2] = data ? data[0] : 0x00;
            ump_out[3] = 0x00;
            *ump_len = 4;
            break;
            
        case MIDI1_STATUS_TUNE_REQUEST:
            // Tune Request: 1 byte
            ump_out[0] = (UMP_MT_SYSTEM << 4) | 0x00;
            ump_out[1] = status;
            ump_out[2] = 0x00;
            ump_out[3] = 0x00;
            *ump_len = 4;
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown system status: 0x%02X", status);
            return MIDI_ERR_MIDI_UNKNOWN_MSG;
    }
    
    return MIDI_OK;
}

/* ============================================================================
 * UMP → MIDI 1.0 转换
 * ============================================================================ */

midi_error_t ump_to_midi1_cv(const uint8_t* ump,
                              uint8_t* status_out, uint8_t* data1_out, uint8_t* data2_out) {
    if (!ump || !status_out || !data1_out || !data2_out) {
        return MIDI_ERR_NULL_PTR;
    }
    
    uint8_t mt = ump_get_message_type(ump);
    
    // 只处理 MT=0x2 (MIDI 1.0 in UMP)
    if (mt != UMP_MT_MIDI1_CV) {
        ESP_LOGW(TAG, "UMP is not MIDI 1.0 CV, MT=0x%X", mt);
        return MIDI_ERR_MIDI_UMP_INVALID;
    }
    
    *status_out = ump[1];
    *data1_out = ump[2];
    *data2_out = ump[3];
    
    return MIDI_OK;
}

midi_error_t ump_to_midi1(const uint8_t* ump, uint8_t ump_len, midi1_message_t* msg_out) {
    if (!ump || !msg_out) return MIDI_ERR_NULL_PTR;
    if (ump_len < 4) return MIDI_ERR_MIDI_UMP_INVALID;
    
    memset(msg_out, 0, sizeof(midi1_message_t));
    
    uint8_t mt = ump_get_message_type(ump);
    
    switch (mt) {
        case UMP_MT_MIDI1_CV:  // MT=0x2: MIDI 1.0 Channel Voice
            msg_out->status = ump[1];
            msg_out->data1 = ump[2];
            msg_out->data2 = ump[3];
            msg_out->length = 3;
            break;
            
        case UMP_MT_SYSTEM:  // MT=0x1: System Common/Real-Time
            msg_out->status = ump[1];
            if (ump[1] >= 0xF8) {
                // Real-Time: 1 byte
                msg_out->length = 1;
            } else if (ump[1] == 0xF2) {
                // Song Position Pointer: 3 bytes
                msg_out->data1 = ump[2];
                msg_out->data2 = ump[3];
                msg_out->length = 3;
            } else if (ump[1] == 0xF1 || ump[1] == 0xF3) {
                // MTC Quarter Frame / Song Select: 2 bytes
                msg_out->data1 = ump[2];
                msg_out->length = 2;
            } else {
                msg_out->length = 1;
            }
            break;
            
        case UMP_MT_MIDI2_CV:  // MT=0x4: MIDI 2.0 Channel Voice
            // 需要转换
            {
                midi2_cv_message_t midi2_msg;
                midi_error_t err = ump_to_midi2_cv(ump, &midi2_msg);
                if (err != MIDI_OK) return err;
                return midi2_to_midi1_cv(&midi2_msg, &msg_out->status, 
                                         &msg_out->data1, &msg_out->data2);
            }
            break;
            
        default:
            ESP_LOGW(TAG, "Unsupported UMP MT: 0x%X", mt);
            return MIDI_ERR_MIDI_UMP_INVALID;
    }
    
    return MIDI_OK;
}

/* ============================================================================
 * MIDI 2.0 ↔ UMP 转换
 * ============================================================================ */

midi_error_t midi2_to_ump_cv(const midi2_cv_message_t* msg, uint8_t* ump_out) {
    if (!msg || !ump_out) return MIDI_ERR_NULL_PTR;
    
    // UMP MT=0x4 格式:
    // Word 0: [MT:4][Group:4][Opcode:4][Channel:4]
    // Word 1: [Index:16]
    // Word 2-3: [Data:32]
    
    uint8_t opcode = msg->opcode & 0x0F;
    
    ump_out[0] = (UMP_MT_MIDI2_CV << 4) | (msg->group & 0x0F);
    ump_out[1] = (opcode << 4) | (msg->channel & 0x0F);
    ump_out[2] = (msg->index >> 8) & 0xFF;
    ump_out[3] = msg->index & 0xFF;
    ump_out[4] = (msg->data >> 24) & 0xFF;
    ump_out[5] = (msg->data >> 16) & 0xFF;
    ump_out[6] = (msg->data >> 8) & 0xFF;
    ump_out[7] = msg->data & 0xFF;
    
    return MIDI_OK;
}

midi_error_t ump_to_midi2_cv(const uint8_t* ump, midi2_cv_message_t* msg_out) {
    if (!ump || !msg_out) return MIDI_ERR_NULL_PTR;
    
    uint8_t mt = ump_get_message_type(ump);
    
    if (mt != UMP_MT_MIDI2_CV) {
        ESP_LOGW(TAG, "UMP is not MIDI 2.0 CV, MT=0x%X", mt);
        return MIDI_ERR_MIDI_UMP_INVALID;
    }
    
    memset(msg_out, 0, sizeof(midi2_cv_message_t));
    
    msg_out->group = ump_get_group(ump);
    msg_out->opcode = (ump[1] >> 4) & 0x0F;
    msg_out->channel = ump[1] & 0x0F;
    msg_out->index = ((uint16_t)ump[2] << 8) | ump[3];
    msg_out->data = ((uint32_t)ump[4] << 24) | ((uint32_t)ump[5] << 16) |
                    ((uint32_t)ump[6] << 8) | ump[7];
    
    return MIDI_OK;
}

/* ============================================================================
 * MIDI 1.0 ↔ MIDI 2.0 转换
 * ============================================================================ */

midi_error_t midi1_to_midi2_cv(uint8_t status, uint8_t data1, uint8_t data2,
                                midi2_cv_message_t* msg_out) {
    if (!msg_out) return MIDI_ERR_NULL_PTR;
    
    uint8_t status_type = status & 0xF0;
    
    if (!midi1_is_channel_voice(status)) {
        return MIDI_ERR_MIDI_INVALID_MSG;
    }
    
    memset(msg_out, 0, sizeof(midi2_cv_message_t));
    
    msg_out->group = 0;
    msg_out->channel = midi1_get_channel(status);
    msg_out->index = data1;
    
    // 根据 MIDI 2.0 规范，不同消息类型的 Opcode 映射
    // MIDI 2.0 Opcode 与 MIDI 1.0 Status 高 4 位相同
    msg_out->opcode = (status_type >> 4) & 0x0F;
    
    // 根据消息类型扩展数据
    switch (status_type) {
        case MIDI1_STATUS_NOTE_OFF:
        case MIDI1_STATUS_NOTE_ON:
            // Velocity: 7-bit → 16-bit
            msg_out->index = data1;  // Note Number
            msg_out->data = midi_scale_7_to_16(data2);  // Velocity
            break;
            
        case MIDI1_STATUS_POLY_PRESSURE:
            // Pressure: 7-bit → 32-bit
            msg_out->index = data1;  // Note Number
            msg_out->data = midi_scale_7_to_32(data2);  // Pressure
            break;
            
        case MIDI1_STATUS_CONTROL_CHANGE:
            // CC Value: 7-bit → 32-bit
            msg_out->index = data1;  // CC Number
            msg_out->data = midi_scale_7_to_32(data2);  // Value
            break;
            
        case MIDI1_STATUS_PROGRAM_CHANGE:
            // Program: 7-bit → 32-bit (包含 Bank Select 时需要额外处理)
            msg_out->index = 0;  // Reserved
            msg_out->data = midi_scale_7_to_32(data1);  // Program
            break;
            
        case MIDI1_STATUS_CHANNEL_PRESSURE:
            // Pressure: 7-bit → 32-bit
            msg_out->index = 0;  // Reserved
            msg_out->data = midi_scale_7_to_32(data1);  // Pressure
            break;
            
        case MIDI1_STATUS_PITCH_BEND:
            // Pitch Bend: 14-bit → 32-bit
            msg_out->index = 0;  // Reserved
            uint16_t pitch_14bit = ((uint16_t)data2 << 7) | data1;
            msg_out->data = midi_scale_14_to_32(pitch_14bit);
            break;
            
        default:
            return MIDI_ERR_MIDI_UNKNOWN_MSG;
    }
    
    return MIDI_OK;
}

midi_error_t midi2_to_midi1_cv(const midi2_cv_message_t* msg,
                                uint8_t* status_out, uint8_t* data1_out, uint8_t* data2_out) {
    if (!msg || !status_out || !data1_out || !data2_out) {
        return MIDI_ERR_NULL_PTR;
    }
    
    uint8_t status_type = (msg->opcode << 4) & 0xF0;
    
    // 验证是否为有效的 Channel Voice Opcode
    if (status_type < 0x80 || status_type > 0xE0) {
        ESP_LOGW(TAG, "Invalid MIDI 2.0 opcode: 0x%X", msg->opcode);
        return MIDI_ERR_MIDI_INVALID_MSG;
    }
    
    *status_out = status_type | (msg->channel & 0x0F);
    
    // 根据消息类型缩减数据
    switch (status_type) {
        case MIDI1_STATUS_NOTE_OFF:
        case MIDI1_STATUS_NOTE_ON:
            *data1_out = msg->index & 0x7F;  // Note Number
            *data2_out = midi_scale_16_to_7((uint16_t)msg->data);  // Velocity
            break;
            
        case MIDI1_STATUS_POLY_PRESSURE:
            *data1_out = msg->index & 0x7F;  // Note Number
            *data2_out = midi_scale_32_to_7(msg->data);  // Pressure
            break;
            
        case MIDI1_STATUS_CONTROL_CHANGE:
            *data1_out = msg->index & 0x7F;  // CC Number
            *data2_out = midi_scale_32_to_7(msg->data);  // Value
            break;
            
        case MIDI1_STATUS_PROGRAM_CHANGE:
            *data1_out = midi_scale_32_to_7(msg->data);  // Program
            *data2_out = 0;
            break;
            
        case MIDI1_STATUS_CHANNEL_PRESSURE:
            *data1_out = midi_scale_32_to_7(msg->data);  // Pressure
            *data2_out = 0;
            break;
            
        case MIDI1_STATUS_PITCH_BEND:
            {
                uint16_t pitch_14bit = midi_scale_32_to_14(msg->data);
                *data1_out = pitch_14bit & 0x7F;        // LSB
                *data2_out = (pitch_14bit >> 7) & 0x7F; // MSB
            }
            break;
            
        default:
            return MIDI_ERR_MIDI_UNKNOWN_MSG;
    }
    
    return MIDI_OK;
}