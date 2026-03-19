#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file network_midi2.h
 * @brief MIDI 2.0 Network (UDP) Protocol Implementation for ESP32
 * 
 * This library implements Network MIDI 2.0 (UDP) specification with support for:
 * 1. Discovery (mDNS/DNS-SD)
 * 2. Session Management (Invitation, Acceptance, Termination)
 * 3. Data Transmission (UMP over UDP)
 */

/* ============================================================================
 * Data Types
 * ============================================================================ */

/**
 * @brief Callback function type for log messages
 * @param message Log message string
 */
typedef void (*network_midi2_log_callback_t)(const char* message);

/**
 * @brief Callback function type for MIDI data reception
 * @param data MIDI data (3 bytes: status, data1, data2)
 * @param length Always 3 for MIDI 1.0
 */
typedef void (*network_midi2_midi_rx_callback_t)(const uint8_t* data, uint16_t length);

/**
 * @brief Callback function type for UMP data reception
 * @param data Raw UMP data
 * @param length UMP data length (typically 4 or 8 bytes per packet)
 */
typedef void (*network_midi2_ump_rx_callback_t)(const uint8_t* data, uint16_t length);

/**
 * @brief Session state enumeration
 */
typedef enum {
    SESSION_STATE_IDLE,           ///< No active session
    SESSION_STATE_INV_PENDING,    ///< Waiting for invitation acceptance
    SESSION_STATE_ACTIVE,         ///< Session is established
    SESSION_STATE_CLOSING         ///< Session termination in progress
} network_midi2_session_state_t;

/**
 * @brief Device mode enumeration
 */
typedef enum {
    MODE_CLIENT = 0,              ///< Only act as client (initiates sessions)
    MODE_SERVER = 1,              ///< Only act as server (accepts sessions)
    MODE_PEER = 2                 ///< Both client and server (bidirectional)
} network_midi2_device_mode_t;

/**
 * @brief Session information structure
 */
typedef struct {
    char device_name[64];         ///< Device/endpoint name
    char product_id[64];          ///< Product identifier
    uint32_t ip_address;          ///< Remote device IP (network byte order)
    uint16_t port;                ///< Remote device UDP port
    uint32_t local_ssrc;          ///< Local SSRC (Synchronization Source, 32-bit)
    uint32_t remote_ssrc;         ///< Remote SSRC (32-bit)
    uint16_t sequence_number;     ///< Current sequence number for UMP data
    network_midi2_session_state_t state;
} network_midi2_session_t;

/**
 * @brief Configuration structure for MIDI 2.0 network device
 */
typedef struct {
    const char* device_name;              ///< Device name for discovery
    const char* product_id;               ///< Product identifier
    uint16_t listen_port;                 ///< UDP port to listen on (default: 5507)
    network_midi2_device_mode_t mode;     ///< Device role (client/server/peer)
    bool enable_discovery;                ///< Enable mDNS discovery announcement
    
    network_midi2_log_callback_t log_callback;
    network_midi2_midi_rx_callback_t midi_rx_callback;
    network_midi2_ump_rx_callback_t ump_rx_callback;
} network_midi2_config_t;

/**
 * @brief Network MIDI 2.0 context structure (opaque)
 */
typedef struct network_midi2_context network_midi2_context_t;

/* ============================================================================
 * Core Functions
 * ============================================================================ */

/**
 * @brief Initialize Network MIDI 2.0 context with default configuration
 * @param device_name Name of this device
 * @param product_id Product identifier  
 * @param port UDP port to listen on
 * @return Context handle, or NULL on error
 */
network_midi2_context_t* network_midi2_init(
    const char* device_name,
    const char* product_id,
    uint16_t port);

/**
 * @brief Initialize Network MIDI 2.0 context with custom configuration
 * @param config Configuration structure
 * @return Context handle, or NULL on error
 */
network_midi2_context_t* network_midi2_init_with_config(
    const network_midi2_config_t* config);

/**
 * @brief Cleanup and free Network MIDI 2.0 context
 * @param ctx Context handle
 */
void network_midi2_deinit(network_midi2_context_t* ctx);

/**
 * @brief Start the Network MIDI 2.0 device
 * @param ctx Context handle
 * @return true on success
 */
bool network_midi2_start(network_midi2_context_t* ctx);

/**
 * @brief Stop the Network MIDI 2.0 device
 * @param ctx Context handle
 */
void network_midi2_stop(network_midi2_context_t* ctx);

/* ============================================================================
 * Discovery Functions (Section 1)
 * ============================================================================ */

/**
 * @brief Send mDNS discovery query to find MIDI 2.0 devices
 * @param ctx Context handle
 * @return true on success
 */
bool network_midi2_send_discovery_query(network_midi2_context_t* ctx);

/**
 * @brief Get discovered device information
 * @param ctx Context handle
 * @param index Device index (0-based)
 * @param device_name Output buffer for device name (at least 64 bytes)
 * @param ip_address Output buffer for IP address
 * @param port Output buffer for UDP port
 * @return true if device exists, false otherwise
 */
bool network_midi2_get_discovered_device(
    network_midi2_context_t* ctx,
    int index,
    char* device_name,
    uint32_t* ip_address,
    uint16_t* port);

/**
 * @brief Get number of discovered devices
 * @param ctx Context handle
 * @return Number of discovered devices
 */
int network_midi2_get_device_count(network_midi2_context_t* ctx);

/* ============================================================================
 * Session Functions (Section 2)
 * ============================================================================ */

/**
 * @brief Initiate a session with a remote host (Client role)
 * @param ctx Context handle
 * @param ip_address Remote host IP address (network byte order)
 * @param port Remote host UDP port
 * @param remote_device_name Name of remote device (optional, can be NULL)
 * @return true if invitation sent successfully
 */
bool network_midi2_session_initiate(
    network_midi2_context_t* ctx,
    uint32_t ip_address,
    uint16_t port,
    const char* remote_device_name);

/**
 * @brief Accept a pending session invitation (Server role)
 * @param ctx Context handle
 * @return true on success
 */
bool network_midi2_session_accept(network_midi2_context_t* ctx);

/**
 * @brief Reject a pending session invitation (Server role)
 * @param ctx Context handle
 * @return true on success
 */
bool network_midi2_session_reject(network_midi2_context_t* ctx);

/**
 * @brief Terminate the current session
 * @param ctx Context handle
 * @return true on success
 */
bool network_midi2_session_terminate(network_midi2_context_t* ctx);

/**
 * @brief Get current session state
 * @param ctx Context handle
 * @return Current session state
 */
network_midi2_session_state_t network_midi2_get_session_state(
    network_midi2_context_t* ctx);

/**
 * @brief Check if session is active (fully connected)
 * @param ctx Context handle
 * @return true if session is active
 */
bool network_midi2_is_session_active(network_midi2_context_t* ctx);

/**
 * @brief Send a ping command to keep session alive
 * @param ctx Context handle
 * @return true if ping sent successfully
 */
bool network_midi2_send_ping(network_midi2_context_t* ctx);

/**
 * @brief Get current session information
 * @param ctx Context handle
 * @param session Output session information
 * @return true if session exists and info was filled
 */
bool network_midi2_get_session_info(
    network_midi2_context_t* ctx,
    network_midi2_session_t* session);

/* ============================================================================
 * Data Transmission Functions (Section 3)
 * ============================================================================ */

/**
 * @brief Send MIDI 1.0 message (3 bytes) via Network MIDI 2.0
 * Converts to UMP System Real Time Message (MIDI 1.0)
 * @param ctx Context handle
 * @param status Status byte (0x80-0xEF)
 * @param data1 First data byte
 * @param data2 Second data byte
 * @return true if sent successfully
 */
bool network_midi2_send_midi(
    network_midi2_context_t* ctx,
    uint8_t status,
    uint8_t data1,
    uint8_t data2);

/**
 * @brief Send raw UMP data
 * @param ctx Context handle
 * @param ump_data Raw UMP packet data (typically 4 or 8 bytes)
 * @param length Length of UMP data
 * @return true if sent successfully
 */
bool network_midi2_send_ump(
    network_midi2_context_t* ctx,
    const uint8_t* ump_data,
    uint16_t length);

/**
 * @brief Send Note On message
 * @param ctx Context handle
 * @param note Note number (0-127)
 * @param velocity Velocity (0-127)
 * @param channel MIDI channel (0-15)
 * @return true if sent successfully
 */
bool network_midi2_send_note_on(
    network_midi2_context_t* ctx,
    uint8_t note,
    uint8_t velocity,
    uint8_t channel);

/**
 * @brief Send Note Off message
 * @param ctx Context handle
 * @param note Note number (0-127)
 * @param velocity Velocity (0-127)
 * @param channel MIDI channel (0-15)
 * @return true if sent successfully
 */
bool network_midi2_send_note_off(
    network_midi2_context_t* ctx,
    uint8_t note,
    uint8_t velocity,
    uint8_t channel);

/**
 * @brief Send Control Change message
 * @param ctx Context handle
 * @param controller Controller number (0-127)
 * @param value Controller value (0-127)
 * @param channel MIDI channel (0-15)
 * @return true if sent successfully
 */
bool network_midi2_send_control_change(
    network_midi2_context_t* ctx,
    uint8_t controller,
    uint8_t value,
    uint8_t channel);

/**
 * @brief Send Program Change message
 * @param ctx Context handle
 * @param program Program number (0-127)
 * @param channel MIDI channel (0-15)
 * @return true if sent successfully
 */
bool network_midi2_send_program_change(
    network_midi2_context_t* ctx,
    uint8_t program,
    uint8_t channel);

/**
 * @brief Send Pitch Bend message
 * @param ctx Context handle
 * @param bend Bend value (-8192 to 8191, 0 = center)
 * @param channel MIDI channel (0-15)
 * @return true if sent successfully
 */
bool network_midi2_send_pitch_bend(
    network_midi2_context_t* ctx,
    int16_t bend,
    uint8_t channel);

/* ============================================================================
 * Callback Registration
 * ============================================================================ */

/**
 * @brief Set logging callback
 * @param ctx Context handle
 * @param callback Callback function
 */
void network_midi2_set_log_callback(
    network_midi2_context_t* ctx,
    network_midi2_log_callback_t callback);

/**
 * @brief Set MIDI reception callback
 * @param ctx Context handle
 * @param callback Callback function
 */
void network_midi2_set_midi_rx_callback(
    network_midi2_context_t* ctx,
    network_midi2_midi_rx_callback_t callback);

/**
 * @brief Set UMP reception callback
 * @param ctx Context handle
 * @param callback Callback function
 */
void network_midi2_set_ump_rx_callback(
    network_midi2_context_t* ctx,
    network_midi2_ump_rx_callback_t callback);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Get library version string
 * @return Version string (e.g., "1.0.0")
 */
const char* network_midi2_get_version(void);

/**
 * @brief Convert MIDI 1.0 status+data to string representation
 * @param status Status byte
 * @param data1 First data byte
 * @param data2 Second data byte
 * @param buffer Output buffer
 * @param buffer_len Buffer length
 * @return Number of bytes written
 */
int network_midi2_midi_to_string(
    uint8_t status,
    uint8_t data1,
    uint8_t data2,
    char* buffer,
    int buffer_len);

#ifdef __cplusplus
}
#endif

