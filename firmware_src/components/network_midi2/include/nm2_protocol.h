/**
 * @file nm2_protocol.h
 * @brief Network MIDI 2.0 Protocol Format Handler
 * 
 * This module handles the low-level packet format for Network MIDI 2.0,
 * including UDP packet structure, command codes, and packet building/parsing.
 * 
 * Designed to be a standalone module with no dependencies on other NM2 modules,
 * ensuring low coupling and easy testing.
 * 
 * Reference: M2-124-UM_v1-0-1_Network-MIDI-2-0-UDP.pdf
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

/** UDP packet signature: "MIDI" in ASCII (0x4D494449) */
#define NM2_SIGNATURE           0x4D494449

/** Maximum UDP packet size */
#define NM2_MAX_PACKET_SIZE     1400

/** Maximum payload in 32-bit words */
#define NM2_MAX_PAYLOAD_WORDS   64

/** mDNS service type */
#define NM2_MDNS_SERVICE_TYPE   "_midi2"
#define NM2_MDNS_SERVICE_PROTO  "_udp"

/* ============================================================================
 * Command Codes (Section 3.2 of specification)
 * ============================================================================ */

/**
 * @brief Network MIDI 2.0 command codes
 */
typedef enum {
    /* Invitation Commands (0x01-0x03) */
    NM2_CMD_INV                    = 0x01,  /**< Invitation */
    NM2_CMD_INV_WITH_AUTH          = 0x02,  /**< Invitation with authentication */
    NM2_CMD_INV_WITH_USER_AUTH     = 0x03,  /**< Invitation with user authentication */
    
    /* Invitation Reply Commands (0x10-0x13) */
    NM2_CMD_INV_ACCEPTED           = 0x10,  /**< Invitation accepted */
    NM2_CMD_INV_PENDING            = 0x11,  /**< Invitation pending */
    NM2_CMD_INV_AUTH_REQUIRED      = 0x12,  /**< Authentication required */
    NM2_CMD_INV_USER_AUTH_REQUIRED = 0x13,  /**< User authentication required */
    
    /* Keep-alive Commands (0x20-0x21) */
    NM2_CMD_PING                   = 0x20,  /**< Ping request */
    NM2_CMD_PING_REPLY             = 0x21,  /**< Ping reply */
    
    /* Retransmission Commands (0x80-0x83) */
    NM2_CMD_RETRANSMIT_REQUEST     = 0x80,  /**< Retransmit request */
    NM2_CMD_RETRANSMIT_ERROR       = 0x81,  /**< Retransmit error */
    NM2_CMD_SESSION_RESET          = 0x82,  /**< Session reset */
    NM2_CMD_SESSION_RESET_REPLY    = 0x83,  /**< Session reset reply */
    
    /* Error Command (0x8F) */
    NM2_CMD_NAK                    = 0x8F,  /**< Negative acknowledgment */
    
    /* Termination Commands (0xF0-0xF1) */
    NM2_CMD_BYE                    = 0xF0,  /**< Session termination */
    NM2_CMD_BYE_REPLY              = 0xF1,  /**< Session termination reply */
    
    /* Data Command (0xFF) */
    NM2_CMD_UMP_DATA               = 0xFF,  /**< UMP data */
} nm2_command_code_t;

/* ============================================================================
 * Enumerations
 * ============================================================================ */

/**
 * @brief BYE reason codes (Section 3.2.3)
 */
typedef enum {
    NM2_BYE_UNKNOWN                  = 0x00,  /**< Unknown reason */
    NM2_BYE_USER_TERMINATED          = 0x01,  /**< User terminated */
    NM2_BYE_POWER_DOWN               = 0x02,  /**< Power down */
    NM2_BYE_TOO_MANY_MISSING_PACKETS = 0x03,  /**< Too many missing packets */
    NM2_BYE_TIMEOUT                  = 0x04,  /**< Timeout */
    NM2_BYE_SESSION_NOT_ESTABLISHED  = 0x05,  /**< Session not established */
    NM2_BYE_NO_PENDING_SESSION       = 0x06,  /**< No pending session */
    NM2_BYE_PROTOCOL_ERROR           = 0x07,  /**< Protocol error */
    
    /* Invitation rejection reasons */
    NM2_BYE_INV_TOO_MANY_SESSIONS    = 0x40,  /**< Too many sessions */
    NM2_BYE_INV_AUTH_REJECTED        = 0x41,  /**< Authentication rejected */
    NM2_BYE_INV_REJECTED_BY_USER     = 0x42,  /**< Rejected by user */
    NM2_BYE_AUTH_FAILED              = 0x43,  /**< Authentication failed */
    NM2_BYE_USERNAME_NOT_FOUND       = 0x44,  /**< Username not found */
    NM2_BYE_NO_MATCHING_AUTH_METHOD  = 0x45,  /**< No matching auth method */
    NM2_BYE_INV_CANCELED             = 0x80,  /**< Invitation canceled */
} nm2_bye_reason_t;

/**
 * @brief NAK reason codes (Section 3.2.4)
 */
typedef enum {
    NM2_NAK_OTHER               = 0x00,  /**< Other error */
    NM2_NAK_CMD_NOT_SUPPORTED   = 0x01,  /**< Command not supported */
    NM2_NAK_CMD_NOT_EXPECTED    = 0x02,  /**< Command not expected */
    NM2_NAK_CMD_MALFORMED       = 0x03,  /**< Command malformed */
    NM2_NAK_BAD_PING_REPLY      = 0x20,  /**< Bad ping reply */
} nm2_nak_reason_t;

/**
 * @brief Retransmit error reasons (Section 3.2.5)
 */
typedef enum {
    NM2_RETRANSMIT_ERR_UNKNOWN              = 0x00,  /**< Unknown error */
    NM2_RETRANSMIT_ERR_BUFFER_NOT_CONTAIN   = 0x01,  /**< Buffer doesn't contain sequence */
} nm2_retransmit_error_t;

/**
 * @brief Authentication state (Section 3.2.2)
 */
typedef enum {
    NM2_AUTH_STATE_FIRST_REQUEST     = 0x00,  /**< First authentication request */
    NM2_AUTH_STATE_DIGEST_INCORRECT  = 0x01,  /**< Digest incorrect */
    NM2_AUTH_STATE_USERNAME_NOT_FOUND = 0x02,  /**< Username not found */
} nm2_auth_state_t;

/**
 * @brief Invitation capabilities flags (Section 3.2.1)
 */
typedef enum {
    NM2_CAP_NONE           = 0x00,  /**< No special capabilities */
    NM2_CAP_SUPPORTS_AUTH  = 0x01,  /**< Supports shared secret auth */
    NM2_CAP_SUPPORTS_USER  = 0x02,  /**< Supports user auth */
    NM2_CAP_ALL            = 0x03,  /**< All capabilities */
} nm2_capabilities_t;

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/**
 * @brief Parsed command packet structure
 */
typedef struct {
    nm2_command_code_t command;       /**< Command code */
    uint8_t payload_words;            /**< Payload length in 32-bit words */
    uint8_t specific1;                /**< Command specific field 1 */
    uint8_t specific2;                /**< Command specific field 2 */
    const uint8_t* payload;           /**< Pointer to payload data */
    uint16_t payload_len;             /**< Payload length in bytes */
} nm2_command_packet_t;

/**
 * @brief Parsed invitation data
 */
typedef struct {
    const char* ump_endpoint_name;    /**< UMP endpoint name */
    const char* product_instance_id;  /**< Product instance ID */
    nm2_capabilities_t capabilities;  /**< Capabilities flags */
} nm2_invitation_t;

/**
 * @brief Parsed invitation reply data
 */
typedef struct {
    const char* ump_endpoint_name;    /**< UMP endpoint name */
    const char* product_instance_id;  /**< Product instance ID */
} nm2_invitation_reply_t;

/**
 * @brief Parsed auth required data
 */
typedef struct {
    const char* crypto_nonce;         /**< 16-byte crypto nonce */
    const char* ump_endpoint_name;    /**< UMP endpoint name */
    const char* product_instance_id;  /**< Product instance ID */
    nm2_auth_state_t auth_state;      /**< Authentication state */
} nm2_auth_required_t;

/**
 * @brief Parsed BYE data
 */
typedef struct {
    nm2_bye_reason_t reason;          /**< BYE reason */
    const char* text_message;         /**< Optional text message */
} nm2_bye_t;

/**
 * @brief Parsed NAK data
 */
typedef struct {
    nm2_nak_reason_t reason;          /**< NAK reason */
    const uint8_t* original_header;   /**< Original command header (4 bytes) */
    const char* text_message;         /**< Optional text message */
} nm2_nak_t;

/* ============================================================================
 * UDP Packet Functions
 * ============================================================================ */

/**
 * @brief Build a UDP packet with signature and command packets
 * 
 * @param buffer Output buffer (must be at least NM2_MAX_PACKET_SIZE bytes)
 * @param max_len Maximum buffer length
 * @param commands Array of command packets to include
 * @param count Number of command packets
 * @return Total packet length, or negative on error
 */
int nm2_protocol_build_packet(uint8_t* buffer, int max_len,
                               const nm2_command_packet_t* commands, int count);

/**
 * @brief Parse a UDP packet into command packets
 * 
 * @param data Input packet data
 * @param len Packet length
 * @param commands Output array for parsed commands
 * @param max_count Maximum number of commands to parse
 * @return Number of commands parsed, or negative on error
 */
int nm2_protocol_parse_packet(const uint8_t* data, int len,
                               nm2_command_packet_t* commands, int max_count);

/**
 * @brief Check if a packet has valid signature
 * 
 * @param data Packet data
 * @param len Packet length
 * @return true if signature is valid
 */
bool nm2_protocol_validate_signature(const uint8_t* data, int len);

/* ============================================================================
 * Command Building Functions
 * ============================================================================ */

/**
 * @brief Build INV command
 */
int nm2_protocol_build_inv(uint8_t* buffer, int max_len,
                            const char* name, const char* product_id,
                            nm2_capabilities_t caps);

/**
 * @brief Build INV_ACCEPTED command
 */
int nm2_protocol_build_inv_accepted(uint8_t* buffer, int max_len,
                                     const char* name, const char* product_id);

/**
 * @brief Build INV_PENDING command
 */
int nm2_protocol_build_inv_pending(uint8_t* buffer, int max_len,
                                    const char* name, const char* product_id);

/**
 * @brief Build INV_AUTH_REQUIRED command
 */
int nm2_protocol_build_inv_auth_required(uint8_t* buffer, int max_len,
                                          const char* nonce,
                                          const char* name, const char* product_id,
                                          nm2_auth_state_t auth_state);

/**
 * @brief Build INV_USER_AUTH_REQUIRED command
 */
int nm2_protocol_build_inv_user_auth_required(uint8_t* buffer, int max_len,
                                               const char* nonce,
                                               const char* name, const char* product_id,
                                               nm2_auth_state_t auth_state);

/**
 * @brief Build INV_WITH_AUTH command
 */
int nm2_protocol_build_inv_with_auth(uint8_t* buffer, int max_len,
                                      const uint8_t* auth_digest);

/**
 * @brief Build INV_WITH_USER_AUTH command
 */
int nm2_protocol_build_inv_with_user_auth(uint8_t* buffer, int max_len,
                                           const uint8_t* auth_digest,
                                           const char* username);

/**
 * @brief Build PING command
 */
int nm2_protocol_build_ping(uint8_t* buffer, int max_len, uint32_t ping_id);

/**
 * @brief Build PING_REPLY command
 */
int nm2_protocol_build_ping_reply(uint8_t* buffer, int max_len, uint32_t ping_id);

/**
 * @brief Build BYE command
 */
int nm2_protocol_build_bye(uint8_t* buffer, int max_len,
                            nm2_bye_reason_t reason, const char* message);

/**
 * @brief Build BYE_REPLY command
 */
int nm2_protocol_build_bye_reply(uint8_t* buffer, int max_len);

/**
 * @brief Build NAK command
 */
int nm2_protocol_build_nak(uint8_t* buffer, int max_len,
                            nm2_nak_reason_t reason,
                            const uint8_t* original_header,
                            const char* message);

/**
 * @brief Build SESSION_RESET command
 */
int nm2_protocol_build_session_reset(uint8_t* buffer, int max_len);

/**
 * @brief Build SESSION_RESET_REPLY command
 */
int nm2_protocol_build_session_reset_reply(uint8_t* buffer, int max_len);

/**
 * @brief Build UMP_DATA command
 */
int nm2_protocol_build_ump_data(uint8_t* buffer, int max_len,
                                 uint16_t sequence,
                                 const uint8_t* ump_data, int ump_len);

/**
 * @brief Build RETRANSMIT_REQUEST command
 */
int nm2_protocol_build_retransmit_request(uint8_t* buffer, int max_len,
                                           uint16_t sequence,
                                           uint16_t num_commands);

/**
 * @brief Build RETRANSMIT_ERROR command
 */
int nm2_protocol_build_retransmit_error(uint8_t* buffer, int max_len,
                                         nm2_retransmit_error_t reason,
                                         uint16_t sequence);

/* ============================================================================
 * Command Parsing Functions
 * ============================================================================ */

/**
 * @brief Parse INV command
 */
bool nm2_protocol_parse_inv(const nm2_command_packet_t* cmd,
                             nm2_invitation_t* inv);

/**
 * @brief Parse INV_ACCEPTED/PENDING command
 */
bool nm2_protocol_parse_inv_reply(const nm2_command_packet_t* cmd,
                                   nm2_invitation_reply_t* reply);

/**
 * @brief Parse INV_AUTH_REQUIRED/USER_AUTH_REQUIRED command
 */
bool nm2_protocol_parse_auth_required(const nm2_command_packet_t* cmd,
                                       nm2_auth_required_t* auth);

/**
 * @brief Parse INV_WITH_AUTH command
 */
bool nm2_protocol_parse_inv_with_auth(const nm2_command_packet_t* cmd,
                                       uint8_t* auth_digest);

/**
 * @brief Parse INV_WITH_USER_AUTH command
 */
bool nm2_protocol_parse_inv_with_user_auth(const nm2_command_packet_t* cmd,
                                            uint8_t* auth_digest,
                                            char* username, int max_len);

/**
 * @brief Parse PING/PING_REPLY command
 */
bool nm2_protocol_parse_ping(const nm2_command_packet_t* cmd,
                              uint32_t* ping_id);

/**
 * @brief Parse BYE command
 */
bool nm2_protocol_parse_bye(const nm2_command_packet_t* cmd,
                             nm2_bye_t* bye);

/**
 * @brief Parse NAK command
 */
bool nm2_protocol_parse_nak(const nm2_command_packet_t* cmd,
                             nm2_nak_t* nak);

/**
 * @brief Parse UMP_DATA command
 */
bool nm2_protocol_parse_ump_data(const nm2_command_packet_t* cmd,
                                  uint16_t* sequence,
                                  const uint8_t** ump_data,
                                  int* ump_len);

/**
 * @brief Parse RETRANSMIT_REQUEST command
 */
bool nm2_protocol_parse_retransmit_request(const nm2_command_packet_t* cmd,
                                            uint16_t* sequence,
                                            uint16_t* num_commands);

/* ============================================================================
 * Authentication Helper Functions
 * ============================================================================ */

/**
 * @brief Generate a random crypto nonce
 * @param nonce Output buffer (must be at least 17 bytes for 16 chars + null)
 */
void nm2_protocol_generate_nonce(char* nonce);

/**
 * @brief Compute authentication digest (SHA256)
 * @param digest Output buffer (must be 32 bytes)
 * @param nonce 16-byte crypto nonce
 * @param secret Shared secret
 */
void nm2_protocol_compute_auth_digest(uint8_t* digest,
                                       const char* nonce,
                                       const char* secret);

/**
 * @brief Compute user authentication digest (SHA256)
 * @param digest Output buffer (must be 32 bytes)
 * @param nonce 16-byte crypto nonce
 * @param username Username
 * @param password Password
 */
void nm2_protocol_compute_user_auth_digest(uint8_t* digest,
                                            const char* nonce,
                                            const char* username,
                                            const char* password);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Get UMP packet size from message type
 * @param message_type UMP message type (upper 4 bits of first byte)
 * @return Packet size in bytes
 */
int nm2_protocol_get_ump_packet_size(uint8_t message_type);

/**
 * @brief Get command code name string
 * @param cmd Command code
 * @return Human-readable command name
 */
const char* nm2_protocol_cmd_name(nm2_command_code_t cmd);

/**
 * @brief Get BYE reason name string
 */
const char* nm2_protocol_bye_reason_name(nm2_bye_reason_t reason);

/**
 * @brief Get NAK reason name string
 */
const char* nm2_protocol_nak_reason_name(nm2_nak_reason_t reason);

/* ============================================================================
 * Session ID Generation
 * ============================================================================ */

/**
 * @brief Generate a random session ID
 * @return Non-zero session ID
 */
uint32_t nm2_protocol_generate_session_id(void);

/* ============================================================================
 * Sequence Number Utilities
 * ============================================================================ */

/**
 * @brief Check if a sequence number is newer (handles wrap-around)
 * @param new_seq New sequence number
 * @param old_seq Old sequence number
 * @return true if new_seq is newer than old_seq
 */
bool nm2_protocol_is_sequence_newer(uint16_t new_seq, uint16_t old_seq);

/**
 * @brief Calculate sequence number difference (handles wrap-around)
 * @param new_seq New sequence number
 * @param old_seq Old sequence number
 * @return Difference (positive if new_seq is newer)
 */
int32_t nm2_protocol_sequence_diff(uint16_t new_seq, uint16_t old_seq);

/* ============================================================================
 * Retransmit Buffer
 * ============================================================================ */

/** Maximum number of cached packets for retransmission */
#define NM2_RETRANSMIT_CACHE_SIZE   64

/**
 * @brief Cached packet entry for retransmission
 */
typedef struct {
    uint16_t sequence;              /**< Sequence number */
    uint16_t length;                /**< Packet length */
    uint32_t timestamp_ms;          /**< Send timestamp (milliseconds) */
    uint8_t data[NM2_MAX_PACKET_SIZE]; /**< Packet data */
    bool valid;                     /**< Entry is valid */
} nm2_packet_cache_entry_t;

/**
 * @brief Retransmit buffer for caching sent packets
 */
typedef struct {
    nm2_packet_cache_entry_t entries[NM2_RETRANSMIT_CACHE_SIZE];
    uint16_t count;                 /**< Number of valid entries */
    uint16_t head;                  /**< Next write position */
} nm2_retransmit_buffer_t;

/**
 * @brief Initialize a retransmit buffer
 * @param buf Buffer to initialize
 */
void nm2_retransmit_buffer_init(nm2_retransmit_buffer_t* buf);

/**
 * @brief Add a packet to the retransmit buffer
 * @param buf Buffer to add to
 * @param sequence Sequence number
 * @param data Packet data
 * @param length Packet length
 * @return true if added successfully
 */
bool nm2_retransmit_buffer_add(nm2_retransmit_buffer_t* buf,
                                uint16_t sequence,
                                const uint8_t* data,
                                uint16_t length);

/**
 * @brief Get a packet from the retransmit buffer
 * @param buf Buffer to search
 * @param sequence Sequence number to find
 * @param data Output buffer for packet data
 * @param max_len Maximum output buffer length
 * @return Packet length, or negative if not found
 */
int nm2_retransmit_buffer_get(nm2_retransmit_buffer_t* buf,
                               uint16_t sequence,
                               uint8_t* data,
                               uint16_t max_len);

/**
 * @brief Remove old entries from the buffer
 * @param buf Buffer to clean
 * @param max_age_ms Maximum age in milliseconds
 */
void nm2_retransmit_buffer_clean(nm2_retransmit_buffer_t* buf,
                                  uint32_t max_age_ms);

/**
 * @brief Clear all entries from the buffer
 * @param buf Buffer to clear
 */
void nm2_retransmit_buffer_clear(nm2_retransmit_buffer_t* buf);

#ifdef __cplusplus
}
#endif