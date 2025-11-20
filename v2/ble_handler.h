/* ==============================================================================
   BLE_HANDLER.H - Bluetooth Low Energy Interface
   
   Provides BLE functionality for:
   - Wireless configuration (WiFi credentials)
   - Remote command execution
   - Status LED control
   - Device advertising
   
   BLE allows configuration without WiFi connection.
   ============================================================================== */

/* Header guard to prevent multiple inclusion of ble_handler.h */
#ifndef BLE_HANDLER_H
#define BLE_HANDLER_H

#include <Arduino.h>

void initBLE();

void handleBLEReconnect();

void sendBLE(const String& msg);

void handleBLECommand(String cmd);

#endif