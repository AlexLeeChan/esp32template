#include "cpu_monitor.h"
#include "globals.h"
#include "config.h"

#if DEBUG_MODE
  #include "debug_handler.h"
#endif

// Timing control
static uint32_t lastCpuUpdate = 0;

// Lightweight CPU tracking (used when DEBUG_MODE=0)
#if !DEBUG_MODE
static uint32_t prevIdleRuntime[2] = {0, 0};
static uint32_t prevTotalRuntime[2] = {0, 0};
static bool cpuInitialized = false;
#endif

void updateCpuLoad() {
  uint32_t now = millis();
  
  // Rate limiting: Update every 500ms
  if (now - lastCpuUpdate < 500) return;
  lastCpuUpdate = now;

#if DEBUG_MODE
  // ============================================================
  // DEBUG_MODE: Use full task monitoring system
  // ============================================================
  // The detailed task monitoring in debug_handler.cpp tracks
  // all tasks and calculates per-task CPU%. We just extract
  // the IDLE task CPU% to derive core load.
  // ============================================================
  
  updateTaskMonitoring();  // Updates taskData[] with all task info
  
  // Extract IDLE task CPU% and convert to core load
  for (uint8_t i = 0; i < taskCount; i++) {
    const char* taskName = taskData[i].name.c_str();
    
    // Core 0 IDLE task
    if (strcmp(taskName, "IDLE") == 0 || strcmp(taskName, "IDLE0") == 0) {
      uint8_t idlePercent = taskData[i].cpuPercent;
      coreLoadPct[0] = (idlePercent > 100) ? 0 : (100 - idlePercent);
    }
    
    #if NUM_CORES > 1
    // Core 1 IDLE task
    if (strcmp(taskName, "IDLE1") == 0) {
      uint8_t idlePercent = taskData[i].cpuPercent;
      coreLoadPct[1] = (idlePercent > 100) ? 0 : (100 - idlePercent);
    }
    #endif
  }

#else
  // ============================================================
  // PRODUCTION MODE: Lightweight IDLE-only calculation
  // ============================================================
  // Only track IDLE tasks to calculate core load
  // Much lower overhead than full task monitoring
  // ============================================================
  
  TaskStatus_t taskStatus[32];
  UBaseType_t numTasks = uxTaskGetSystemState(taskStatus, 32, NULL);
  
  if (numTasks == 0) return;
  
  // Accumulate runtime per core
  uint32_t idleRuntime[2] = {0, 0};
  uint32_t totalRuntime[2] = {0, 0};
  
  for (UBaseType_t i = 0; i < numTasks; i++) {
    const char* taskName = taskStatus[i].pcTaskName;
    uint32_t runtime = taskStatus[i].ulRunTimeCounter;  // 100μs ticks
    
    // Determine core affinity
    #if NUM_CORES > 1
      BaseType_t affinity = xTaskGetAffinity(taskStatus[i].xHandle);
    #else
      BaseType_t affinity = 0;
    #endif
    
    // Core 0 tasks
    if (affinity == 0) {
      totalRuntime[0] += runtime;
      if (strcmp(taskName, "IDLE") == 0 || strcmp(taskName, "IDLE0") == 0) {
        idleRuntime[0] = runtime;
      }
    }
    
    #if NUM_CORES > 1
    // Core 1 tasks
    else if (affinity == 1) {
      totalRuntime[1] += runtime;
      if (strcmp(taskName, "IDLE1") == 0) {
        idleRuntime[1] = runtime;
      }
    }
    #endif
  }
  
  // First run: Initialize baseline
  if (!cpuInitialized) {
    for (int core = 0; core < NUM_CORES; core++) {
      prevIdleRuntime[core] = idleRuntime[core];
      prevTotalRuntime[core] = totalRuntime[core];
    }
    cpuInitialized = true;
    return;
  }
  
  // Calculate CPU load from delta values
  for (int core = 0; core < NUM_CORES; core++) {
    // Calculate how much time passed in this interval
    uint32_t idleDelta = idleRuntime[core] - prevIdleRuntime[core];
    uint32_t totalDelta = totalRuntime[core] - prevTotalRuntime[core];
    
    if (totalDelta > 0) {
      // IDLE% = (idle time / total time) * 100
      uint32_t idlePercent = (idleDelta * 100ULL) / totalDelta;
      if (idlePercent > 100) idlePercent = 100;
      
      // CPU Load = 100% - IDLE%
      coreLoadPct[core] = 100 - idlePercent;
    } else {
      coreLoadPct[core] = 0;
    }
    
    // Save current values for next delta
    prevIdleRuntime[core] = idleRuntime[core];
    prevTotalRuntime[core] = totalRuntime[core];
  }
  
#endif // DEBUG_MODE
}