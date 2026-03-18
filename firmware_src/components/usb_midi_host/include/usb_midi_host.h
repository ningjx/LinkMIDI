#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file usb_midi_host.h
 * @brief USB MIDI Host Driver for ESP32
 * 
 * This module implements a USB host driver for MIDI keyboards and controllers.
 * It provides functions to:
 * - Initialize USB host stack
 * - Enumerate MIDI devices
 * - Receive MIDI data from connected devices
 * - Manage device connections/disconnections
 */

/**
 * @brief MIDI message type enumeration
 */
typedef enum {
    MIDI_MSG_NOTE_OFF = 0x80,       ///< Note Off (0x80 - 0x8F)
    MIDI_MSG_NOTE_ON = 0x90,        ///< Note On (0x90 - 0x9F)
    MIDI_MSG_POLY_PRESSURE = 0xA0,  ///< Polyphonic Key Pressure (0xA0 - 0xAF)
    MIDI_MSG_CONTROL_CHANGE = 0xB0, ///< Control Change (0xB0 - 0xBF)
    MIDI_MSG_PROGRAM_CHANGE = 0xC0, ///< Program Change (0xC0 - 0xCF)
    MIDI_MSG_CHANNEL_PRESSURE = 0xD0, ///< Channel Pressure (0xD0 - 0xDF)
    MIDI_MSG_PITCH_BEND = 0xE0,     ///< Pitch Bend (0xE0 - 0xEF)
    MIDI_MSG_SYSTEM = 0xF0,         ///< System Message (0xF0 - 0xFF)
} usb_midi_message_type_t;

/**
 * @brief USB MIDI device information
 */
typedef struct {
    uint16_t vendor_id;             ///< USB Vendor ID
    uint16_t product_id;            ///< USB Product ID
    char manufacturer[128];          ///< Manufacturer name
    char product_name[128];          ///< Product name
    uint8_t device_address;          ///< USB device address
    uint8_t midi_in_endpoint;        ///< MIDI In endpoint address
    uint16_t max_packet_size;        ///< Maximum packet size
    bool is_connected;               ///< Connection status
} usb_midi_device_t;

/**
 * @brief MIDI message callback type
 * 
 * Called when MIDI data is received from a connected device.
 * @param device_index Index of the device that sent the data
 * @param data Raw MIDI data (typically 3 bytes for MIDI 1.0)
 * @param length Data length in bytes
 */
typedef void (*usb_midi_rx_callback_t)(uint8_t device_index, 
                                        const uint8_t* data, 
                                        uint16_t length);

/**
 * @brief Device connection callback type
 * 
 * Called when a MIDI device is connected
 * @param device_index Index of the newly connected device
 * @param device_info Device information
 */
typedef void (*usb_midi_device_connected_callback_t)(uint8_t device_index,
                                                     const usb_midi_device_t* device_info);

/**
 * @brief Device disconnection callback type
 * 
 * Called when a MIDI device is disconnected
 * @param device_index Index of the disconnected device
 */
typedef void (*usb_midi_device_disconnected_callback_t)(uint8_t device_index);

/**
 * @brief Configuration structure for USB MIDI host
 */
typedef struct {
    usb_midi_rx_callback_t midi_rx_callback;
    usb_midi_device_connected_callback_t device_connected_callback;
    usb_midi_device_disconnected_callback_t device_disconnected_callback;
} usb_midi_host_config_t;

/**
 * @brief USB MIDI host context (opaque)
 */
typedef struct usb_midi_host_context usb_midi_host_context_t;

/**
 * @brief Initialize USB MIDI host driver
 * 
 * Sets up the USB host stack and MIDI processing infrastructure.
 * Must be called before any other USB MIDI functions.
 * 
 * @param config Configuration structure with callbacks
 * @return Context handle, or NULL on error
 */
usb_midi_host_context_t* usb_midi_host_init(const usb_midi_host_config_t* config);

/**
 * @brief Cleanup and free USB MIDI host context
 * 
 * Stops all processing and releases resources.
 * @param ctx Context handle
 */
void usb_midi_host_deinit(usb_midi_host_context_t* ctx);

/**
 * @brief Start the USB MIDI host service
 * 
 * Begins monitoring for device connections and MIDI data.
 * @param ctx Context handle
 * @return true on success
 */
bool usb_midi_host_start(usb_midi_host_context_t* ctx);

/**
 * @brief Stop the USB MIDI host service
 * 
 * Stops monitoring and closes all connections.
 * @param ctx Context handle
 */
void usb_midi_host_stop(usb_midi_host_context_t* ctx);

/**
 * @brief Get the number of connected MIDI devices
 * 
 * @param ctx Context handle
 * @return Number of connected devices (0-255)
 */
uint8_t usb_midi_host_get_device_count(usb_midi_host_context_t* ctx);

/**
 * @brief Get information about a connected MIDI device
 * 
 * @param ctx Context handle
 * @param device_index Index of the device (0-based)
 * @param device_info Output buffer for device information
 * @return true if device exists, false otherwise
 */
bool usb_midi_host_get_device_info(usb_midi_host_context_t* ctx,
                                   uint8_t device_index,
                                   usb_midi_device_t* device_info);

/**
 * @brief Check if a device is currently connected
 * 
 * @param ctx Context handle
 * @param device_index Index of the device
 * @return true if device is connected
 */
bool usb_midi_host_is_device_connected(usb_midi_host_context_t* ctx,
                                       uint8_t device_index);

/**
 * @brief Get USB host stack status
 * 
 * @param ctx Context handle
 * @return true if USB host is running
 */
bool usb_midi_host_is_running(usb_midi_host_context_t* ctx);

/**
 * @brief Get the number of MIDI IN endpoints
 * 
 * This is primarily for diagnostics and testing.
 * @param ctx Context handle
 * @param device_index Device index
 * @return Number of MIDI IN endpoints, or -1 if device not found
 */
int usb_midi_host_get_endpoint_count(usb_midi_host_context_t* ctx,
                                     uint8_t device_index);

#ifdef __cplusplus
}
#endif
