#include <stddef.h>
#include "midi_error.h"

/**
 * @file midi_error.c
 * @brief Error code string conversion implementation
 */

/* Error name lookup table */
static const struct {
    midi_error_t code;
    const char* name;
    const char* desc;
} error_table[] = {
    /* Success/General */
    { MIDI_OK,                       "MIDI_OK",                       "Success" },
    { MIDI_OK_ASYNC,                 "MIDI_OK_ASYNC",                 "Operation started (async)" },
    
    /* System/Memory */
    { MIDI_ERR_UNKNOWN,              "MIDI_ERR_UNKNOWN",              "Unknown error" },
    { MIDI_ERR_NULL_PTR,             "MIDI_ERR_NULL_PTR",             "Null pointer" },
    { MIDI_ERR_INVALID_ARG,          "MIDI_ERR_INVALID_ARG",          "Invalid argument" },
    { MIDI_ERR_NO_MEM,               "MIDI_ERR_NO_MEM",               "Out of memory" },
    { MIDI_ERR_TIMEOUT,              "MIDI_ERR_TIMEOUT",              "Operation timed out" },
    { MIDI_ERR_NOT_INITIALIZED,      "MIDI_ERR_NOT_INITIALIZED",      "Not initialized" },
    { MIDI_ERR_ALREADY_INITIALIZED,  "MIDI_ERR_ALREADY_INITIALIZED",  "Already initialized" },
    { MIDI_ERR_NOT_FOUND,            "MIDI_ERR_NOT_FOUND",            "Resource not found" },
    { MIDI_ERR_BUFFER_OVERFLOW,      "MIDI_ERR_BUFFER_OVERFLOW",      "Buffer overflow" },
    { MIDI_ERR_BUFFER_EMPTY,         "MIDI_ERR_BUFFER_EMPTY",         "Buffer empty" },
    { MIDI_ERR_BUSY,                 "MIDI_ERR_BUSY",                 "Resource busy" },
    { MIDI_ERR_NOT_SUPPORTED,        "MIDI_ERR_NOT_SUPPORTED",        "Not supported" },
    
    /* Network */
    { MIDI_ERR_NET_BASE,             "MIDI_ERR_NET_BASE",             "Network error" },
    { MIDI_ERR_NET_INIT_FAILED,      "MIDI_ERR_NET_INIT_FAILED",      "Network init failed" },
    { MIDI_ERR_NET_NOT_CONNECTED,    "MIDI_ERR_NET_NOT_CONNECTED",    "Network not connected" },
    { MIDI_ERR_NET_CONNECTION_LOST,  "MIDI_ERR_NET_CONNECTION_LOST",  "Connection lost" },
    { MIDI_ERR_NET_SOCKET_ERROR,     "MIDI_ERR_NET_SOCKET_ERROR",     "Socket error" },
    { MIDI_ERR_NET_BIND_FAILED,      "MIDI_ERR_NET_BIND_FAILED",      "Bind failed" },
    { MIDI_ERR_NET_SEND_FAILED,      "MIDI_ERR_NET_SEND_FAILED",      "Send failed" },
    { MIDI_ERR_NET_RECV_FAILED,      "MIDI_ERR_NET_RECV_FAILED",      "Receive failed" },
    { MIDI_ERR_NET_DNS_FAILED,       "MIDI_ERR_NET_DNS_FAILED",       "DNS resolution failed" },
    { MIDI_ERR_NET_MDNS_FAILED,      "MIDI_ERR_NET_MDNS_FAILED",      "mDNS operation failed" },
    
    /* USB */
    { MIDI_ERR_USB_BASE,             "MIDI_ERR_USB_BASE",             "USB error" },
    { MIDI_ERR_USB_INIT_FAILED,      "MIDI_ERR_USB_INIT_FAILED",      "USB init failed" },
    { MIDI_ERR_USB_NOT_CONNECTED,    "MIDI_ERR_USB_NOT_CONNECTED",    "USB not connected" },
    { MIDI_ERR_USB_DEVICE_ERROR,     "MIDI_ERR_USB_DEVICE_ERROR",     "USB device error" },
    { MIDI_ERR_USB_TRANSFER_FAILED,  "MIDI_ERR_USB_TRANSFER_FAILED",  "USB transfer failed" },
    { MIDI_ERR_USB_ENUM_FAILED,      "MIDI_ERR_USB_ENUM_FAILED",      "USB enumeration failed" },
    { MIDI_ERR_USB_NO_DRIVER,        "MIDI_ERR_USB_NO_DRIVER",        "No USB driver" },
    { MIDI_ERR_USB_TIMEOUT,          "MIDI_ERR_USB_TIMEOUT",          "USB timeout" },
    { MIDI_ERR_USB_DISCONNECTED,     "MIDI_ERR_USB_DISCONNECTED",     "USB disconnected" },
    { MIDI_ERR_USB_MAX_DEVICES,      "MIDI_ERR_USB_MAX_DEVICES",      "Max USB devices reached" },
    
    /* MIDI Protocol */
    { MIDI_ERR_MIDI_BASE,            "MIDI_ERR_MIDI_BASE",            "MIDI protocol error" },
    { MIDI_ERR_MIDI_INVALID_MSG,     "MIDI_ERR_MIDI_INVALID_MSG",     "Invalid MIDI message" },
    { MIDI_ERR_MIDI_PARSE_FAILED,    "MIDI_ERR_MIDI_PARSE_FAILED",    "MIDI parse failed" },
    { MIDI_ERR_MIDI_CONVERT_FAILED,  "MIDI_ERR_MIDI_CONVERT_FAILED",  "MIDI conversion failed" },
    { MIDI_ERR_MIDI_UNKNOWN_MSG,     "MIDI_ERR_MIDI_UNKNOWN_MSG",     "Unknown MIDI message" },
    { MIDI_ERR_MIDI_UMP_INVALID,     "MIDI_ERR_MIDI_UMP_INVALID",     "Invalid UMP packet" },
    
    /* Configuration */
    { MIDI_ERR_CONFIG_BASE,          "MIDI_ERR_CONFIG_BASE",          "Configuration error" },
    { MIDI_ERR_CONFIG_INVALID,       "MIDI_ERR_CONFIG_INVALID",       "Invalid configuration" },
    { MIDI_ERR_CONFIG_NOT_FOUND,     "MIDI_ERR_CONFIG_NOT_FOUND",     "Config not found" },
    { MIDI_ERR_CONFIG_SAVE_FAILED,   "MIDI_ERR_CONFIG_SAVE_FAILED",   "Config save failed" },
    { MIDI_ERR_CONFIG_LOAD_FAILED,   "MIDI_ERR_CONFIG_LOAD_FAILED",   "Config load failed" },
    { MIDI_ERR_CONFIG_NVS_ERROR,     "MIDI_ERR_CONFIG_NVS_ERROR",     "NVS storage error" },
    
    /* Session */
    { MIDI_ERR_SESSION_BASE,         "MIDI_ERR_SESSION_BASE",         "Session error" },
    { MIDI_ERR_SESSION_NOT_ACTIVE,   "MIDI_ERR_SESSION_NOT_ACTIVE",   "No active session" },
    { MIDI_ERR_SESSION_ALREADY_ACTIVE, "MIDI_ERR_SESSION_ALREADY_ACTIVE", "Session already active" },
    { MIDI_ERR_SESSION_INIT_FAILED,  "MIDI_ERR_SESSION_INIT_FAILED",  "Session init failed" },
    { MIDI_ERR_SESSION_REJECTED,     "MIDI_ERR_SESSION_REJECTED",     "Session rejected" },
    { MIDI_ERR_SESSION_TIMEOUT,      "MIDI_ERR_SESSION_TIMEOUT",      "Session timeout" },
    { MIDI_ERR_SESSION_CLOSED,       "MIDI_ERR_SESSION_CLOSED",       "Session closed" },
};

#define ERROR_TABLE_SIZE (sizeof(error_table) / sizeof(error_table[0]))

const char* midi_error_str(midi_error_t err) {
    for (size_t i = 0; i < ERROR_TABLE_SIZE; i++) {
        if (error_table[i].code == err) {
            return error_table[i].desc;
        }
    }
    return "Unknown error code";
}

const char* midi_error_name(midi_error_t err) {
    for (size_t i = 0; i < ERROR_TABLE_SIZE; i++) {
        if (error_table[i].code == err) {
            return error_table[i].name;
        }
    }
    return "MIDI_ERR_UNKNOWN";
}

const char* midi_error_category(midi_error_t err) {
    uint16_t category = (err >> 8) & 0xFF;
    
    switch (category) {
        case 0x00: return "General";
        case 0x01: return "System";
        case 0x02: return "Network";
        case 0x03: return "USB";
        case 0x04: return "MIDI";
        case 0x05: return "Config";
        case 0x06: return "Session";
        default:   return "Unknown";
    }
}