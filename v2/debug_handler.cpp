/* ==============================================================================
   DEBUG_HANDLER.CPP - Debug and Logging Implementation
   
   Implements comprehensive logging system:
   - Maintains circular log buffers (errors, WiFi events, reboots)
   - Persists logs to NVS flash via background task
   - Monitors task stack usage to detect potential overflows
   - Tracks CPU usage per task using FreeRTOS statistics
   
   Background flash writes prevent blocking main tasks. Logs are accessible
   via web API for remote debugging.
   ============================================================================== */

#include "debug_handler.h"

#if DEBUG_MODE

#include "globals.h"
#include "time_handler.h"
#include <esp_system.h>

static void addLogEntry(LogEntry* logs, uint8_t& count, const String& msg, uint32_t uptimeSec);
static void queueFlashWrite(FlashWriteType type);
static void saveRebootLogsToFlash();
static void saveWifiLogsToFlash();
static void saveErrorLogsToFlash();
static String getTaskStateName(eTaskState s);
static String getStackHealth(uint32_t hwm);
static String getAffinityString(BaseType_t affinity);
static inline BaseType_t getSafeAffinity(TaskHandle_t handle);

static bool isLikelyWraparound(uint32_t prevRuntime, uint32_t currentRuntime);

static bool isLikelyWraparound(uint32_t prevRuntime, uint32_t currentRuntime) {

  const uint32_t nearMax = 0xF0000000;
  const uint32_t nearZero = 0x10000000;
  return (prevRuntime > nearMax) && (currentRuntime < nearZero);
}

static void addLogEntry(LogEntry* logs, uint8_t& count, const String& msg, uint32_t uptimeSec) {
  String trimmed = msg;
  if (trimmed.length() > 59) trimmed = trimmed.substring(0, 59);

  uint32_t epochTime = 0;
  if (getTimeInitialized()) {
    epochTime = getEpochTime();
  }

  if (count < MAX_DEBUG_LOGS) {
    logs[count].uptime = uptimeSec;
    logs[count].epoch = epochTime;
    trimmed.toCharArray(logs[count].msg, sizeof(logs[count].msg));
    count++;
  } else {
    memmove(&logs[0], &logs[1], (MAX_DEBUG_LOGS - 1) * sizeof(LogEntry));
    logs[MAX_DEBUG_LOGS - 1].uptime = uptimeSec;
    logs[MAX_DEBUG_LOGS - 1].epoch = epochTime;
    trimmed.toCharArray(logs[MAX_DEBUG_LOGS - 1].msg, sizeof(logs[MAX_DEBUG_LOGS - 1].msg));
  }
}

void addRebootLog(const String& msg, uint32_t uptimeSec) {
  addLogEntry(rebootLogs, rebootLogCount, msg, uptimeSec);
  queueFlashWrite(FLASH_WRITE_REBOOT_LOGS);
}

void addWifiLog(const String& msg, uint32_t uptimeSec) {
  addLogEntry(wifiLogs, wifiLogCount, msg, uptimeSec);
  queueFlashWrite(FLASH_WRITE_WIFI_LOGS);
}

void addErrorLog(const String& msg, uint32_t uptimeSec) {
  addLogEntry(errorLogs, errorLogCount, msg, uptimeSec);
  queueFlashWrite(FLASH_WRITE_ERROR_LOGS);
}

static void saveRebootLogsToFlash() {
  /* Acquire flashWriteMutex mutex (wait up to 1000ms) to safely access shared resource */
    if (xSemaphoreTake(flashWriteMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    prefs.putBytes("reboot_logs", rebootLogs, sizeof(rebootLogs));
    prefs.putUChar("reboot_log_count", rebootLogCount);
    xSemaphoreGive(flashWriteMutex);
  }
}

static void saveWifiLogsToFlash() {
  /* Acquire flashWriteMutex mutex (wait up to 1000ms) to safely access shared resource */
    if (xSemaphoreTake(flashWriteMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    prefs.putBytes("wifi_logs", wifiLogs, sizeof(wifiLogs));
    prefs.putUChar("wifi_log_count", wifiLogCount);
    xSemaphoreGive(flashWriteMutex);
  }
}

static void saveErrorLogsToFlash() {
  /* Acquire flashWriteMutex mutex (wait up to 1000ms) to safely access shared resource */
    if (xSemaphoreTake(flashWriteMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    prefs.putBytes("error_logs", errorLogs, sizeof(errorLogs));
    prefs.putUChar("error_log_count", errorLogCount);
    xSemaphoreGive(flashWriteMutex);
  }
}

static void queueFlashWrite(FlashWriteType type) {
  if (flashWriteQueue) {
    FlashWriteRequest req;
    req.type = type;
    req.timestamp = millis();
    xQueueSend(flashWriteQueue, &req, 0);
  }
}

void flashWriteTask(void* param) {
  (void)param;
  FlashWriteRequest req;

  for (;;) {
    if (xQueueReceive(flashWriteQueue, &req, portMAX_DELAY) == pdTRUE) {
      vTaskDelay(pdMS_TO_TICKS(200));

      FlashWriteRequest tempReq;
      bool rebootPending = (req.type == FLASH_WRITE_REBOOT_LOGS);
      bool wifiPending = (req.type == FLASH_WRITE_WIFI_LOGS);
      bool errorPending = (req.type == FLASH_WRITE_ERROR_LOGS);

      while (xQueueReceive(flashWriteQueue, &tempReq, 0) == pdTRUE) {
        if (tempReq.type == FLASH_WRITE_REBOOT_LOGS) rebootPending = true;
        if (tempReq.type == FLASH_WRITE_WIFI_LOGS) wifiPending = true;
        if (tempReq.type == FLASH_WRITE_ERROR_LOGS) errorPending = true;
      }

      if (rebootPending) saveRebootLogsToFlash();
      if (wifiPending) saveWifiLogsToFlash();
      if (errorPending) saveErrorLogsToFlash();
    }
  }
}

void loadDebugLogs() {
  /* Acquire flashWriteMutex mutex (wait up to 1000ms) to safely access shared resource */
    if (xSemaphoreTake(flashWriteMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    rebootLogCount = prefs.getUChar("reboot_log_count", 0);
    if (rebootLogCount > MAX_DEBUG_LOGS) rebootLogCount = MAX_DEBUG_LOGS;
    size_t sz = prefs.getBytesLength("reboot_logs");
    if (sz == sizeof(rebootLogs)) {
      prefs.getBytes("reboot_logs", rebootLogs, sz);
    } else {
      rebootLogCount = 0;
    }

    wifiLogCount = prefs.getUChar("wifi_log_count", 0);
    if (wifiLogCount > MAX_DEBUG_LOGS) wifiLogCount = MAX_DEBUG_LOGS;
    sz = prefs.getBytesLength("wifi_logs");
    if (sz == sizeof(wifiLogs)) {
      prefs.getBytes("wifi_logs", wifiLogs, sz);
    } else {
      wifiLogCount = 0;
    }

    errorLogCount = prefs.getUChar("error_log_count", 0);
    if (errorLogCount > MAX_DEBUG_LOGS) errorLogCount = MAX_DEBUG_LOGS;
    sz = prefs.getBytesLength("error_logs");
    if (sz == sizeof(errorLogs)) {
      prefs.getBytes("error_logs", errorLogs, sz);
    } else {
      errorLogCount = 0;
    }

    xSemaphoreGive(flashWriteMutex);
  }
}

void clearDebugLogs() {
  /* Acquire flashWriteMutex mutex (wait up to 1000ms) to safely access shared resource */
    if (xSemaphoreTake(flashWriteMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    rebootLogCount = wifiLogCount = errorLogCount = 0;
    memset(rebootLogs, 0, sizeof(rebootLogs));
    memset(wifiLogs, 0, sizeof(wifiLogs));
    memset(errorLogs, 0, sizeof(errorLogs));
    prefs.remove("reboot_logs");
    prefs.remove("reboot_log_count");
    prefs.remove("wifi_logs");
    prefs.remove("wifi_log_count");
    prefs.remove("error_logs");
    prefs.remove("error_log_count");
    xSemaphoreGive(flashWriteMutex);
  }
}

String formatResetReason(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "OTHER";
  }
}

static String getTaskStateName(eTaskState s) {
  switch (s) {
    case eRunning: return "RUNNING";
    case eReady: return "READY";
    case eBlocked: return "BLOCKED";
    case eSuspended: return "SUSPENDED";
    case eDeleted: return "DELETED";
    default: return "UNKNOWN";
  }
}

static String getStackHealth(uint32_t hwm) {
  if (hwm > 1500) return "good";
  if (hwm > 800) return "ok";
  if (hwm > 300) return "low";
  return "critical";
}

static String getAffinityString(BaseType_t affinity) {
  return (affinity == tskNO_AFFINITY) ? "ANY" : String(affinity);
}

static inline BaseType_t getSafeAffinity(TaskHandle_t handle) {
#if CONFIG_FREERTOS_UNICORE
  (void)handle;
  return 0;
#else
  if (handle == nullptr) return tskNO_AFFINITY;
  return xTaskGetAffinity(handle);
#endif
}

void updateTaskMonitoring() {
  if (millis() - lastTaskSample < 500) return;
  lastTaskSample = millis();

  TaskStatus_t statusArray[MAX_TASKS_MONITORED];
  UBaseType_t numTasks = uxTaskGetSystemState(statusArray, MAX_TASKS_MONITORED, NULL);
  if (numTasks == 0) {
    addErrorLog(F("Failed to get task state"), millis() / 1000);
    return;
  }

  for (int c = 0; c < NUM_CORES; c++) {
    coreRuntime[c].totalRuntime100ms = 0;
    coreRuntime[c].taskCount = 0;
    coreRuntime[c].cpuPercentTotal = 0;
  }
  noAffinityRuntime100ms = 0;

  for (uint8_t j = 0; j < numTasks; j++) {
    BaseType_t affinity = getSafeAffinity(statusArray[j].xHandle);
    uint32_t runtime = statusArray[j].ulRunTimeCounter;

    if (affinity == tskNO_AFFINITY) {
      noAffinityRuntime100ms += (uint64_t)runtime;
    } else if (affinity >= 0 && affinity < NUM_CORES) {
      coreRuntime[affinity].totalRuntime100ms += (uint64_t)runtime;
      coreRuntime[affinity].taskCount++;
    }
  }

  uint64_t coreDelta[2] = { 0, 0 };
  for (int c = 0; c < NUM_CORES; c++) {
    coreDelta[c] = coreRuntime[c].totalRuntime100ms - coreRuntime[c].prevTotalRuntime100ms;
    if (coreDelta[c] == 0) coreDelta[c] = 1;
  }

  if (!statsInitialized) {
    taskCount = min((uint8_t)numTasks, (uint8_t)MAX_TASKS_MONITORED);
    for (uint8_t i = 0; i < taskCount; i++) {
      taskData[i].name = String(statusArray[i].pcTaskName);
      taskData[i].priority = statusArray[i].uxCurrentPriority;
      taskData[i].state = statusArray[i].eCurrentState;
      taskData[i].runtime = statusArray[i].ulRunTimeCounter;
      taskData[i].prevRuntime = statusArray[i].ulRunTimeCounter;
      taskData[i].runtimeAccumUs = 0;
      taskData[i].stackHighWater = statusArray[i].xHandle ? uxTaskGetStackHighWaterMark(statusArray[i].xHandle) : 0;
      taskData[i].stackHealth = getStackHealth(taskData[i].stackHighWater);
      taskData[i].cpuPercent = 0;
      taskData[i].handle = statusArray[i].xHandle;
      taskData[i].coreAffinity = getSafeAffinity(statusArray[i].xHandle);
    }
    for (int c = 0; c < NUM_CORES; c++) {
      coreRuntime[c].prevTotalRuntime100ms = coreRuntime[c].totalRuntime100ms;
    }
    statsInitialized = true;
    return;
  }

  for (uint8_t i = 0; i < taskCount; i++) {
    bool found = false;
    for (uint8_t j = 0; j < numTasks; j++) {
      if (taskData[i].handle == statusArray[j].xHandle) {
        uint32_t currentRuntime100ms = statusArray[j].ulRunTimeCounter;

        uint32_t taskDelta;
        if (currentRuntime100ms >= taskData[i].prevRuntime) {

          taskDelta = currentRuntime100ms - taskData[i].prevRuntime;
        } else {

          if (isLikelyWraparound(taskData[i].prevRuntime, currentRuntime100ms)) {

            taskDelta = (0xFFFFFFFF - taskData[i].prevRuntime) + currentRuntime100ms + 1;
          } else {

            Serial.printf("[DEBUG] Task %s counter reset: %u -> %u (not wraparound)\n", 
                         taskData[i].name.c_str(), 
                         taskData[i].prevRuntime, 
                         currentRuntime100ms);
            taskDelta = 0;
          }
        }

        taskData[i].runtimeAccumUs += (uint64_t)taskDelta;

        BaseType_t affinity = getSafeAffinity(statusArray[j].xHandle);
        taskData[i].coreAffinity = affinity;

        if (affinity == tskNO_AFFINITY) {
          uint64_t totalCoreDelta = 0;
          for (int c = 0; c < NUM_CORES; c++) totalCoreDelta += coreDelta[c];
          if (totalCoreDelta > 0) {
            uint64_t percentage = ((uint64_t)taskDelta * 100ULL) / totalCoreDelta;
            taskData[i].cpuPercent = (percentage > 100) ? 100 : (uint8_t)percentage;
          } else {
            taskData[i].cpuPercent = 0;
          }
        } else if (affinity >= 0 && affinity < NUM_CORES) {
          if (coreDelta[affinity] > 0) {
            uint64_t percentage = ((uint64_t)taskDelta * 100ULL) / coreDelta[affinity];
            taskData[i].cpuPercent = (percentage > 100) ? 100 : (uint8_t)percentage;
          } else {
            taskData[i].cpuPercent = 0;
          }
          coreRuntime[affinity].cpuPercentTotal += taskData[i].cpuPercent;
        } else {
          taskData[i].cpuPercent = 0;
        }

        taskData[i].priority = statusArray[j].uxCurrentPriority;
        taskData[i].state = statusArray[j].eCurrentState;
        taskData[i].runtime = currentRuntime100ms;
        taskData[i].prevRuntime = currentRuntime100ms;
        taskData[i].stackHighWater = statusArray[j].xHandle ? uxTaskGetStackHighWaterMark(statusArray[j].xHandle) : 0;
        taskData[i].stackHealth = getStackHealth(taskData[i].stackHighWater);
        taskData[i].handle = statusArray[j].xHandle;
        found = true;
        break;
      }
    }
    if (!found) {
      taskData[i].cpuPercent = 0;
      taskData[i].state = eDeleted;
    }
  }

  for (uint8_t j = 0; j < numTasks && taskCount < MAX_TASKS_MONITORED; j++) {
    bool exists = false;
    for (uint8_t i = 0; i < taskCount; i++) {
      if (taskData[i].handle == statusArray[j].xHandle) {
        exists = true;
        break;
      }
    }
    if (!exists) {
      taskData[taskCount].name = String(statusArray[j].pcTaskName);
      taskData[taskCount].priority = statusArray[j].uxCurrentPriority;
      taskData[taskCount].state = statusArray[j].eCurrentState;
      taskData[taskCount].runtime = statusArray[j].ulRunTimeCounter;
      taskData[taskCount].prevRuntime = statusArray[j].ulRunTimeCounter;
      taskData[taskCount].runtimeAccumUs = 0;
      taskData[taskCount].stackHighWater = statusArray[j].xHandle ? uxTaskGetStackHighWaterMark(statusArray[j].xHandle) : 0;
      taskData[taskCount].stackHealth = getStackHealth(taskData[taskCount].stackHighWater);
      taskData[taskCount].cpuPercent = 0;
      taskData[taskCount].handle = statusArray[j].xHandle;
      taskData[taskCount].coreAffinity = getSafeAffinity(statusArray[j].xHandle);
      taskCount++;
    }
  }

  for (int c = 0; c < NUM_CORES; c++) {
    coreRuntime[c].prevTotalRuntime100ms = coreRuntime[c].totalRuntime100ms;
  }
}

void checkTaskStacks() {
  uint32_t now = millis();
  if (now - lastStackCheck < STACK_CHECK_INTERVAL) return;
  lastStackCheck = now;

  if (webTaskHandle) {
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(webTaskHandle);
    if (hwm < 500) {
      addErrorLog(String("webTask low stack: ") + hwm, millis() / 1000);
    }
  }
  if (bizTaskHandle) {
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(bizTaskHandle);
    if (hwm < 500) {
      addErrorLog(String("bizTask low stack: ") + hwm, millis() / 1000);
    }
  }
  if (sysTaskHandle) {
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(sysTaskHandle);
    if (hwm < 500) {
      addErrorLog(String("sysTask low stack: ") + hwm, millis() / 1000);
    }
  }
}

#endif