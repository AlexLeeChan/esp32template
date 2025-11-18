#ifndef TIME_HANDLER_H
#define TIME_HANDLER_H

#include <Arduino.h>

// ============================================================================
// TIME FUNCTIONS
// ============================================================================

/**
 * @brief Initialize NTP client
 */
void initNTP();

/**
 * @brief Synchronize time with NTP servers
 */
void syncNTP();

/**
 * @brief Check if time is synchronized
 */
bool getTimeInitialized();

/**
 * @brief Get current time as string
 */
String getCurrentTimeString();

/**
 * @brief Get current epoch time
 */
uint32_t getEpochTime();

/**
 * @brief Check if NTP sync is needed
 */
bool shouldSyncNTP();

#endif // TIME_HANDLER_H