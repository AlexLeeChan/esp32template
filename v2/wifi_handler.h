#ifndef WIFI_HANDLER_H
#define WIFI_HANDLER_H

#include <Arduino.h>

// ============================================================================
// WIFI FUNCTIONS
// ============================================================================

/**
 * @brief Setup WiFi (configure mode and event handlers)
 */
void setupWiFi();

/**
 * @brief Start WiFi connection attempt
 */
void startWiFiConnection();

/**
 * @brief Check and manage WiFi connection state
 */
void checkWiFiConnection();

/**
 * @brief Load WiFi credentials from NVS to RAM
 */
void loadWiFiCredentials();

/**
 * @brief Save WiFi credentials to NVS and RAM
 */
void saveWiFi(const String& ssid, const String& pass);

/**
 * @brief Load network configuration from NVS
 */
void loadNetworkConfig();

/**
 * @brief Save network configuration to NVS
 */
void saveNetworkConfig();

#endif // WIFI_HANDLER_H