/* ==============================================================================
   CPU_MONITOR.H - CPU Usage Monitoring Interface
   
   Provides CPU usage tracking:
   - Per-task CPU utilization (DEBUG_MODE)
   - Lightweight overall CPU tracking (non-DEBUG_MODE)
   - Runtime statistics collection
   
   Helps identify performance bottlenecks and task scheduling issues.
   ============================================================================== */

/* Header guard to prevent multiple inclusion of cpu_monitor.h */
#ifndef CPU_MONITOR_H
#define CPU_MONITOR_H

#include <Arduino.h>

void updateCpuLoad();

#endif
