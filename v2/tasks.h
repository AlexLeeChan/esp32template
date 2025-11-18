#ifndef TASKS_H
#define TASKS_H

#include <Arduino.h>
#include "types.h"

// ============================================================================
// RTOS TASK FUNCTIONS
// ============================================================================

/**
 * @brief System management task (WiFi, NTP, BLE, monitoring)
 */
void systemTask(void* param);

/**
 * @brief Web server task (HTTP handling)
 */
void webTask(void* param);

/**
 * @brief Business logic task (command processing)
 */
void bizTask(void* param);

/**
 * @brief Initialize message pool and mutexes
 */
void initMessagePool();

/**
 * @brief Allocate a message from the pool
 */
ExecMessage* allocMessage();

/**
 * @brief Free a message back to the pool
 */
void freeMessage(ExecMessage* msg);

/**
 * @brief Update CPU load statistics (always enabled)
 */
void updateCpuLoad();

#endif // TASKS_H