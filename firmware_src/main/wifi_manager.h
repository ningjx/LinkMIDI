#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file wifi_manager.h
 * @brief WiFi Connection Management Module
 * 
 * Handles WiFi initialization with NVS storage and configuration from Kconfig.
 */

/**
 * @brief Initialize WiFi with NVS storage
 * @return true on success, false on failure
 */
bool wifi_manager_init(void);

/**
 * @brief Connect to WiFi using stored or configured credentials
 * @return true if connection succeeded, false otherwise
 */
bool wifi_manager_connect(void);

/**
 * @brief Wait for WiFi connection to be established
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return true if connected, false if timeout
 */
bool wifi_manager_wait_for_connection(uint32_t timeout_ms);

/**
 * @brief Deinitialize WiFi subsystem
 */
void wifi_manager_deinit(void);

/**
 * @brief Get WiFi connection status
 * @return true if connected, false otherwise
 */
bool wifi_manager_is_connected(void);

#ifdef __cplusplus
}
#endif
