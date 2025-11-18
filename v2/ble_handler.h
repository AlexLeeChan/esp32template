#ifndef BLE_HANDLER_H
#define BLE_HANDLER_H

#include <Arduino.h>

// ============================================================================
// BLE FUNCTIONS
// ============================================================================

/**
 * @brief Initialize BLE server
 */
void initBLE();

/**
 * @brief Handle BLE reconnection
 */
void handleBLEReconnect();

/**
 * @brief Send data over BLE
 */
void sendBLE(const String& msg);

/**
 * @brief Handle BLE command
 */
void handleBLECommand(String cmd);

#endif // BLE_HANDLER_H