#ifndef WEB_HANDLER_H
#define WEB_HANDLER_H

#include <Arduino.h>

// ============================================================================
// WEB SERVER FUNCTIONS
// ============================================================================

/**
 * @brief Register all web routes
 */
void registerRoutes();

/**
 * @brief Send main HTML page
 */
void sendIndex();

/**
 * @brief API: Get system status
 */
void handleApiStatus();

/**
 * @brief API: Start business logic
 */
void handleApiBizStart();

/**
 * @brief API: Stop business logic
 */
void handleApiBizStop();

/**
 * @brief API: Execute command
 */
void handleApiExec();

/**
 * @brief API: Configure network
 */
void handleApiNetwork();

#if DEBUG_MODE
/**
 * @brief API: Get task monitoring data
 */
void handleApiTasks();

/**
 * @brief API: Get debug logs
 */
void handleApiDebugLogs();

/**
 * @brief API: Clear debug logs
 */
void handleApiDebugClear();
#endif

#if ENABLE_OTA
/**
 * @brief Check if OTA is active
 */
bool isOtaActive();

/**
 * @brief Send 503 busy JSON response
 */
void sendBusyJson(const char* msg = "OTA in progress");
#endif

#endif // WEB_HANDLER_H