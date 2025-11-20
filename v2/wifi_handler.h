/* ==============================================================================
   WIFI_HANDLER.H - WiFi Management Interface
   
   Provides WiFi connection and management functions:
   - WiFi initialization and connection
   - Automatic reconnection with exponential backoff
   - Connection status monitoring
   - Network configuration (DHCP/Static IP)
   
   WiFi runs as a state machine that automatically handles disconnections.
   ============================================================================== */

/* Header guard to prevent multiple inclusion of wifi_handler.h */
#ifndef WIFI_HANDLER_H
#define WIFI_HANDLER_H

#include <Arduino.h>

void setupWiFi();

void startWiFiConnection();

void checkWiFiConnection();

void loadWiFiCredentials();

void saveWiFi(const String& ssid, const String& pass);

void loadNetworkConfig();

void saveNetworkConfig();

#endif