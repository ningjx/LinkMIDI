#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file midi_error.h
 * @brief Unified error codes for LinkMIDI project
 * 
 * This module provides standardized error handling across all components.
 * All public APIs should return midi_error_t type for consistency.
 */

/**
 * @brief Unified error code enumeration
 * 
 * Error codes are organized by category:
 * - 0x00xx: Success/General
 * - 0x01xx: System/Memory errors
 * - 0x02xx: Network errors
 * - 0x03xx: USB errors
 * - 0x04xx: MIDI protocol errors
 * - 0x05xx: Configuration errors
 * - 0x06xx: Session errors
 */
typedef enum {
    /* ==========================================================================
     * Success/General (0x00xx)
     * ========================================================================== */
    MIDI_OK                         = 0x0000,  ///< Operation successful
    MIDI_OK_ASYNC                   = 0x0001,  ///< Operation started, will complete async
    
    /* ==========================================================================
     * System/Memory Errors (0x01xx)
     * ========================================================================== */
    MIDI_ERR_UNKNOWN                = 0x0100,  ///< Unknown/unspecified error
    MIDI_ERR_NULL_PTR               = 0x0101,  ///< Null pointer passed
    MIDI_ERR_INVALID_ARG            = 0x0102,  ///< Invalid argument
    MIDI_ERR_NO_MEM                 = 0x0103,  ///< Out of memory
    MIDI_ERR_TIMEOUT                = 0x0104,  ///< Operation timed out
    MIDI_ERR_NOT_INITIALIZED        = 0x0105,  ///< Module not initialized
    MIDI_ERR_ALREADY_INITIALIZED    = 0x0106,  ///< Module already initialized
    MIDI_ERR_NOT_FOUND              = 0x0107,  ///< Resource not found
    MIDI_ERR_BUFFER_OVERFLOW        = 0x0108,  ///< Buffer overflow
    MIDI_ERR_BUFFER_EMPTY           = 0x0109,  ///< Buffer empty
    MIDI_ERR_BUSY                   = 0x010A,  ///< Resource busy
    MIDI_ERR_NOT_SUPPORTED          = 0x010B,  ///< Operation not supported
    MIDI_ERR_STORAGE_ERROR          = 0x010C,  ///< Storage operation failed
    
    /* ==========================================================================
     * Network Errors (0x02xx)
     * ========================================================================== */
    MIDI_ERR_NET_BASE               = 0x0200,  ///< Network error base
    MIDI_ERR_NET_INIT_FAILED        = 0x0201,  ///< Network initialization failed
    MIDI_ERR_NET_NOT_CONNECTED      = 0x0202,  ///< Network not connected
    MIDI_ERR_NET_CONNECTION_LOST    = 0x0203,  ///< Network connection lost
    MIDI_ERR_NET_SOCKET_ERROR       = 0x0204,  ///< Socket error
    MIDI_ERR_NET_BIND_FAILED        = 0x0205,  ///< Socket bind failed
    MIDI_ERR_NET_SEND_FAILED        = 0x0206,  ///< Send operation failed
    MIDI_ERR_NET_RECV_FAILED        = 0x0207,  ///< Receive operation failed
    MIDI_ERR_NET_DNS_FAILED         = 0x0208,  ///< DNS resolution failed
    MIDI_ERR_NET_MDNS_FAILED        = 0x0209,  ///< mDNS operation failed
    
    /* ==========================================================================
     * USB Errors (0x03xx)
     * ========================================================================== */
    MIDI_ERR_USB_BASE               = 0x0300,  ///< USB error base
    MIDI_ERR_USB_INIT_FAILED        = 0x0301,  ///< USB initialization failed
    MIDI_ERR_USB_NOT_CONNECTED      = 0x0302,  ///< USB device not connected
    MIDI_ERR_USB_DEVICE_ERROR       = 0x0303,  ///< USB device error
    MIDI_ERR_USB_TRANSFER_FAILED    = 0x0304,  ///< USB transfer failed
    MIDI_ERR_USB_ENUM_FAILED        = 0x0305,  ///< USB enumeration failed
    MIDI_ERR_USB_NO_DRIVER          = 0x0306,  ///< No driver for device
    MIDI_ERR_USB_TIMEOUT            = 0x0307,  ///< USB operation timeout
    MIDI_ERR_USB_DISCONNECTED       = 0x0308,  ///< USB device disconnected
    MIDI_ERR_USB_MAX_DEVICES        = 0x0309,  ///< Maximum devices reached
    
    /* ==========================================================================
     * MIDI Protocol Errors (0x04xx)
     * ========================================================================== */
    MIDI_ERR_MIDI_BASE              = 0x0400,  ///< MIDI error base
    MIDI_ERR_MIDI_INVALID_MSG       = 0x0401,  ///< Invalid MIDI message
    MIDI_ERR_MIDI_PARSE_FAILED      = 0x0402,  ///< MIDI parsing failed
    MIDI_ERR_MIDI_CONVERT_FAILED    = 0x0403,  ///< MIDI conversion failed
    MIDI_ERR_MIDI_UNKNOWN_MSG       = 0x0404,  ///< Unknown MIDI message type
    MIDI_ERR_MIDI_UMP_INVALID       = 0x0405,  ///< Invalid UMP packet
    
    /* ==========================================================================
     * Configuration Errors (0x05xx)
     * ========================================================================== */
    MIDI_ERR_CONFIG_BASE            = 0x0500,  ///< Config error base
    MIDI_ERR_CONFIG_INVALID         = 0x0501,  ///< Invalid configuration
    MIDI_ERR_CONFIG_NOT_FOUND       = 0x0502,  ///< Configuration not found
    MIDI_ERR_CONFIG_SAVE_FAILED     = 0x0503,  ///< Failed to save configuration
    MIDI_ERR_CONFIG_LOAD_FAILED     = 0x0504,  ///< Failed to load configuration
    MIDI_ERR_CONFIG_NVS_ERROR       = 0x0505,  ///< NVS storage error
    
    /* ==========================================================================
     * Session Errors (0x06xx)
     * ========================================================================== */
    MIDI_ERR_SESSION_BASE           = 0x0600,  ///< Session error base
    MIDI_ERR_SESSION_NOT_ACTIVE     = 0x0601,  ///< No active session
    MIDI_ERR_SESSION_ALREADY_ACTIVE = 0x0602,  ///< Session already active
    MIDI_ERR_SESSION_INIT_FAILED    = 0x0603,  ///< Session initialization failed
    MIDI_ERR_SESSION_REJECTED       = 0x0604,  ///< Session rejected by peer
    MIDI_ERR_SESSION_TIMEOUT        = 0x0605,  ///< Session establishment timeout
    MIDI_ERR_SESSION_CLOSED         = 0x0606,  ///< Session closed by peer
    
} midi_error_t;

/**
 * @brief Check if error code indicates success
 * @param err Error code to check
 * @return true if successful
 */
static inline bool midi_is_success(midi_error_t err) {
    return (err == MIDI_OK || err == MIDI_OK_ASYNC);
}

/**
 * @brief Check if error code indicates failure
 * @param err Error code to check
 * @return true if error
 */
static inline bool midi_is_error(midi_error_t err) {
    return !midi_is_success(err);
}

/**
 * @brief Get human-readable error string
 * @param err Error code
 * @return Static string describing the error (never NULL)
 */
const char* midi_error_str(midi_error_t err);

/**
 * @brief Get short error name (enum name)
 * @param err Error code
 * @return Static string with the error name (never NULL)
 */
const char* midi_error_name(midi_error_t err);

/**
 * @brief Get error category name
 * @param err Error code
 * @return Static string with category name (never NULL)
 */
const char* midi_error_category(midi_error_t err);

#ifdef __cplusplus
}
#endif