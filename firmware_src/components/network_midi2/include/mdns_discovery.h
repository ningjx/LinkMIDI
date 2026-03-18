#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file mdns_discovery.h
 * @brief mDNS Discovery Module for Network MIDI 2.0
 * 
 * This module handles the mDNS (Multicast DNS) discovery for network MIDI 2.0 devices.
 * It provides functions to:
 * - Announce the device on the local network
 * - Query for other MIDI 2.0 devices
 * - Manage discovered devices list
 */

/**
 * @brief mDNS discovery context (opaque)
 */
typedef struct mdns_discovery_context mdns_discovery_context_t;

/**
 * @brief Initialize mDNS discovery module
 * @param device_name Name of this device
 * @param product_id Product identifier
 * @param port UDP port for MIDI connections
 * @return Context handle, or NULL on error
 */
mdns_discovery_context_t* mdns_discovery_init(
    const char* device_name,
    const char* product_id,
    uint16_t port);

/**
 * @brief Cleanup and free mDNS discovery context
 * @param ctx Context handle
 */
void mdns_discovery_deinit(mdns_discovery_context_t* ctx);

/**
 * @brief Start mDNS discovery service
 * @param ctx Context handle
 * @return true on success
 */
bool mdns_discovery_start(mdns_discovery_context_t* ctx);

/**
 * @brief Stop mDNS discovery service
 * @param ctx Context handle
 */
void mdns_discovery_stop(mdns_discovery_context_t* ctx);

/**
 * @brief Send mDNS discovery query to find MIDI 2.0 devices
 * @param ctx Context handle
 * @return true on success
 */
bool mdns_discovery_send_query(mdns_discovery_context_t* ctx);

/**
 * @brief Get discovered device information
 * @param ctx Context handle
 * @param index Device index (0-based)
 * @param device_name Output buffer for device name (at least 64 bytes)
 * @param ip_address Output buffer for IP address
 * @param port Output buffer for UDP port
 * @return true if device exists, false otherwise
 */
bool mdns_discovery_get_device(
    mdns_discovery_context_t* ctx,
    int index,
    char* device_name,
    uint32_t* ip_address,
    uint16_t* port);

/**
 * @brief Get number of discovered devices
 * @param ctx Context handle
 * @return Number of discovered devices
 */
int mdns_discovery_get_device_count(mdns_discovery_context_t* ctx);

/**
 * @brief Clear the discovered devices list
 * @param ctx Context handle
 */
void mdns_discovery_clear_devices(mdns_discovery_context_t* ctx);

#ifdef __cplusplus
}
#endif
