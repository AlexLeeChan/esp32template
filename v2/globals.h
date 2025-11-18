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

// ============================================================================
// GLOBAL VARIABLE DECLARATIONS (extern)
// ============================================================================

// BLE Variables
#if ESP32_HAS_BLE
extern NimBLEServer* pBLEServer;
extern NimBLECharacteristic* pTxCharacteristic;
#endif

extern bool bleDeviceConnected;
extern bool bleOldDeviceConnected;

// BLE UUIDs
extern const char* BLE_SERVICE_UUID;
extern const char* BLE_CHAR_UUID_RX;
extern const char* BLE_CHAR_UUID_TX;

// Network Configuration
extern NetworkConfig netConfig;

// Web Server & Storage
extern WebServer server;
extern Preferences prefs;

// Thread Safety Mutexes
extern SemaphoreHandle_t wifiMutex;
extern SemaphoreHandle_t poolMutex;
extern SemaphoreHandle_t timeMutex;
extern volatile bool serverStarted;

// WiFi Credentials Cache
extern WiFiCredentials wifiCredentials;

// WiFi State Machine
extern volatile WiFiState wifiState;
extern uint32_t wifiLastConnectAttempt;
extern uint32_t wifiLastDisconnectTime;
extern bool wifiManualDisconnect;
extern bool wifiConfigChanged;
extern bool wifiWasConnected;
extern bool wifiFirstConnectDone;
extern bool wifiHasBeenConfigured;
extern uint8_t wifiReconnectAttempts;

// Message Pool
extern ExecMessage msgPool[MSG_POOL_SIZE];

// Business Logic
extern volatile BizState gBizState;
extern QueueHandle_t execQ;
extern volatile uint32_t bizProcessed;

// Task Handles
extern TaskHandle_t webTaskHandle;
extern TaskHandle_t bizTaskHandle;
extern TaskHandle_t sysTaskHandle;

// Temperature Sensor
#if ESP32_HAS_TEMP
extern temperature_sensor_handle_t s_temp_sensor;
#endif

// Time Synchronization
extern bool timeInitialized;
extern time_t lastNtpSync;

// ============================================================================
// CPU LOAD MONITORING (always enabled for status API)
// ============================================================================
// These are the ONLY CPU monitoring variables needed outside DEBUG_MODE
// They provide core load percentage for the web UI

extern volatile uint8_t coreLoadPct[2];  // Core load % (0-100) for Core 0 and Core 1

// ============================================================================
// DEBUG MODE VARIABLES
// ============================================================================
#if DEBUG_MODE

// Task monitoring arrays
#ifndef MAX_TASKS_MONITORED
#define MAX_TASKS_MONITORED 64
#endif

extern TaskMonitorData taskData[MAX_TASKS_MONITORED];
extern uint8_t taskCount;
extern uint32_t lastTaskSample;
extern bool statsInitialized;

// Core runtime tracking
extern CoreRuntimeData coreRuntime[2];
extern uint32_t lastStackCheck;

// Debug logs
extern LogEntry rebootLogs[MAX_DEBUG_LOGS];
extern uint8_t rebootLogCount;
extern LogEntry wifiLogs[MAX_DEBUG_LOGS];
extern uint8_t wifiLogCount;
extern LogEntry errorLogs[MAX_DEBUG_LOGS];
extern uint8_t errorLogCount;

// Flash write queue
extern QueueHandle_t flashWriteQueue;
extern TaskHandle_t flashWriteTaskHandle;
extern SemaphoreHandle_t flashWriteMutex;

extern const char* NVS_FLAG_USER_REBOOT;

#endif // DEBUG_MODE

// ============================================================================
// OTA VARIABLES
// ============================================================================
#if ENABLE_OTA
extern OTAStatus otaStatus;
extern SemaphoreHandle_t otaMutex;
extern SemaphoreHandle_t taskDeletionMutex;
extern volatile bool tasksDeleted;
extern volatile bool webTaskShouldExit;
extern volatile bool bizTaskShouldExit;
extern volatile bool otaInProgress;
#endif

// FreeRTOS Runtime Counter
extern uint64_t runtimeOffsetUs;

#endif // GLOBALS_H
