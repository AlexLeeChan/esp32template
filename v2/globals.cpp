/* ==============================================================================
   GLOBALS.CPP - Global Variable Definitions
   
   Defines (allocates memory for) all global variables declared in globals.h.
   These variables are shared across the entire application and must be properly
   synchronized using mutexes when accessed from multiple tasks.
   
   Initialization of complex objects (web server, BLE, etc.) happens in setup().
   ============================================================================== */

#include "globals.h"

#if ESP32_HAS_BLE
NimBLEServer* pBLEServer = nullptr;
NimBLECharacteristic* pTxCharacteristic = nullptr;
#endif

bool bleDeviceConnected = false;
bool bleOldDeviceConnected = false;

const char* BLE_SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
const char* BLE_CHAR_UUID_RX = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
const char* BLE_CHAR_UUID_TX = "beb5483e-36e1-4688-b7f5-ea07361b26a9";

NetworkConfig netConfig = {
  true,
  IPAddress(192, 168, 1, 100),
  IPAddress(192, 168, 1, 1),
  IPAddress(255, 255, 255, 0),
  IPAddress(8, 8, 8, 8)
};

WebServer server(80);
Preferences prefs;

SemaphoreHandle_t wifiMutex = nullptr;
SemaphoreHandle_t poolMutex = nullptr;
SemaphoreHandle_t timeMutex = nullptr;
volatile bool serverStarted = false;
volatile bool bootComplete = false;

WiFiCredentials wifiCredentials = { "", "", false };

volatile WiFiState wifiState = WIFI_STATE_IDLE;
uint32_t wifiLastConnectAttempt = 0;
uint32_t wifiLastDisconnectTime = 0;
bool wifiManualDisconnect = false;
bool wifiConfigChanged = false;
bool wifiWasConnected = false;
bool wifiFirstConnectDone = false;
bool wifiHasBeenConfigured = false;
uint8_t wifiReconnectAttempts = 0;

ExecMessage msgPool[MSG_POOL_SIZE];  /* Pre-allocated message pool avoids malloc/free */

volatile BizState gBizState = BIZ_STOPPED;
QueueHandle_t execQ = nullptr;
volatile uint32_t bizProcessed = 0;

TaskHandle_t webTaskHandle = nullptr;
TaskHandle_t bizTaskHandle = nullptr;
TaskHandle_t sysTaskHandle = nullptr;

#if ESP32_HAS_TEMP
temperature_sensor_handle_t s_temp_sensor = NULL;
#endif

bool timeInitialized = false;
time_t lastNtpSync = 0;

volatile uint8_t coreLoadPct[2] = { 0, 0 };

#if DEBUG_MODE
TaskMonitorData taskData[MAX_TASKS_MONITORED];
uint8_t taskCount = 0;
uint32_t lastTaskSample = 0;
bool statsInitialized = false;

CoreRuntimeData coreRuntime[2];
uint64_t noAffinityRuntime100ms = 0;

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

const char* NVS_FLAG_USER_REBOOT = "userReboot";
#endif

#if ENABLE_OTA
OTAStatus otaStatus = { OTA_IDLE, 0, "", true, 0 };
SemaphoreHandle_t otaMutex = nullptr;
SemaphoreHandle_t taskDeletionMutex = nullptr;
volatile bool tasksDeleted = false;
volatile bool webTaskShouldExit = false;
volatile bool bizTaskShouldExit = false;
volatile bool otaInProgress = false;
#endif

uint64_t runtimeOffsetUs = 0;