/* ==============================================================================
   GLOBALS.H - Global Variable Declarations
   
   Declares all globally accessible variables and objects including:
   - FreeRTOS primitives (task handles, mutexes, semaphores, queues)
   - System state variables (WiFi, OTA, Business logic states)
   - Shared resources (web server, BLE server, preferences storage)
   - Message pools and buffers
   
   All globals are declared here and defined in globals.cpp to avoid
   multiple definition errors.
   ============================================================================== */

/* Header guard to prevent multiple inclusion of globals.h */
#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "config.h"
#include "types.h"

#if ESP32_HAS_BLE
  #include <NimBLEDevice.h>
  #include <NimBLEServer.h>
#endif

#if ESP32_HAS_BLE
extern NimBLEServer* pBLEServer;
extern NimBLECharacteristic* pTxCharacteristic;
#endif

extern bool bleDeviceConnected;
extern bool bleOldDeviceConnected;

extern const char* BLE_SERVICE_UUID;
extern const char* BLE_CHAR_UUID_RX;
extern const char* BLE_CHAR_UUID_TX;

extern NetworkConfig netConfig;

extern WebServer server;
extern Preferences prefs;

extern SemaphoreHandle_t wifiMutex;
extern SemaphoreHandle_t poolMutex;
extern SemaphoreHandle_t timeMutex;
extern volatile bool serverStarted;
extern volatile bool bootComplete;

extern WiFiCredentials wifiCredentials;

extern volatile WiFiState wifiState;
extern uint32_t wifiLastConnectAttempt;
extern uint32_t wifiLastDisconnectTime;
extern bool wifiManualDisconnect;
extern bool wifiConfigChanged;
extern bool wifiWasConnected;
extern bool wifiFirstConnectDone;
extern bool wifiHasBeenConfigured;
extern uint8_t wifiReconnectAttempts;

extern ExecMessage msgPool[MSG_POOL_SIZE];  /* Pre-allocated message pool avoids malloc/free */

extern volatile BizState gBizState;
extern QueueHandle_t execQ;
extern volatile uint32_t bizProcessed;

extern TaskHandle_t webTaskHandle;
extern TaskHandle_t bizTaskHandle;
extern TaskHandle_t sysTaskHandle;

#if ESP32_HAS_TEMP
extern temperature_sensor_handle_t s_temp_sensor;
#endif

extern bool timeInitialized;
extern time_t lastNtpSync;

extern volatile uint8_t coreLoadPct[2];

#if DEBUG_MODE
#define MAX_TASKS_MONITORED 64

extern TaskMonitorData taskData[MAX_TASKS_MONITORED];
extern uint8_t taskCount;
extern uint32_t lastTaskSample;
extern bool statsInitialized;

extern CoreRuntimeData coreRuntime[2];
extern uint64_t noAffinityRuntime100ms;
extern uint32_t lastStackCheck;

extern LogEntry rebootLogs[MAX_DEBUG_LOGS];
extern uint8_t rebootLogCount;
extern LogEntry wifiLogs[MAX_DEBUG_LOGS];
extern uint8_t wifiLogCount;
extern LogEntry errorLogs[MAX_DEBUG_LOGS];
extern uint8_t errorLogCount;

extern QueueHandle_t flashWriteQueue;
extern TaskHandle_t flashWriteTaskHandle;
extern SemaphoreHandle_t flashWriteMutex;

extern const char* NVS_FLAG_USER_REBOOT;
#endif

#if ENABLE_OTA
extern OTAStatus otaStatus;
extern SemaphoreHandle_t otaMutex;
extern SemaphoreHandle_t taskDeletionMutex;
extern volatile bool tasksDeleted;
extern volatile bool webTaskShouldExit;
extern volatile bool bizTaskShouldExit;
extern volatile bool otaInProgress;
#endif

extern uint64_t runtimeOffsetUs;

#endif