#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include "config.h"

#if ENABLE_OTA

#include <Arduino.h>

// ============================================================================
// OTA FUNCTIONS
// ============================================================================

/**
 * @brief Register OTA-related web routes
 */
void registerOtaRoutes();

/**
 * @brief API: Get OTA status
 */
void handleOTAStatus();

/**
 * @brief API: Start OTA update
 */
void handleOTAUpdate();

/**
 * @brief API: Reset OTA state
 */
void handleOTAReset();

/**
 * @brief API: Get partition information
 */
void handleOTAInfo();

/**
 * @brief Delete non-essential tasks for OTA
 */
void deleteNonEssentialTasks();

/**
 * @brief Delete web task for OTA
 */
void deleteWebTask();

/**
 * @brief Recreate tasks after OTA failure
 */
void recreateTasks();

/**
 * @brief Dump partition table information
 */
void dumpPartitionInfo(String& logOutput);

#endif // ENABLE_OTA

#endif // OTA_HANDLER_H