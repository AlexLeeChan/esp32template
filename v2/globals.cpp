#include "globals.h"

// ============================================================================
// GLOBAL VARIABLE DEFINITIONS
// ============================================================================

// BLE Variables
#if ESP32_HAS_BLE
NimBLEServer* pBLEServer = nullptr;
NimBLECharacteristic* pTxCharacteristic = nullptr;
#endif

bool bleDeviceConnected = false;
bool bleOldDeviceConnected = false;

// BLE UUIDs
const char* BLE_SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
const char* BLE_CHAR_UUID_RX = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
const char* BLE_CHAR_UUID_TX = "beb5483e-36e1-4688-b7f5-ea07361b26a9";

// Network Configuration
NetworkConfig netConfig = {
  true,
  IPAddress(192, 168, 1, 100),
  IPAddress(192, 168, 1, 1),
  IPAddress(255, 255, 255, 0),
  IPAddress(8, 8, 8, 8)
};

// Web Server & Storage
WebServer server(80);
Preferences prefs;

// Thread Safety Mutexes
SemaphoreHandle_t wifiMutex = nullptr;
SemaphoreHandle_t poolMutex = nullptr;
SemaphoreHandle_t timeMutex = nullptr;
volatile bool serverStarted = false;

// WiFi Credentials Cache
WiFiCredentials wifiCredentials = { "", "", false };

// WiFi State Machine
volatile WiFiState wifiState = WIFI_STATE_IDLE;
uint32_t wifiLastConnectAttempt = 0;
uint32_t wifiLastDisconnectTime = 0;
bool wifiManualDisconnect = false;
bool wifiConfigChanged = false;
bool wifiWasConnected = false;
bool wifiFirstConnectDone = false;
bool wifiHasBeenConfigured = false;
uint8_t wifiReconnectAttempts = 0;

// Message Pool
ExecMessage msgPool[MSG_POOL_SIZE];

// Business Logic
volatile BizState gBizState = BIZ_STOPPED;
QueueHandle_t execQ = nullptr;
volatile uint32_t bizProcessed = 0;

// Task Handles
TaskHandle_t webTaskHandle = nullptr;
TaskHandle_t bizTaskHandle = nullptr;
TaskHandle_t sysTaskHandle = nullptr;

// Temperature Sensor
#if ESP32_HAS_TEMP
temperature_sensor_handle_t s_temp_sensor = NULL;
#endif

// Time Synchronization
bool timeInitialized = false;
time_t lastNtpSync = 0;

// ============================================================================
// CPU LOAD MONITORING (always enabled)
// ============================================================================
volatile uint8_t coreLoadPct[2] = { 0, 0 };

// ============================================================================
// DEBUG MODE VARIABLES
// ============================================================================
#if DEBUG_MODE

TaskMonitorData taskData[MAX_TASKS_MONITORED];
uint8_t taskCount = 0;
uint32_t lastTaskSample = 0;
bool statsInitialized = false;

CoreRuntimeData coreRuntime[2];
uint32_t lastStackCheck = 0;

LogEntry rebootLogs[MAX_DEBUG_LOGS];
uint8_t rebootLogCount = 0;
LogEntry wifiLogs[MAX_DEBUG_LOGS];
uint8_t wifiLogCount = 0;
LogEntry errorLogs[MAX_DEBUG_LOGS];
uint8_t errorLogCount = 0;

QueueHandle_t flashWriteQueue = nullptr;
TaskHandle_t flashWriteTaskHandle = nullptr;
SemaphoreHandle_t flashWriteMutex = nullptr;

const char* NVS_FLAG_USER_REBOOT = "userRebootRequested";

#endif // DEBUG_MODE

// ============================================================================
// OTA VARIABLES
// ============================================================================
#if ENABLE_OTA
OTAStatus otaStatus = { OTA_IDLE, 0, "", true, 0 };
SemaphoreHandle_t otaMutex = nullptr;
SemaphoreHandle_t taskDeletionMutex = nullptr;
volatile bool tasksDeleted = false;
volatile bool webTaskShouldExit = false;
volatile bool bizTaskShouldExit = false;
volatile bool otaInProgress = false;
#endif

// FreeRTOS Runtime Counter
uint64_t runtimeOffsetUs = 0;
