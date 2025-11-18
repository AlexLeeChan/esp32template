#ifndef DEBUG_HANDLER_H
#define DEBUG_HANDLER_H

#include "config.h"

#if DEBUG_MODE

#include <Arduino.h>

// ============================================================================
// DEBUG FUNCTIONS
// ============================================================================

/**
 * @brief Add error log entry
 */
void addErrorLog(const String& msg, uint32_t uptimeSec);

/**
 * @brief Add reboot log entry
 */
void addRebootLog(const String& msg, uint32_t uptimeSec);

/**
 * @brief Add WiFi log entry
 */
void addWifiLog(const String& msg, uint32_t uptimeSec);

/**
 * @brief Update task monitoring statistics
 */
void updateTaskMonitoring();

/**
 * @brief Update CPU load from detailed task data (used by cpu_monitor when DEBUG_MODE is on)
 */
void updateCpuLoadFromTaskData();

/**
 * @brief Check task stack health
 */
void checkTaskStacks();

/**
 * @brief Load debug logs from NVS
 */
void loadDebugLogs();

/**
 * @brief Clear all debug logs
 */
void clearDebugLogs();

/**
 * @brief Format reset reason
 */
String formatResetReason(esp_reset_reason_t reason);

/**
 * @brief Flash write background task
 */
void flashWriteTask(void* param);

// Logging macros
#define LOG_ERROR(msg, uptime) addErrorLog(msg, uptime)
#define LOG_WIFI(msg, uptime) addWifiLog(msg, uptime)
#define LOG_REBOOT(msg, uptime) addRebootLog(msg, uptime)

#else

// Empty macros when DEBUG_MODE is off
#define LOG_ERROR(msg, uptime) (void)0
#define LOG_WIFI(msg, uptime) (void)0
#define LOG_REBOOT(msg, uptime) (void)0

#endif // DEBUG_MODE

#endif // DEBUG_HANDLER_H