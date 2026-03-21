/**
 * @file nm2_protocol.c
 * @brief Network MIDI 2.0 Protocol Format Handler Implementation
 * 
 * Standalone implementation with no dependencies on other NM2 modules.
 */

#include "nm2_protocol.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/sha256.h"

static const char* TAG = "NM2_PROTO";

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/**
 * @brief Calculate length in 32-bit words (rounded up)
 */
static inline int to_words(int bytes) {
    return (bytes + 3) / 4;
}

/**
 * @brief Write a string to buffer with padding to 4-byte boundary
 * @return Number of bytes written (including padding)
 */
static int write_string_padded(uint8_t* buffer, const char* str, int max_words) {
    if (!str || !buffer) return 0;
    
    int len = strlen(str);
    int words = to_words(len);
    if (words > max_words) {
        words = max_words;
        len = words * 4;
    }
    
    memcpy(buffer, str, len);
    memset(buffer + len, 0, words * 4 - len);
    
    return words * 4;
}

/* ============================================================================
 * UDP Packet Functions
 * ============================================================================ */

int nm2_protocol_build_packet(uint8_t* buffer, int max_len,
                               const nm2_command_packet_t* commands, int count) {
    if (!buffer || !commands || count <= 0) {
        return -1;
    }
    
    int offset = 0;
    
    // Write signature
    if (max_len < 4) {
        ESP_LOGW(TAG, "Buffer too small for signature");
        return -1;
    }
    
    buffer[offset++] = (NM2_SIGNATURE >> 24) & 0xFF;
    buffer[offset++] = (NM2_SIGNATURE >> 16) & 0xFF;
    buffer[offset++] = (NM2_SIGNATURE >> 8) & 0xFF;
    buffer[offset++] = NM2_SIGNATURE & 0xFF;
    
    // Write command packets
    for (int i = 0; i < count; i++) {
        const nm2_command_packet_t* cmd = &commands[i];
        int cmd_len = 4 + cmd->payload_words * 4;
        
        if (offset + cmd_len > max_len) {
            ESP_LOGW(TAG, "Buffer overflow, cmd %d needs %d, have %d", 
                     i, cmd_len, max_len - offset);
            return -1;
        }
        
        buffer[offset++] = (uint8_t)cmd->command;
        buffer[offset++] = cmd->payload_words;
        buffer[offset++] = cmd->specific1;
        buffer[offset++] = cmd->specific2;
        
        if (cmd->payload && cmd->payload_len > 0) {
            int payload_bytes = cmd->payload_words * 4;
            memcpy(&buffer[offset], cmd->payload, cmd->payload_len);
            // Pad with zeros
            if (cmd->payload_len < payload_bytes) {
                memset(&buffer[offset + cmd->payload_len], 0, 
                       payload_bytes - cmd->payload_len);
            }
            offset += payload_bytes;
        }
    }
    
    return offset;
}

int nm2_protocol_parse_packet(const uint8_t* data, int len,
                               nm2_command_packet_t* commands, int max_count) {
    if (!data || len < 8 || !commands || max_count <= 0) {
        return -1;
    }
    
    // Validate signature
    uint32_t sig = ((uint32_t)data[0] << 24) | 
                   ((uint32_t)data[1] << 16) |
                   ((uint32_t)data[2] << 8) | 
                   (uint32_t)data[3];
    
    if (sig != NM2_SIGNATURE) {
        ESP_LOGW(TAG, "Invalid signature: 0x%08X (expected 0x%08X)", sig, NM2_SIGNATURE);
        return -1;
    }
    
    int offset = 4;
    int count = 0;
    
    while (offset < len && count < max_count) {
        if (offset + 4 > len) {
            ESP_LOGW(TAG, "Truncated command header at offset %d", offset);
            break;
        }
        
        nm2_command_packet_t* cmd = &commands[count];
        cmd->command = (nm2_command_code_t)data[offset];
        cmd->payload_words = data[offset + 1];
        cmd->specific1 = data[offset + 2];
        cmd->specific2 = data[offset + 3];
        
        int payload_bytes = cmd->payload_words * 4;
        offset += 4;
        
        if (offset + payload_bytes > len) {
            ESP_LOGW(TAG, "Truncated payload for cmd 0x%02X", cmd->command);
            break;
        }
        
        cmd->payload = &data[offset];
        cmd->payload_len = payload_bytes;
        offset += payload_bytes;
        count++;
    }
    
    return count;
}

bool nm2_protocol_validate_signature(const uint8_t* data, int len) {
    if (!data || len < 4) {
        return false;
    }
    
    uint32_t sig = ((uint32_t)data[0] << 24) | 
                   ((uint32_t)data[1] << 16) |
                   ((uint32_t)data[2] << 8) | 
                   (uint32_t)data[3];
    
    return sig == NM2_SIGNATURE;
}

/* ============================================================================
 * Command Building Functions
 * ============================================================================ */

int nm2_protocol_build_inv(uint8_t* buffer, int max_len,
                            const char* name, const char* product_id,
                            nm2_capabilities_t caps) {
    if (!buffer || !name) return -1;
    
    int name_len = strlen(name);
    int product_len = product_id ? strlen(product_id) : 0;
    int name_words = to_words(name_len);
    int product_words = to_words(product_len);
    int payload_words = name_words + product_words;
    int total_len = 4 + payload_words * 4;
    
    if (total_len > max_len) {
        ESP_LOGW(TAG, "INV too large: %d > %d", total_len, max_len);
        return -1;
    }
    
    int offset = 0;
    buffer[offset++] = NM2_CMD_INV;
    buffer[offset++] = payload_words;
    buffer[offset++] = name_words;
    buffer[offset++] = caps;
    
    offset += write_string_padded(&buffer[offset], name, name_words);
    if (product_len > 0) {
        offset += write_string_padded(&buffer[offset], product_id, product_words);
    }
    
    return offset;
}

int nm2_protocol_build_inv_accepted(uint8_t* buffer, int max_len,
                                     const char* name, const char* product_id) {
    if (!buffer || !name) return -1;
    
    int name_len = strlen(name);
    int product_len = product_id ? strlen(product_id) : 0;
    int name_words = to_words(name_len);
    int product_words = to_words(product_len);
    int payload_words = name_words + product_words;
    int total_len = 4 + payload_words * 4;
    
    if (total_len > max_len) {
        ESP_LOGW(TAG, "INV_ACCEPTED too large");
        return -1;
    }
    
    int offset = 0;
    buffer[offset++] = NM2_CMD_INV_ACCEPTED;  // 0x10
    buffer[offset++] = payload_words;
    buffer[offset++] = name_words;
    buffer[offset++] = 0;
    
    offset += write_string_padded(&buffer[offset], name, name_words);
    if (product_len > 0) {
        offset += write_string_padded(&buffer[offset], product_id, product_words);
    }
    
    return offset;
}

int nm2_protocol_build_inv_pending(uint8_t* buffer, int max_len,
                                    const char* name, const char* product_id) {
    if (!buffer || !name) return -1;
    
    int name_len = strlen(name);
    int product_len = product_id ? strlen(product_id) : 0;
    int name_words = to_words(name_len);
    int product_words = to_words(product_len);
    int payload_words = name_words + product_words;
    int total_len = 4 + payload_words * 4;
    
    if (total_len > max_len) return -1;
    
    int offset = 0;
    buffer[offset++] = NM2_CMD_INV_PENDING;  // 0x11
    buffer[offset++] = payload_words;
    buffer[offset++] = name_words;
    buffer[offset++] = 0;
    
    offset += write_string_padded(&buffer[offset], name, name_words);
    if (product_len > 0) {
        offset += write_string_padded(&buffer[offset], product_id, product_words);
    }
    
    return offset;
}

int nm2_protocol_build_inv_auth_required(uint8_t* buffer, int max_len,
                                          const char* nonce,
                                          const char* name, const char* product_id,
                                          nm2_auth_state_t auth_state) {
    if (!buffer || !nonce || !name) return -1;
    
    int name_len = strlen(name);
    int product_len = product_id ? strlen(product_id) : 0;
    int name_words = to_words(name_len);
    int product_words = to_words(product_len);
    int payload_words = 4 + name_words + product_words;  // 4 words for nonce
    int total_len = 4 + payload_words * 4;
    
    if (total_len > max_len) return -1;
    
    int offset = 0;
    buffer[offset++] = NM2_CMD_INV_AUTH_REQUIRED;  // 0x12
    buffer[offset++] = payload_words;
    buffer[offset++] = name_words;
    buffer[offset++] = auth_state;
    
    // Write 16-byte nonce (4 words)
    memcpy(&buffer[offset], nonce, 16);
    offset += 16;
    
    offset += write_string_padded(&buffer[offset], name, name_words);
    if (product_len > 0) {
        offset += write_string_padded(&buffer[offset], product_id, product_words);
    }
    
    return offset;
}

int nm2_protocol_build_inv_user_auth_required(uint8_t* buffer, int max_len,
                                               const char* nonce,
                                               const char* name, const char* product_id,
                                               nm2_auth_state_t auth_state) {
    if (!buffer || !nonce || !name) return -1;
    
    int name_len = strlen(name);
    int product_len = product_id ? strlen(product_id) : 0;
    int name_words = to_words(name_len);
    int product_words = to_words(product_len);
    int payload_words = 4 + name_words + product_words;
    int total_len = 4 + payload_words * 4;
    
    if (total_len > max_len) return -1;
    
    int offset = 0;
    buffer[offset++] = NM2_CMD_INV_USER_AUTH_REQUIRED;  // 0x13
    buffer[offset++] = payload_words;
    buffer[offset++] = name_words;
    buffer[offset++] = auth_state;
    
    memcpy(&buffer[offset], nonce, 16);
    offset += 16;
    
    offset += write_string_padded(&buffer[offset], name, name_words);
    if (product_len > 0) {
        offset += write_string_padded(&buffer[offset], product_id, product_words);
    }
    
    return offset;
}

int nm2_protocol_build_inv_with_auth(uint8_t* buffer, int max_len,
                                      const uint8_t* auth_digest) {
    if (!buffer || !auth_digest) return -1;
    
    if (max_len < 36) return -1;
    
    int offset = 0;
    buffer[offset++] = NM2_CMD_INV_WITH_AUTH;  // 0x02
    buffer[offset++] = 8;  // 32 bytes = 8 words
    buffer[offset++] = 0;
    buffer[offset++] = 0;
    
    memcpy(&buffer[offset], auth_digest, 32);
    offset += 32;
    
    return offset;
}

int nm2_protocol_build_inv_with_user_auth(uint8_t* buffer, int max_len,
                                           const uint8_t* auth_digest,
                                           const char* username) {
    if (!buffer || !auth_digest) return -1;
    
    int username_len = username ? strlen(username) : 0;
    int username_words = to_words(username_len);
    int payload_words = 8 + username_words;
    int total_len = 4 + payload_words * 4;
    
    if (total_len > max_len) return -1;
    
    int offset = 0;
    buffer[offset++] = NM2_CMD_INV_WITH_USER_AUTH;  // 0x03
    buffer[offset++] = payload_words;
    buffer[offset++] = 0;
    buffer[offset++] = 0;
    
    memcpy(&buffer[offset], auth_digest, 32);
    offset += 32;
    
    if (username_len > 0) {
        offset += write_string_padded(&buffer[offset], username, username_words);
    }
    
    return offset;
}

int nm2_protocol_build_ping(uint8_t* buffer, int max_len, uint32_t ping_id) {
    if (!buffer || max_len < 8) return -1;
    
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

int nm2_protocol_build_ping_reply(uint8_t* buffer, int max_len, uint32_t ping_id) {
    if (!buffer || max_len < 8) return -1;
    
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

int nm2_protocol_build_bye(uint8_t* buffer, int max_len,
                            nm2_bye_reason_t reason, const char* message) {
    if (!buffer) return -1;
    
    int msg_len = message ? strlen(message) : 0;
    int msg_words = to_words(msg_len);
    int total_len = 4 + msg_words * 4;
    
    if (total_len > max_len) return -1;
    
    int offset = 0;
    buffer[offset++] = NM2_CMD_BYE;  // 0xF0
    buffer[offset++] = msg_words;
    buffer[offset++] = reason;
    buffer[offset++] = 0;
    
    if (msg_len > 0) {
        offset += write_string_padded(&buffer[offset], message, msg_words);
    }
    
    return offset;
}

int nm2_protocol_build_bye_reply(uint8_t* buffer, int max_len) {
    if (!buffer || max_len < 4) return -1;
    
    buffer[0] = NM2_CMD_BYE_REPLY;  // 0xF1
    buffer[1] = 0;
    buffer[2] = 0;
    buffer[3] = 0;
    
    return 4;
}

int nm2_protocol_build_nak(uint8_t* buffer, int max_len,
                            nm2_nak_reason_t reason,
                            const uint8_t* original_header,
                            const char* message) {
    if (!buffer) return -1;
    
    int msg_len = message ? strlen(message) : 0;
    int msg_words = to_words(msg_len);
    int payload_words = 1 + msg_words;  // 1 word for original header
    int total_len = 4 + payload_words * 4;
    
    if (total_len > max_len) return -1;
    
    int offset = 0;
    buffer[offset++] = NM2_CMD_NAK;  // 0x8F
    buffer[offset++] = payload_words;
    buffer[offset++] = reason;
    buffer[offset++] = 0;
    
    // Original command header (4 bytes)
    if (original_header) {
        memcpy(&buffer[offset], original_header, 4);
    } else {
        memset(&buffer[offset], 0, 4);
    }
    offset += 4;
    
    if (msg_len > 0) {
        offset += write_string_padded(&buffer[offset], message, msg_words);
    }
    
    return offset;
}

int nm2_protocol_build_session_reset(uint8_t* buffer, int max_len) {
    if (!buffer || max_len < 4) return -1;
    
    buffer[0] = NM2_CMD_SESSION_RESET;  // 0x82
    buffer[1] = 0;
    buffer[2] = 0;
    buffer[3] = 0;
    
    return 4;
}

int nm2_protocol_build_session_reset_reply(uint8_t* buffer, int max_len) {
    if (!buffer || max_len < 4) return -1;
    
    buffer[0] = NM2_CMD_SESSION_RESET_REPLY;  // 0x83
    buffer[1] = 0;
    buffer[2] = 0;
    buffer[3] = 0;
    
    return 4;
}

int nm2_protocol_build_ump_data(uint8_t* buffer, int max_len,
                                 uint16_t sequence,
                                 const uint8_t* ump_data, int ump_len) {
    if (!buffer || !ump_data || ump_len <= 0) return -1;
    
    int ump_words = to_words(ump_len);
    if (ump_words > NM2_MAX_PAYLOAD_WORDS) {
        ump_words = NM2_MAX_PAYLOAD_WORDS;
        ump_len = ump_words * 4;
    }
    
    int total_len = 4 + ump_words * 4;
    if (total_len > max_len) return -1;
    
    int offset = 0;
    buffer[offset++] = NM2_CMD_UMP_DATA;  // 0xFF
    buffer[offset++] = ump_words;
    buffer[offset++] = (sequence >> 8) & 0xFF;
    buffer[offset++] = sequence & 0xFF;
    
    memcpy(&buffer[offset], ump_data, ump_len);
    // Pad to word boundary
    int pad = ump_words * 4 - ump_len;
    if (pad > 0) {
        memset(&buffer[offset + ump_len], 0, pad);
    }
    offset += ump_words * 4;
    
    return offset;
}

int nm2_protocol_build_retransmit_request(uint8_t* buffer, int max_len,
                                           uint16_t sequence,
                                           uint16_t num_commands) {
    if (!buffer || max_len < 8) return -1;
    
    buffer[0] = NM2_CMD_RETRANSMIT_REQUEST;  // 0x80
    buffer[1] = 1;
    buffer[2] = (sequence >> 8) & 0xFF;
    buffer[3] = sequence & 0xFF;
    buffer[4] = (num_commands >> 8) & 0xFF;
    buffer[5] = num_commands & 0xFF;
    buffer[6] = 0;
    buffer[7] = 0;
    
    return 8;
}

int nm2_protocol_build_retransmit_error(uint8_t* buffer, int max_len,
                                         nm2_retransmit_error_t reason,
                                         uint16_t sequence) {
    if (!buffer || max_len < 8) return -1;
    
    buffer[0] = NM2_CMD_RETRANSMIT_ERROR;  // 0x81
    buffer[1] = 1;
    buffer[2] = reason;
    buffer[3] = 0;
    buffer[4] = (sequence >> 8) & 0xFF;
    buffer[5] = sequence & 0xFF;
    buffer[6] = 0;
    buffer[7] = 0;
    
    return 8;
}

/* ============================================================================
 * Command Parsing Functions
 * ============================================================================ */

bool nm2_protocol_parse_inv(const nm2_command_packet_t* cmd,
                             nm2_invitation_t* inv) {
    if (!cmd || !inv || cmd->payload_len < 4) {
        return false;
    }
    
    inv->capabilities = (nm2_capabilities_t)cmd->specific2;
    
    int name_words = cmd->specific1;
    int name_bytes = name_words * 4;
    
    if (cmd->payload_len < name_bytes) {
        return false;
    }
    
    inv->ump_endpoint_name = (const char*)cmd->payload;
    
    int product_offset = name_bytes;
    if (cmd->payload_len > product_offset) {
        inv->product_instance_id = (const char*)&cmd->payload[product_offset];
    } else {
        inv->product_instance_id = NULL;
    }
    
    return true;
}

bool nm2_protocol_parse_inv_reply(const nm2_command_packet_t* cmd,
                                   nm2_invitation_reply_t* reply) {
    if (!cmd || !reply || cmd->payload_len < 4) {
        return false;
    }
    
    int name_words = cmd->specific1;
    int name_bytes = name_words * 4;
    
    if (cmd->payload_len < name_bytes) {
        return false;
    }
    
    reply->ump_endpoint_name = (const char*)cmd->payload;
    
    int product_offset = name_bytes;
    if (cmd->payload_len > product_offset) {
        reply->product_instance_id = (const char*)&cmd->payload[product_offset];
    } else {
        reply->product_instance_id = NULL;
    }
    
    return true;
}

bool nm2_protocol_parse_auth_required(const nm2_command_packet_t* cmd,
                                       nm2_auth_required_t* auth) {
    if (!cmd || !auth || cmd->payload_len < 20) {  // 16 bytes nonce + min 4 bytes
        return false;
    }
    
    auth->auth_state = (nm2_auth_state_t)cmd->specific2;
    auth->crypto_nonce = (const char*)cmd->payload;
    
    int name_words = cmd->specific1;
    int name_offset = 16;  // After nonce
    
    if (cmd->payload_len >= name_offset + name_words * 4) {
        auth->ump_endpoint_name = (const char*)&cmd->payload[name_offset];
        
        int product_offset = name_offset + name_words * 4;
        if (cmd->payload_len > product_offset) {
            auth->product_instance_id = (const char*)&cmd->payload[product_offset];
        } else {
            auth->product_instance_id = NULL;
        }
    } else {
        auth->ump_endpoint_name = NULL;
        auth->product_instance_id = NULL;
    }
    
    return true;
}

bool nm2_protocol_parse_inv_with_auth(const nm2_command_packet_t* cmd,
                                       uint8_t* auth_digest) {
    if (!cmd || !auth_digest || cmd->payload_len < 32) {
        return false;
    }
    
    memcpy(auth_digest, cmd->payload, 32);
    return true;
}

bool nm2_protocol_parse_inv_with_user_auth(const nm2_command_packet_t* cmd,
                                            uint8_t* auth_digest,
                                            char* username, int max_len) {
    if (!cmd || !auth_digest || cmd->payload_len < 32) {
        return false;
    }
    
    memcpy(auth_digest, cmd->payload, 32);
    
    if (username && max_len > 0 && cmd->payload_len > 32) {
        int username_len = cmd->payload_len - 32;
        if (username_len >= max_len) {
            username_len = max_len - 1;
        }
        memcpy(username, &cmd->payload[32], username_len);
        username[username_len] = '\0';
    }
    
    return true;
}

bool nm2_protocol_parse_ping(const nm2_command_packet_t* cmd,
                              uint32_t* ping_id) {
    if (!cmd || !ping_id || cmd->payload_len < 4) {
        return false;
    }
    
    *ping_id = ((uint32_t)cmd->payload[0] << 24) |
               ((uint32_t)cmd->payload[1] << 16) |
               ((uint32_t)cmd->payload[2] << 8) |
               (uint32_t)cmd->payload[3];
    
    return true;
}

bool nm2_protocol_parse_bye(const nm2_command_packet_t* cmd,
                             nm2_bye_t* bye) {
    if (!cmd || !bye) {
        return false;
    }
    
    bye->reason = (nm2_bye_reason_t)cmd->specific1;
    
    if (cmd->payload_len > 0) {
        bye->text_message = (const char*)cmd->payload;
    } else {
        bye->text_message = NULL;
    }
    
    return true;
}

bool nm2_protocol_parse_nak(const nm2_command_packet_t* cmd,
                             nm2_nak_t* nak) {
    if (!cmd || !nak || cmd->payload_len < 4) {
        return false;
    }
    
    nak->reason = (nm2_nak_reason_t)cmd->specific1;
    nak->original_header = cmd->payload;
    
    if (cmd->payload_len > 4) {
        nak->text_message = (const char*)&cmd->payload[4];
    } else {
        nak->text_message = NULL;
    }
    
    return true;
}

bool nm2_protocol_parse_ump_data(const nm2_command_packet_t* cmd,
                                  uint16_t* sequence,
                                  const uint8_t** ump_data,
                                  int* ump_len) {
    if (!cmd || !sequence || !ump_data || !ump_len) {
        return false;
    }
    
    *sequence = ((uint16_t)cmd->specific1 << 8) | cmd->specific2;
    *ump_data = cmd->payload;
    *ump_len = cmd->payload_len;
    
    return true;
}

bool nm2_protocol_parse_retransmit_request(const nm2_command_packet_t* cmd,
                                            uint16_t* sequence,
                                            uint16_t* num_commands) {
    if (!cmd || !sequence || !num_commands || cmd->payload_len < 4) {
        return false;
    }
    
    *sequence = ((uint16_t)cmd->payload[0] << 8) | cmd->payload[1];
    *num_commands = ((uint16_t)cmd->payload[2] << 8) | cmd->payload[3];
    
    return true;
}

/* ============================================================================
 * Authentication Helper Functions
 * ============================================================================ */

void nm2_protocol_generate_nonce(char* nonce) {
    if (!nonce) return;
    
    const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*";
    
    for (int i = 0; i < 16; i++) {
        nonce[i] = chars[esp_random() % (sizeof(chars) - 1)];
    }
    nonce[16] = '\0';
}

void nm2_protocol_compute_auth_digest(uint8_t* digest,
                                       const char* nonce,
                                       const char* secret) {
    if (!digest || !nonce || !secret) return;
    
    char data[128];
    int len = snprintf(data, sizeof(data), "%s%s", nonce, secret);
    
    mbedtls_sha256((unsigned char*)data, len, digest, 0);  // 0 = SHA256
}

void nm2_protocol_compute_user_auth_digest(uint8_t* digest,
                                            const char* nonce,
                                            const char* username,
                                            const char* password) {
    if (!digest || !nonce || !username || !password) return;
    
    char data[256];
    int len = snprintf(data, sizeof(data), "%s%s%s", nonce, username, password);
    
    mbedtls_sha256((unsigned char*)data, len, digest, 0);
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

int nm2_protocol_get_ump_packet_size(uint8_t message_type) {
    switch (message_type) {
        case 0x0:  // Utility
        case 0x1:  // System
        case 0x3:  // Flex Data (small)
        case 0x7:  // Mixed Data Set (header)
        case 0x8:  // Stream
        case 0x9:  // MDS payload
        case 0xA:  // MDS payload
        case 0xB:  // MDS payload
        case 0xC:  // MDS payload
        case 0xD:  // MDS payload
        case 0xE:  // MDS payload
        case 0xF:  // MDS payload
            return 4;
            
        case 0x2:  // MIDI 1.0
        case 0x4:  // MIDI 2.0
        case 0x6:  // Flex Data (large)
            return 8;
            
        case 0x5:  // Jitter Reduction
            return 16;
            
        default:
            return 4;
    }
}

const char* nm2_protocol_cmd_name(nm2_command_code_t cmd) {
    switch (cmd) {
        case NM2_CMD_INV:                    return "INV";
        case NM2_CMD_INV_WITH_AUTH:          return "INV_WITH_AUTH";
        case NM2_CMD_INV_WITH_USER_AUTH:     return "INV_WITH_USER_AUTH";
        case NM2_CMD_INV_ACCEPTED:           return "INV_ACCEPTED";
        case NM2_CMD_INV_PENDING:            return "INV_PENDING";
        case NM2_CMD_INV_AUTH_REQUIRED:      return "INV_AUTH_REQUIRED";
        case NM2_CMD_INV_USER_AUTH_REQUIRED: return "INV_USER_AUTH_REQUIRED";
        case NM2_CMD_PING:                   return "PING";
        case NM2_CMD_PING_REPLY:             return "PING_REPLY";
        case NM2_CMD_RETRANSMIT_REQUEST:     return "RETRANSMIT_REQUEST";
        case NM2_CMD_RETRANSMIT_ERROR:       return "RETRANSMIT_ERROR";
        case NM2_CMD_SESSION_RESET:          return "SESSION_RESET";
        case NM2_CMD_SESSION_RESET_REPLY:    return "SESSION_RESET_REPLY";
        case NM2_CMD_NAK:                    return "NAK";
        case NM2_CMD_BYE:                    return "BYE";
        case NM2_CMD_BYE_REPLY:              return "BYE_REPLY";
        case NM2_CMD_UMP_DATA:               return "UMP_DATA";
        default:                             return "UNKNOWN";
    }
}

const char* nm2_protocol_bye_reason_name(nm2_bye_reason_t reason) {
    switch (reason) {
        case NM2_BYE_UNKNOWN:                  return "Unknown";
        case NM2_BYE_USER_TERMINATED:          return "User Terminated";
        case NM2_BYE_POWER_DOWN:               return "Power Down";
        case NM2_BYE_TOO_MANY_MISSING_PACKETS: return "Too Many Missing Packets";
        case NM2_BYE_TIMEOUT:                  return "Timeout";
        case NM2_BYE_SESSION_NOT_ESTABLISHED:  return "Session Not Established";
        case NM2_BYE_NO_PENDING_SESSION:       return "No Pending Session";
        case NM2_BYE_PROTOCOL_ERROR:           return "Protocol Error";
        case NM2_BYE_INV_TOO_MANY_SESSIONS:    return "Too Many Sessions";
        case NM2_BYE_INV_AUTH_REJECTED:        return "Auth Rejected";
        case NM2_BYE_INV_REJECTED_BY_USER:     return "Rejected By User";
        case NM2_BYE_AUTH_FAILED:              return "Auth Failed";
        case NM2_BYE_USERNAME_NOT_FOUND:       return "Username Not Found";
        case NM2_BYE_NO_MATCHING_AUTH_METHOD:  return "No Matching Auth Method";
        case NM2_BYE_INV_CANCELED:             return "Invitation Canceled";
        default:                               return "Unknown";
    }
}

const char* nm2_protocol_nak_reason_name(nm2_nak_reason_t reason) {
    switch (reason) {
        case NM2_NAK_OTHER:              return "Other";
        case NM2_NAK_CMD_NOT_SUPPORTED:  return "Command Not Supported";
        case NM2_NAK_CMD_NOT_EXPECTED:   return "Command Not Expected";
        case NM2_NAK_CMD_MALFORMED:      return "Command Malformed";
        case NM2_NAK_BAD_PING_REPLY:     return "Bad Ping Reply";
        default:                         return "Unknown";
    }
}