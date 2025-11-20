/* ==============================================================================
   CPU_MONITOR.CPP - CPU Usage Monitoring Implementation
   
   Implements CPU monitoring with two modes:
   
   DEBUG_MODE: Detailed per-task CPU usage tracking using FreeRTOS runtime
              statistics, identifies which tasks consume most CPU time
   
   Non-DEBUG: Lightweight idle task monitoring for basic CPU load indication
   
   Used for performance optimization and system health monitoring.
   ============================================================================== */

#include "cpu_monitor.h"
#include "globals.h"
#include "config.h"

#if DEBUG_MODE
  #include "debug_handler.h"
#endif

static uint32_t lastCpuUpdate = 0;

#if !DEBUG_MODE
static uint32_t prevIdleRuntime[2] = {0, 0};
static uint32_t prevTotalRuntime[2] = {0, 0};
static bool cpuInitialized = false;
#endif

void updateCpuLoad() {
  uint32_t now = millis();
  

  if (now - lastCpuUpdate < 500) return;
  lastCpuUpdate = now;

#if DEBUG_MODE

  
  updateTaskMonitoring();
  

  for (uint8_t i = 0; i < taskCount; i++) {
    const char* taskName = taskData[i].name.c_str();
    

    if (strcmp(taskName, "IDLE") == 0 || strcmp(taskName, "IDLE0") == 0) {
      uint8_t idlePercent = taskData[i].cpuPercent;
      coreLoadPct[0] = (idlePercent > 100) ? 0 : (100 - idlePercent);
    }
    
    #if NUM_CORES > 1

    if (strcmp(taskName, "IDLE1") == 0) {
      uint8_t idlePercent = taskData[i].cpuPercent;
      coreLoadPct[1] = (idlePercent > 100) ? 0 : (100 - idlePercent);
    }
    #endif
  }

#else

  
  TaskStatus_t taskStatus[32];
  UBaseType_t numTasks = uxTaskGetSystemState(taskStatus, 32, NULL);
  
  if (numTasks == 0) return;
  

  uint32_t idleRuntime[2] = {0, 0};
  uint32_t totalRuntime[2] = {0, 0};
  
  for (UBaseType_t i = 0; i < numTasks; i++) {
    const char* taskName = taskStatus[i].pcTaskName;
    uint32_t runtime = taskStatus[i].ulRunTimeCounter;
    

    #if NUM_CORES > 1
      BaseType_t affinity = xTaskGetAffinity(taskStatus[i].xHandle);
    #else
      BaseType_t affinity = 0;
    #endif
    

    if (affinity == 0) {
      totalRuntime[0] += runtime;
      if (strcmp(taskName, "IDLE") == 0 || strcmp(taskName, "IDLE0") == 0) {
        idleRuntime[0] = runtime;
      }
    }
    
    #if NUM_CORES > 1

    else if (affinity == 1) {
      totalRuntime[1] += runtime;
      if (strcmp(taskName, "IDLE1") == 0) {
        idleRuntime[1] = runtime;
      }
    }
    #endif
  }
  

  if (!cpuInitialized) {
    for (int core = 0; core < NUM_CORES; core++) {
      prevIdleRuntime[core] = idleRuntime[core];
      prevTotalRuntime[core] = totalRuntime[core];
    }
    cpuInitialized = true;
    return;
  }
  

  for (int core = 0; core < NUM_CORES; core++) {

    uint32_t idleDelta = idleRuntime[core] - prevIdleRuntime[core];
    uint32_t totalDelta = totalRuntime[core] - prevTotalRuntime[core];
    
    if (totalDelta > 0) {

      uint32_t idlePercent = (idleDelta * 100ULL) / totalDelta;
      if (idlePercent > 100) idlePercent = 100;
      

      coreLoadPct[core] = 100 - idlePercent;
    } else {
      coreLoadPct[core] = 0;
    }
    

    prevIdleRuntime[core] = idleRuntime[core];
    prevTotalRuntime[core] = totalRuntime[core];
  }
  
#endif
}
