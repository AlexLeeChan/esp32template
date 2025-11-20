/* ==============================================================================
   NETWORK_UTILS.H - Network Utility Functions Interface
   
   Provides network-related helper functions:
   - IP address formatting and validation
   - Network configuration utilities
   - Connection quality metrics
   
   Common network operations used across multiple modules.
   ============================================================================== */

/* Header guard to prevent multiple inclusion of network_utils.h */
#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include <Arduino.h>
#include <IPAddress.h>

String cleanString(const String& input);

bool isValidIP(const String& s);

bool isValidSubnet(const String& s);

IPAddress parseIP(const String& s);

#endif
