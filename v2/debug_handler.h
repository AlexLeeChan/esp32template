/* ==============================================================================
   DEBUG_HANDLER.H - Debug and Logging Interface
   
   Provides error logging and task monitoring (when DEBUG_MODE enabled):
   - Error, WiFi, and reboot event logging
   - Log persistence to NVS flash storage
   - Task stack monitoring
   - FreeRTOS runtime statistics
   
   Logs are circular buffers that persist across reboots for debugging.
   ============================================================================== */

/* Header guard to prevent multiple inclusion of debug_handler.h */
#ifndef DEBUG_HANDLER_H
#define DEBUG_HANDLER_H

#include "config.h"

#if DEBUG_MODE

#include <Arduino.h>

void addErrorLog(const String& msg, uint32_t uptimeSec);

void addRebootLog(const String& msg, uint32_t uptimeSec);

void addWifiLog(const String& msg, uint32_t uptimeSec);

void updateTaskMonitoring();

void checkTaskStacks();

void loadDebugLogs();

void clearDebugLogs();

String formatResetReason(esp_reset_reason_t reason);

void flashWriteTask(void* param);

#define LOG_ERROR(msg, uptime) addErrorLog(msg, uptime)
#define LOG_WIFI(msg, uptime) addWifiLog(msg, uptime)
#define LOG_REBOOT(msg, uptime) addRebootLog(msg, uptime)

#else

#define LOG_ERROR(msg, uptime) (void)0
#define LOG_WIFI(msg, uptime) (void)0
#define LOG_REBOOT(msg, uptime) (void)0

#endif

#endif