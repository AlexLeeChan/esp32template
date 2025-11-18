#ifndef CPU_MONITOR_H
#define CPU_MONITOR_H

#include <Arduino.h>

/**
 * @brief Update CPU load percentage for all cores
 * 
 * This function works in both DEBUG_MODE and production:
 * - DEBUG_MODE=1: Uses full task monitoring from debug_handler
 * - DEBUG_MODE=0: Lightweight IDLE-task-only calculation
 * 
 * Call frequency: Every 500ms from systemTask
 * Updates: coreLoadPct[0] and coreLoadPct[1]
 */
void updateCpuLoad();

#endif // CPU_MONITOR_H