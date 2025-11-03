/*
  Universal ESP32(x) Template - PER-CORE ISOLATION
  ESP-IDF 5.1 Compatible
    Features:
  - Per-core runtime tracking with separate monitoring for each core
  - Task affinity detection using xTaskGetAffinity()
  - Per-core CPU percentage calculation relative to each core's runtime
  - Architecture-aware: Handles single-core (C3/C6/S2) and dual-core (ESP32/S3)
  - BLE configuration and control (where available)
  - Wi-Fi with DHCP/static IP
  - HTTP API + dashboard
  - Message pool (no malloc in hot path)
*/
// ===== DEBUG MODE FLAG =====
#define DEBUG_MODE 1
#ifndef CONFIG_FREERTOS_USE_CUSTOM_RUN_TIME_STATS_HOOKS
#define CONFIG_FREERTOS_USE_CUSTOM_RUN_TIME_STATS_HOOKS 1
#endif
#ifndef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
#define CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS 1
#endif
#ifndef CONFIG_FREERTOS_USE_TRACE_FACILITY
#define CONFIG_FREERTOS_USE_TRACE_FACILITY 1
#endif
#ifndef CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
#define CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS 1
#endif

// Override FreeRTOS port macros to use custom run time stats
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS configureTimerForRunTimeStats
#define portGET_RUN_TIME_COUNTER_VALUE ulGetRunTimeCounterValue
#define portALT_GET_RUN_TIME_COUNTER_VALUE ulGetRunTimeCounterValue

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "esp_freertos_hooks.h"
#include "esp_task_wdt.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <math.h>
#include <pgmspace.h>
// ===== target family detection =====
#if defined(CONFIG_IDF_TARGET_ESP32)
#define RNGDS_HAS_BLE 1
#define RNGDS_HAS_TEMP 0
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define RNGDS_HAS_BLE 1
#define RNGDS_HAS_TEMP 1
#include "driver/temperature_sensor.h"
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#define RNGDS_HAS_BLE 1
#define RNGDS_HAS_TEMP 1
#include "driver/temperature_sensor.h"
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
#define RNGDS_HAS_BLE 1
#define RNGDS_HAS_TEMP 1
#include "driver/temperature_sensor.h"
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
#define RNGDS_HAS_BLE 0
#define RNGDS_HAS_TEMP 1
#include "driver/temperature_sensor.h"
#else
#define RNGDS_HAS_BLE 1
#define RNGDS_HAS_TEMP 0
#endif
// ===== Universal Serial Port =====
// Define a single 'HWSerial' object to handle USB-CDC (Serial0) vs UART (Serial)
//#if ARDUINO_USB_CDC_ON_BOOT
// For chips with native USB (S2, S3, C3, C6)
//#define HWSerial Serial0
//#else
// For chips with external USB-UART (classic ESP32)
#define HWSerial Serial
//#endif
#if RNGDS_HAS_BLE
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#endif
// ===== message pool config =====
#define MSG_POOL_SIZE 10
#define MAX_MSG_SIZE 256
struct ExecMessage {
  char payload[MAX_MSG_SIZE];
  uint16_t length;
  bool inUse;
};
// ===== runtime stats source for FreeRTOS =====
// 64-bit version: true microsecond precision, ~584 000 years before wrap
// Replace below if you want ultra-long drift-free tracking

// ===== CPU load metering (declare before functions that use them) =====
volatile uint32_t idleCnt[2] = { 0, 0 };
volatile uint32_t idleCal[2] = { 1, 1 };
volatile uint32_t lastIdle[2] = { 0, 0 };
volatile uint8_t coreLoadPct[2] = { 0, 0 };
uint32_t lastLoadTs = 0;

static uint64_t runtimeOffsetUs = 0;

extern "C" void configureTimerForRunTimeStats(void) {
  runtimeOffsetUs = esp_timer_get_time();
}

extern "C" uint64_t ulGetRunTimeCounterValue(void) {
  return (esp_timer_get_time() - runtimeOffsetUs);  // µs resolution
}
#define portGET_RUN_TIME_COUNTER_VALUE ulGetRunTimeCounterValue
#define portALT_GET_RUN_TIME_COUNTER_VALUE ulGetRunTimeCounterValue

// ===== system configuration =====
#define WDT_TIMEOUT 20
#define MAX_BLE_CMD_LENGTH 256
#define STACK_CHECK_INTERVAL 60000
#define WIFI_RECONNECT_DELAY 15000
#define WIFI_CONNECT_TIMEOUT 30000
// ===== board LED per family =====
#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define BLE_LED_PIN 8
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
#define BLE_LED_PIN 15
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define BLE_LED_PIN 48
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
#define BLE_LED_PIN 18
#else
#define BLE_LED_PIN 2
#endif
#define BLE_LED_BLINK_MS 500
// ===== core count helper =====
#if CONFIG_FREERTOS_UNICORE
#define NUM_CORES 1
#else
#define NUM_CORES 2
#endif
// ===== BLE profile =====
static const char* BLE_SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
static const char* BLE_CHAR_UUID_RX = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
static const char* BLE_CHAR_UUID_TX = "beb5483e-36e1-4688-b7f5-ea07361b26a9";
#if RNGDS_HAS_BLE
NimBLEServer* pBLEServer = nullptr;
NimBLECharacteristic* pTxCharacteristic = nullptr;
#endif
bool bleDeviceConnected = false;
bool bleOldDeviceConnected = false;
// ===== persistent network config in NVS =====
struct {
  bool useDHCP;
  IPAddress staticIP;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns;
} netConfig = {
  true,
  IPAddress(192, 168, 1, 100),
  IPAddress(192, 168, 1, 1),
  IPAddress(255, 255, 255, 0),
  IPAddress(8, 8, 8, 8)
};
WebServer server(80);
Preferences prefs;
// ===== task monitoring WITH PER-CORE AWARENESS =====
#define MAX_TASKS_MONITORED 64
struct TaskMonitorData {
  String name;
  UBaseType_t priority;
  eTaskState state;
  uint32_t runtime;
  uint32_t prevRuntime;
  uint64_t runtimeAccumUs; // 64-bit wrap-free accumulator of task runtime in microseconds
  uint32_t stackHighWater;
  uint8_t cpuPercent; // Per-core aware instantaneous CPU%
  String stackHealth;
  BaseType_t coreAffinity; // tskNO_AFFINITY, 0, or 1
  TaskHandle_t handle; // Store handle for affinity queries
};
TaskMonitorData taskData[MAX_TASKS_MONITORED];
uint32_t lastTotalRuntime100ms = 0;
uint8_t taskCount = 0;
uint32_t lastTaskSample = 0;
bool statsInitialized = false;
// ===== PER-CORE runtime tracking =====
struct CoreRuntimeData {
  uint64_t totalRuntime100ms; // Total runtime for this core
  uint64_t prevTotalRuntime100ms; // Previous sample
  uint8_t taskCount; // Number of tasks pinned to this core
  uint8_t cpuPercentTotal; // Sum of all task CPU% (should be ~100%)
};
CoreRuntimeData coreRuntime[2]; // Index 0 = Core 0, Index 1 = Core 1
uint64_t noAffinityRuntime100ms = 0; // Tasks with no affinity
uint64_t prevNoAffinityRuntime100ms = 0;
// Long-term wrap-free accumulators (microseconds)
uint64_t coreAccumUs[2] = { 0, 0 };
uint64_t noAffinityAccumUs = 0;
// ISR/Overhead accounting
uint64_t isrOverheadAccumUs = 0; // accumulated time not accounted to any task
uint64_t lastSampleWallUs = 0;   // last sample timestamp (esp_timer_get_time)
// Idle-based effective runtime accumulator (milliseconds across all cores)
uint64_t idleEffectiveMsAccum = 0;
uint64_t idleEffLastSampleUs = 0;
uint64_t lastTotalRuntimeUs = 0; // fallback source when load is unavailable
static esp_timer_handle_t s_idleEffTimer = NULL;

static void idleEffTimerCb(void* arg) {
  (void)arg;
  uint64_t nowUs = esp_timer_get_time();
  
  // First call: seed accumulator to current uptime
  if (idleEffLastSampleUs == 0) {
    idleEffectiveMsAccum = nowUs / 1000ULL;  // Seed to current uptime
    idleEffLastSampleUs = nowUs;
    return;  // Exit, start accumulating from next callback
  }
  
  uint64_t elapsedUs = nowUs - idleEffLastSampleUs;
  idleEffLastSampleUs = nowUs;
  
  if (elapsedUs == 0) return;
  if (elapsedUs > 500000ULL) elapsedUs = 500000ULL; // clamp to 500ms window
  
  // Accumulate wall-clock time
  idleEffectiveMsAccum += elapsedUs / 1000ULL;
}

uint32_t lastStackCheck = 0;
// ===== heap tracking =====
uint32_t lastHeapCheck = 0;
uint32_t minHeapEver = UINT32_MAX;
// ===== business logic structures =====
enum BizState : uint8_t { BIZ_STOPPED = 0,
                          BIZ_RUNNING = 1 };
volatile BizState gBizState = BIZ_STOPPED;
QueueHandle_t execQ = nullptr;
volatile uint32_t bizProcessed = 0;
TaskHandle_t webTaskHandle = nullptr;
TaskHandle_t bizTaskHandle = nullptr;
TaskHandle_t sysTaskHandle = nullptr;
// ===== Wi-Fi state machine =====
enum WiFiState : uint8_t {
  WIFI_STATE_IDLE = 0,
  WIFI_STATE_CONNECTING,
  WIFI_STATE_CONNECTED,
  WIFI_STATE_DISCONNECTED
};
volatile WiFiState wifiState = WIFI_STATE_IDLE;
uint32_t wifiLastConnectAttempt = 0;
bool wifiManualDisconnect = false;
uint8_t wifiReconnectAttempts = 0;
const uint8_t MAX_WIFI_RECONNECT_ATTEMPTS = 10;
// ===== temperature handle (IDF temp sensor) =====
#if RNGDS_HAS_TEMP
static temperature_sensor_handle_t s_temp_sensor = NULL;
#endif
// ===== message pool =====
ExecMessage msgPool[MSG_POOL_SIZE];
SemaphoreHandle_t poolMutex = nullptr;
// ===== forward declarations =====
void handleBLECommand(String cmd);
void setupWiFi();
void checkWiFiConnection();
void updateTaskMonitoring();
void sendTaskMonitoring();
void sendDebugInfo();
void routes();
void systemTask(void* param);
void webTask(void* param);
void bizTask(void* param);
static void handleApiStatus();
// ============================================================================
// message pool
// ============================================================================
void initMessagePool() {
  poolMutex = xSemaphoreCreateMutex();
  for (int i = 0; i < MSG_POOL_SIZE; i++) {
    msgPool[i].inUse = false;
    msgPool[i].length = 0;
  }
}
ExecMessage* allocMessage() {
  if (xSemaphoreTake(poolMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    for (int i = 0; i < MSG_POOL_SIZE; i++) {
      if (!msgPool[i].inUse) {
        msgPool[i].inUse = true;
        xSemaphoreGive(poolMutex);
        return &msgPool[i];
      }
    }
    xSemaphoreGive(poolMutex);
  }
  return nullptr;
}
void freeMessage(ExecMessage* msg) {
  if (msg && xSemaphoreTake(poolMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    msg->inUse = false;
    msg->length = 0;
    xSemaphoreGive(poolMutex);
  }
}
// ============================================================================
// Timing Utilities
// ============================================================================
inline uint64_t micros64() {
  return (uint64_t)esp_timer_get_time();
}
inline float usToSec(uint64_t us) {
  return (float)us / 1e6f;
}
inline float usToMs(uint64_t us) {
  return (float)us / 1000.0f;
}
// ============================================================================
// idle hooks -> raw idle counters per core
// ============================================================================
extern "C" bool idleHook0() {
  idleCnt[0]++;
  return true;
}
#if NUM_CORES > 1
extern "C" bool idleHook1() {
  idleCnt[1]++;
  return true;
}
#endif
// ============================================================================
// CPU load calibration + periodic load calc
// ============================================================================
void startCpuLoadMonitor() {
  esp_register_freertos_idle_hook_for_cpu(idleHook0, 0);
#if NUM_CORES > 1
  esp_register_freertos_idle_hook_for_cpu(idleHook1, 1);
#endif
  uint32_t s0[2] = { idleCnt[0], idleCnt[1] };
  uint64_t t0 = esp_timer_get_time();
  delay(3000);
  uint64_t t1 = esp_timer_get_time();
  uint32_t d0 = idleCnt[0] - s0[0];
  uint32_t d1 = (NUM_CORES > 1) ? (idleCnt[1] - s0[1]) : 1;
  uint32_t elapsedMs = (t1 - t0) / 1000;
  idleCal[0] = max(1UL, (uint32_t)((float)d0 * (1000.0f / elapsedMs)));
  idleCal[1] = max(1UL, (uint32_t)((float)d1 * (1000.0f / elapsedMs)));
  lastIdle[0] = idleCnt[0];
  lastIdle[1] = idleCnt[1];
  lastLoadTs = millis();
  
  // FIX: Seed integrator to current uptime to avoid startup drift
  uint64_t currentUptimeUs = esp_timer_get_time();
 
}
void updateCpuLoad() {
  uint32_t now = millis();
  if (now - lastLoadTs < 1000) return;
  uint32_t elapsed = now - lastLoadTs;
  lastLoadTs = now;
  for (int c = 0; c < NUM_CORES; c++) {
    uint32_t di = idleCnt[c] - lastIdle[c];
    lastIdle[c] = idleCnt[c];
    float ratio = (float)(di * (1000.0f / elapsed)) / idleCal[c];
    if (ratio > 1.0f) ratio = 1.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    uint8_t newLoad = (uint8_t)(100.0f * (1.0f - ratio) + 0.5f);
    coreLoadPct[c] = (coreLoadPct[c] + newLoad) / 2; // less smoothing (EMA 1/2)
  }
}
// ============================================================================
// heap + stack checks
// ============================================================================
void monitorHeapHealth() {
  uint32_t now = millis();
  if (now - lastHeapCheck < 60000) return;
  lastHeapCheck = now;
  uint32_t currentFree = ESP.getFreeHeap();
  uint32_t minFree = ESP.getMinFreeHeap();
  if (minFree < minHeapEver) {
    minHeapEver = minFree;
  }
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32S2)
  if (currentFree < 10000) {
  }
#else
  if (currentFree < 20000) {
  }
#endif
}
void checkTaskStacks() {
  uint32_t now = millis();
  if (now - lastStackCheck < STACK_CHECK_INTERVAL) return;
  lastStackCheck = now;
  if (webTaskHandle) {
    UBaseType_t w = uxTaskGetStackHighWaterMark(webTaskHandle);
    if (w < 512) {
    }
  }
  if (bizTaskHandle) {
    UBaseType_t w = uxTaskGetStackHighWaterMark(bizTaskHandle);
    if (w < 512) {
    }
  }
  if (sysTaskHandle) {
    UBaseType_t w = uxTaskGetStackHighWaterMark(sysTaskHandle);
    if (w < 512) {
    }
  }
}
// ============================================================================
// small string helpers
// ============================================================================
String cleanString(const String& input) {
  if (input.length() == 0) return String();
  const char* str = input.c_str();
  int len = input.length();
  int start = 0;
  int end = len - 1;
  while (start < len && isspace((unsigned char)str[start])) start++;
  while (end >= start && isspace((unsigned char)str[end])) end--;
  if (start > end) return String();
  return input.substring(start, end + 1);
}
bool isValidIP(const String& s) {
  IPAddress ip;
  if (!ip.fromString(s)) return false;
  return !(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
}
bool isValidSubnet(const String& s) {
  IPAddress sub;
  if (!sub.fromString(s)) return false;
  uint32_t m = ~(uint32_t)sub;
  return (m & (m + 1)) == 0;
}
IPAddress parseIP(const String& s) {
  IPAddress ip;
  if (ip.fromString(s)) return ip;
  int p[4] = { 0 };
  int idx = 0, st = 0;
  for (int i = 0; i <= s.length() && idx < 4; i++) {
    if (i == s.length() || s[i] == '.') {
      p[idx++] = s.substring(st, i).toInt();
      st = i + 1;
    }
  }
  return IPAddress(p[0], p[1], p[2], p[3]);
}
// ============================================================================
// NVS helpers
// ============================================================================
void saveWiFi(const String& s, const String& p) {
  prefs.putString("wifi_ssid", s);
  prefs.putString("wifi_pass", p);
}
void saveNetworkConfig() {
  prefs.putBool("use_dhcp", netConfig.useDHCP);
  prefs.putUInt("static_ip", (uint32_t)netConfig.staticIP);
  prefs.putUInt("gateway", (uint32_t)netConfig.gateway);
  prefs.putUInt("subnet", (uint32_t)netConfig.subnet);
  prefs.putUInt("dns", (uint32_t)netConfig.dns);
}
void loadNetworkConfig() {
  netConfig.useDHCP = prefs.getBool("use_dhcp", true);
  netConfig.staticIP = prefs.getUInt("static_ip", (uint32_t)IPAddress(192, 168, 1, 100));
  netConfig.gateway = prefs.getUInt("gateway", (uint32_t)IPAddress(192, 168, 1, 1));
  netConfig.subnet = prefs.getUInt("subnet", (uint32_t)IPAddress(255, 255, 255, 0));
  netConfig.dns = prefs.getUInt("dns", (uint32_t)IPAddress(8, 8, 8, 8));
}
// ============================================================================
// temperature helper
// ============================================================================
float getInternalTemperatureC() {
  #if RNGDS_HAS_TEMP
  if (s_temp_sensor == NULL) return NAN;
  float tsens_out;
  if (temperature_sensor_get_celsius(s_temp_sensor, &tsens_out) == ESP_OK) {
    return tsens_out;
  }
  return NAN;
  #else
  return NAN;
  #endif
}
// ============================================================================
// BLE send (notify) or stub
// ============================================================================
void sendBLE(const String& m) {
  #if RNGDS_HAS_BLE
  if (!bleDeviceConnected || !pTxCharacteristic) return;
  const char* data = m.c_str();
  size_t len = m.length();
  if (len <= 512) {
    pTxCharacteristic->setValue((uint8_t*)data, len);
    pTxCharacteristic->notify();
  } else {
    for (size_t i = 0; i < len; i += 512) {
      size_t chunk_size = (len - i > 512) ? 512 : (len - i);
      pTxCharacteristic->setValue((uint8_t*)(data + i), chunk_size);
      pTxCharacteristic->notify();
      delay(20);
    }
  }
  #else
  (void)m;
  #endif
}
// ============================================================================
// BLE init + RX handling
// ============================================================================
#if RNGDS_HAS_BLE
class MyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    bleDeviceConnected = true;
    NimBLEDevice::getAdvertising()->stop();
    pServer->updateConnParams(connInfo.getConnHandle(), 30, 60, 0, 400);
  }
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    bleDeviceConnected = false;
  }
};
class BLECommandBuffer {
private:
  char buffer[MAX_BLE_CMD_LENGTH];
  size_t pos;
public:
  BLECommandBuffer()
    : pos(0) {
    buffer[0] = '\0';
  }
  void append(const uint8_t* data, size_t len) {
    size_t available = MAX_BLE_CMD_LENGTH - pos - 1;
    size_t to_copy = (len < available) ? len : available;
    if (to_copy > 0) {
      memcpy(buffer + pos, data, to_copy);
      pos += to_copy;
      buffer[pos] = '\0';
    }
  }
  bool hasComplete() const {
    return strchr(buffer, '\n') != nullptr;
  }
  String extractCommand() {
    char* newline = strchr(buffer, '\n');
    if (!newline) return String();
    *newline = '\0';
    String cmd(buffer);
    size_t remaining = pos - (newline - buffer + 1);
    if (remaining > 0) {
      memmove(buffer, newline + 1, remaining);
    }
    pos = remaining;
    buffer[pos] = '\0';
    return cmd;
  }
};
static BLECommandBuffer cmdBuffer;
class MyCharCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
    std::string value = pChar->getValue();
    if (value.length() > 0) {
      cmdBuffer.append((uint8_t*)value.data(), value.length());
      while (cmdBuffer.hasComplete()) {
        String cmd = cmdBuffer.extractCommand();
        cmd = cleanString(cmd);
        if (cmd.length() > 0) {
          handleBLECommand(cmd);
        }
      }
    }
  }
};
void initBLE() {
  NimBLEDevice::init("RNGDS_ESP32x");
  NimBLEDevice::setMTU(256);
  pBLEServer = NimBLEDevice::createServer();
  pBLEServer->setCallbacks(new MyServerCallbacks());
  NimBLEService* pService = pBLEServer->createService(BLE_SERVICE_UUID);
  pTxCharacteristic = pService->createCharacteristic(
    BLE_CHAR_UUID_TX,
    NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* pRxCharacteristic = pService->createCharacteristic(
    BLE_CHAR_UUID_RX,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pRxCharacteristic->setCallbacks(new MyCharCallbacks());
  pService->start();
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  NimBLEAdvertisementData advData;
  advData.setName("RNGDS");
  advData.addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setAdvertisementData(advData);
  NimBLEAdvertisementData scanData;
  scanData.setName("RNGDS");
  pAdvertising->setScanResponseData(scanData);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  pAdvertising->start();
}
void handleBLEReconnect() {
  if (!bleDeviceConnected && bleOldDeviceConnected) {
    delay(500);
    NimBLEDevice::startAdvertising();
    bleOldDeviceConnected = bleDeviceConnected;
  }
  if (bleDeviceConnected && !bleOldDeviceConnected) {
    bleOldDeviceConnected = bleDeviceConnected;
  }
}
#else
void initBLE() {
}
void handleBLEReconnect() {}
#endif
// ============================================================================
// BLE command handler
// ============================================================================
void handleBLECommand(String cmd) {
  cmd = cleanString(cmd);
  cmd.trim();
  String upper = cmd;
  upper.toUpperCase();
  String response;
  response.reserve(128);
  if (upper.startsWith("SET_WIFI|") || upper.startsWith("WIFI:")) {
    String ssid, pass;
    if (upper.startsWith("SET_WIFI|")) {
      String rest = cmd.substring(9);
      int sep = rest.indexOf('|');
      if (sep > 0) {
        ssid = rest.substring(0, sep);
        pass = rest.substring(sep + 1);
      } else {
        sendBLE("ERR:FORMAT\n");
        return;
      }
    } else {
      String rest = cmd.substring(5);
      int sep = rest.indexOf(',');
      if (sep > 0) {
        ssid = rest.substring(0, sep);
        pass = rest.substring(sep + 1);
      } else {
        sendBLE("ERR:FORMAT\n");
        return;
      }
    }
    saveWiFi(ssid, pass);
    wifiManualDisconnect = false;
    wifiReconnectAttempts = 0;
    wifiState = WIFI_STATE_IDLE;
    sendBLE("OK:WIFI_SAVED\n");
    WiFi.disconnect(true, true);
    delay(500);
    WiFi.mode(WIFI_OFF);
    delay(500);
    WiFi.mode(WIFI_STA);
    delay(100);
    if (!netConfig.useDHCP) {
      WiFi.config(netConfig.staticIP, netConfig.gateway,
                  netConfig.subnet, netConfig.dns);
    }
    WiFi.begin(ssid.c_str(), pass.c_str());
    wifiState = WIFI_STATE_CONNECTING;
    wifiLastConnectAttempt = millis();
  } else if (upper.startsWith("SET_IP|")) {
    String rest = cmd.substring(7);
    if (rest.equalsIgnoreCase("DHCP")) {
      netConfig.useDHCP = true;
      saveNetworkConfig();
      sendBLE("OK:DHCP_ON\n");
    } else if (rest.startsWith("STATIC|") || rest.startsWith("static|")) {
      rest = rest.substring(7);
      int p1 = rest.indexOf('|');
      int p2 = rest.indexOf('|', p1 + 1);
      int p3 = rest.indexOf('|', p2 + 1);
      if (p1 > 0 && p2 > 0 && p3 > 0) {
        String ip = rest.substring(0, p1);
        String gw = rest.substring(p1 + 1, p2);
        String sub = rest.substring(p2 + 1, p3);
        String dns = rest.substring(p3 + 1);
        if (isValidIP(ip) && isValidIP(gw)) {
          netConfig.staticIP = parseIP(ip);
          netConfig.gateway = parseIP(gw);
          netConfig.subnet = isValidSubnet(sub) ? parseIP(sub) : IPAddress(255, 255, 255, 0);
          netConfig.dns = isValidIP(dns) ? parseIP(dns) : IPAddress(8, 8, 8, 8);
          netConfig.useDHCP = false;
          saveNetworkConfig();
          sendBLE("OK:STATIC_IP_SET\n");
        } else {
          sendBLE("ERR:INVALID_IP\n");
        }
      } else {
        sendBLE("ERR:FORMAT\n");
      }
    } else {
      sendBLE("ERR:FORMAT\n");
    }
  } else if (upper == "GET_STATUS" || upper == "STATUS") {
    bool connected = (WiFi.status() == WL_CONNECTED);
    response = "STATUS|";
    response += connected ? "CONNECTED" : "DISCONNECTED";
    response += "|";
    response += WiFi.SSID();
    response += "|";
    response += connected ? WiFi.localIP().toString() : "-";
    response += "|";
    response += connected ? String(WiFi.RSSI()) : "-";
    response += "|";
    response += netConfig.useDHCP ? "DHCP" : "STATIC";
    if (!netConfig.useDHCP) {
      response += "|";
      response += netConfig.staticIP.toString();
    }
    response += "\n";
    sendBLE(response);
  } else if (upper == "DISCONNECT_WIFI" || upper == "DISCONNECT") {
    WiFi.disconnect(true, true);
    delay(200);
    wifiManualDisconnect = true;
    wifiState = WIFI_STATE_DISCONNECTED;
    sendBLE("OK:WIFI_DISCONNECTED\n");
  } else if (upper == "CLEAR_SAVED" || upper == "CLEAR_WIFI") {
    prefs.remove("wifi_ssid");
    prefs.remove("wifi_pass");
    WiFi.disconnect(true, true);
    delay(200);
    wifiManualDisconnect = true;
    wifiState = WIFI_STATE_IDLE;
    sendBLE("OK:WIFI_CLEARED\n");
  } else if (upper == "RESTART") {
    sendBLE("OK:RESTARTING\n");
    delay(150);
    ESP.restart();
  } else if (upper == "HEAP") {
    response = "HEAP:FREE=";
    response += String(ESP.getFreeHeap());
    response += "|MIN=";
    response += String(ESP.getMinFreeHeap());
    response += "|MAX=";
    response += String(ESP.getMaxAllocHeap());
    response += "\n";
    sendBLE(response);
  } else if (upper == "TEMP") {
    float t = getInternalTemperatureC();
    if (isnan(t)) {
      sendBLE("TEMP:NOT_AVAILABLE\n");
    } else {
      sendBLE("TEMP:" + String(t, 2) + "\n");
    }
  } else {
    sendBLE("ERR:UNKNOWN_CMD\n");
  }
}
// ============================================================================
// Wi-Fi bringup + state machine
// ============================================================================
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  String ssid = prefs.getString("wifi_ssid", "");
  String pass = prefs.getString("wifi_pass", "");
  if (ssid.length() == 0) {
    return;
  }
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
      case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        wifiState = WIFI_STATE_CONNECTED;
        break;
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        wifiState = WIFI_STATE_CONNECTED;
        break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        if (!wifiManualDisconnect) {
          wifiState = WIFI_STATE_DISCONNECTED;
        }
        break;
      default:
        break;
    }
  });
  if (!netConfig.useDHCP) {
    WiFi.config(netConfig.staticIP, netConfig.gateway, netConfig.subnet, netConfig.dns);
  } else {
  }
  WiFi.begin(ssid.c_str(), pass.c_str());
  wifiState = WIFI_STATE_CONNECTING;
  wifiLastConnectAttempt = millis();
  wifiReconnectAttempts = 1;
}
void checkWiFiConnection() {
  if (wifiManualDisconnect) return;
  uint32_t now = millis();
  wl_status_t status = WiFi.status();
  switch (wifiState) {
    case WIFI_STATE_IDLE:
      {
        if (prefs.getString("wifi_ssid", "").length() > 0) {
          if (wifiReconnectAttempts >= MAX_WIFI_RECONNECT_ATTEMPTS) {
            wifiManualDisconnect = true;
            return;
          }
          wifiState = WIFI_STATE_CONNECTING;
          wifiLastConnectAttempt = now;
          String ssid = prefs.getString("wifi_ssid", "");
          String pass = prefs.getString("wifi_pass", "");
          WiFi.begin(ssid.c_str(), pass.c_str());
          wifiReconnectAttempts++;
        }
      }
      break;
    case WIFI_STATE_CONNECTING:
      {
        if (status == WL_CONNECTED) {
          wifiState = WIFI_STATE_CONNECTED;
          wifiReconnectAttempts = 0;
          
        } else if (now - wifiLastConnectAttempt > WIFI_CONNECT_TIMEOUT) {
          wifiState = WIFI_STATE_DISCONNECTED;
        }
      }
      break;
    case WIFI_STATE_CONNECTED:
      {
        if (status != WL_CONNECTED) {
          wifiState = WIFI_STATE_DISCONNECTED;
        }
      }
      break;
    case WIFI_STATE_DISCONNECTED:
      {
        uint8_t maxShift = (wifiReconnectAttempts < 4) ? wifiReconnectAttempts : 4;
        uint32_t backoffDelay = WIFI_RECONNECT_DELAY * (1 << maxShift);
        if (now - wifiLastConnectAttempt > backoffDelay) {
          wifiState = WIFI_STATE_IDLE;
        }
      }
      break;
  }
}
// ============================================================================
// task monitor helpers
// ============================================================================
String getTaskStateName(eTaskState s) {
  switch (s) {
    case eRunning: return "RUNNING";
    case eReady: return "READY";
    case eBlocked: return "BLOCKED";
    case eSuspended: return "SUSPENDED";
    case eDeleted: return "DELETED";
    default: return "UNKNOWN";
  }
}
String getStackHealth(uint32_t hwm) {
  if (hwm > 1500) return "good";
  if (hwm > 800) return "ok";
  if (hwm > 300) return "low";
  return "critical";
}
String getAffinityString(BaseType_t affinity) {
  if (affinity == tskNO_AFFINITY) {
    return "ANY";
  }
  return String(affinity);
}
// Safe affinity getter across single/dual core targets
static inline BaseType_t getSafeAffinity(TaskHandle_t handle) {
#if CONFIG_FREERTOS_UNICORE
  (void)handle;
  return 0; // single-core: treat as core 0
#else
  if (handle == nullptr) return tskNO_AFFINITY;
  return xTaskGetAffinity(handle);
#endif
}
// ============================================================================
// PER-CORE Task Monitoring - The Core Algorithm
// ============================================================================
void updateTaskMonitoring() {
  if (millis() - lastTaskSample < 500) return;
  lastTaskSample = millis();
  
  uint64_t sampleStartUs = esp_timer_get_time();
  TaskStatus_t statusArray[MAX_TASKS_MONITORED];
  UBaseType_t numTasks = uxTaskGetSystemState(statusArray, MAX_TASKS_MONITORED, NULL);
  
  if (numTasks == 0) return;
  // Reset per-core counters
  for (int c = 0; c < NUM_CORES; c++) {
    coreRuntime[c].totalRuntime100ms = 0;
    coreRuntime[c].taskCount = 0;
    coreRuntime[c].cpuPercentTotal = 0;
  }
  noAffinityRuntime100ms = 0;
  // First pass: Calculate per-core totals using xTaskGetAffinity()
  for (uint8_t j = 0; j < numTasks; j++) {
    BaseType_t affinity = getSafeAffinity(statusArray[j].xHandle);
    uint32_t runtime = statusArray[j].ulRunTimeCounter;
    
    if (affinity == tskNO_AFFINITY) {
      // Task can migrate between cores
      noAffinityRuntime100ms += (uint64_t)runtime;
    } else if (affinity >= 0 && affinity < NUM_CORES) {
      // Task pinned to specific core
      coreRuntime[affinity].totalRuntime100ms += (uint64_t)runtime;
      coreRuntime[affinity].taskCount++;
    }
  }
  // Calculate per-core deltas
  uint64_t coreDelta[2] = { 0, 0 };
  for (int c = 0; c < NUM_CORES; c++) {
    coreDelta[c] = coreRuntime[c].totalRuntime100ms - coreRuntime[c].prevTotalRuntime100ms;
    if (coreDelta[c] == 0) coreDelta[c] = 1; // Avoid division by zero
    
                    
  }
  uint64_t noAffinityDelta = noAffinityRuntime100ms - prevNoAffinityRuntime100ms;
  
  // First time init
  if (!statsInitialized) {
    taskCount = (numTasks > MAX_TASKS_MONITORED) ? MAX_TASKS_MONITORED : numTasks;
    for (uint8_t i = 0; i < taskCount; i++) {
      taskData[i].name = String(statusArray[i].pcTaskName);
      taskData[i].priority = statusArray[i].uxCurrentPriority;
      taskData[i].state = statusArray[i].eCurrentState;
      taskData[i].runtime = statusArray[i].ulRunTimeCounter;
      taskData[i].prevRuntime = statusArray[i].ulRunTimeCounter;
      taskData[i].runtimeAccumUs = (uint64_t)statusArray[i].ulRunTimeCounter;
      taskData[i].stackHighWater = statusArray[i].xHandle ? uxTaskGetStackHighWaterMark(statusArray[i].xHandle) : 0;
      taskData[i].stackHealth = getStackHealth(taskData[i].stackHighWater);
      taskData[i].cpuPercent = 0;
      taskData[i].handle = statusArray[i].xHandle;
      taskData[i].coreAffinity = getSafeAffinity(statusArray[i].xHandle);
      
      // No seeding of core accumulators at init; accumulate only deltas to avoid double counting
    }
    for (int c = 0; c < NUM_CORES; c++) {
      coreRuntime[c].prevTotalRuntime100ms = coreRuntime[c].totalRuntime100ms;
    }
    prevNoAffinityRuntime100ms = noAffinityRuntime100ms;
    statsInitialized = true;
    return;
  }
  // Update existing tasks with PER-CORE CPU calculation
  uint64_t sumTaskDeltaUs = 0; // sum of all task deltas in this window
  for (uint8_t i = 0; i < taskCount; i++) {
    bool found = false;
    for (uint8_t j = 0; j < numTasks; j++) {
      if (taskData[i].name.equals(statusArray[j].pcTaskName)) {
        uint32_t currentRuntime100ms = statusArray[j].ulRunTimeCounter;
        uint64_t taskDelta100ms = (uint64_t)currentRuntime100ms - taskData[i].prevRuntime;
        if (currentRuntime100ms < taskData[i].prevRuntime) {
          taskDelta100ms += (1ULL << 32); // Handle single wrap
        }
        sumTaskDeltaUs += taskDelta100ms;
        // accumulate wrap-free per-task runtime in microseconds
        taskData[i].runtimeAccumUs += taskDelta100ms;
        // Get task affinity
        BaseType_t affinity = getSafeAffinity(statusArray[j].xHandle);
        taskData[i].coreAffinity = affinity;
        // accumulate per-core totals (wrap-free)
        if (affinity == tskNO_AFFINITY) {
          noAffinityAccumUs += taskDelta100ms;
        } else if (affinity >= 0 && affinity < NUM_CORES) {
          coreAccumUs[affinity] += taskDelta100ms;
        }
        // Calculate CPU% based on core affinity
        if (affinity == tskNO_AFFINITY) {
          // Task can run on any core - calculate against total of all cores
          uint64_t totalCoreDelta = 0;
          for (int c = 0; c < NUM_CORES; c++) {
            totalCoreDelta += coreDelta[c];
          }
          if (totalCoreDelta > 0) {
            uint64_t percentage = (taskDelta100ms * 100ULL) / totalCoreDelta;
            taskData[i].cpuPercent = (percentage > 100) ? 100 : (uint8_t)percentage;
            
          } else {
            taskData[i].cpuPercent = 0;
            
          }
        } else if (affinity >= 0 && affinity < NUM_CORES) {
          // Task pinned to specific core - calculate against that core only
          if (coreDelta[affinity] > 0) {
            uint64_t percentage = (taskDelta100ms * 100ULL) / coreDelta[affinity];
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
  // Add new tasks
  for (uint8_t j = 0; j < numTasks && taskCount < MAX_TASKS_MONITORED; j++) {
    bool exists = false;
    for (uint8_t i = 0; i < taskCount; i++) {
      if (taskData[i].name.equals(statusArray[j].pcTaskName)) {
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
      taskData[taskCount].runtimeAccumUs = (uint64_t)statusArray[j].ulRunTimeCounter;
      taskData[taskCount].stackHighWater = statusArray[j].xHandle ? uxTaskGetStackHighWaterMark(statusArray[j].xHandle) : 0;
      taskData[taskCount].stackHealth = getStackHealth(taskData[taskCount].stackHighWater);
      taskData[taskCount].cpuPercent = 0;
      taskData[taskCount].handle = statusArray[j].xHandle;
      taskData[taskCount].coreAffinity = getSafeAffinity(statusArray[j].xHandle);
      taskCount++;
      
      // No seeding at add; we accumulate only per-sample deltas
    }
  }
  // Store totals for next sample
  for (int c = 0; c < NUM_CORES; c++) {
    coreRuntime[c].prevTotalRuntime100ms = coreRuntime[c].totalRuntime100ms;
    
  }
  prevNoAffinityRuntime100ms = noAffinityRuntime100ms;
  

  // ISR/overhead: anything not accounted to tasks in this window
  if (lastSampleWallUs != 0) {
    uint64_t wallDeltaUsAllCores = (sampleStartUs - lastSampleWallUs) * (uint64_t)NUM_CORES;
    uint64_t overheadDeltaUs = (wallDeltaUsAllCores > sumTaskDeltaUs) ? (wallDeltaUsAllCores - sumTaskDeltaUs) : 0;
    isrOverheadAccumUs += overheadDeltaUs;
    
  }
  lastSampleWallUs = sampleStartUs;
}
// ============================================================================
// Runtime Drift Diagnostics
// ============================================================================
static uint64_t lastDriftCheckUs = 0;
static uint64_t driftAccumUs = 0;
void diagRuntimeDrift() {
  const uint64_t tNow = micros64();
  if (lastDriftCheckUs == 0) {
    lastDriftCheckUs = tNow;
    return;
  }
  const uint64_t deltaUs = tNow - lastDriftCheckUs;
  lastDriftCheckUs = tNow;
  driftAccumUs += deltaUs;
  
}

// ============================================================================
// Focused Runtime Metrics Serial Output
// ============================================================================
static uint32_t lastRuntimePrintMs = 0;
void printRuntimeMetrics() {
  uint32_t now = millis();
  if (now - lastRuntimePrintMs < 5000) return; // print every 5s
  lastRuntimePrintMs = now;

  uint64_t uptime_ms = esp_timer_get_time() / 1000ULL;

  // Use cumulative idle-based effective runtime derived in systemTask
  uint64_t total_cpu_time_ms = idleEffectiveMsAccum;
  uint64_t effective_runtime_ms = (NUM_CORES > 0) ? (idleEffectiveMsAccum / NUM_CORES) : 0;
  int64_t diff_ms = (int64_t)uptime_ms - (int64_t)effective_runtime_ms;
  float diff_pct = (uptime_ms > 0) ? ((float)diff_ms / (float)uptime_ms) * 100.0f : 0.0f;

  

  for (int c = 0; c < NUM_CORES; c++) {
    
  }
}
// ============================================================================
// API endpoints
// ============================================================================
static void handleApiStatus() {
  DynamicJsonDocument doc(1536);
  doc["ble"] = bleDeviceConnected;
  bool connected = (WiFi.status() == WL_CONNECTED);
  doc["connected"] = connected;
  if (connected) {
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    JsonObject net = doc.createNestedObject("net");
    net["ssid"] = WiFi.SSID();
    net["dhcp"] = netConfig.useDHCP;
    if (!netConfig.useDHCP) {
      net["static_ip"] = netConfig.staticIP.toString();
      net["gateway"] = netConfig.gateway.toString();
      net["subnet"] = netConfig.subnet.toString();
      net["dns"] = netConfig.dns.toString();
    }
  }
  doc["uptime_ms"] = millis();
  doc["heap_free"] = ESP.getFreeHeap();
  doc["heap_total"] = ESP.getHeapSize();
  doc["heap_min"] = ESP.getMinFreeHeap();
  doc["heap_max_alloc"] = ESP.getMaxAllocHeap();
  doc["core0_load"] = coreLoadPct[0];
  doc["core1_load"] = coreLoadPct[1];
  doc["chip_model"] = ESP.getChipModel();
  doc["cpu_freq"] = ESP.getCpuFreqMHz();
  doc["flash_size"] = ESP.getFlashChipSize() / (1024 * 1024);
  doc["num_cores"] = NUM_CORES;
  float t = getInternalTemperatureC();
  if (!isnan(t)) doc["temp_c"] = t;
  else doc["temp_c"] = nullptr;
  JsonObject biz = doc.createNestedObject("biz");
  biz["running"] = (gBizState == BIZ_RUNNING);
  if (execQ) biz["queue"] = uxQueueMessagesWaiting(execQ);
  biz["processed"] = bizProcessed;
  JsonObject cores = doc.createNestedObject("cores");
  for (int c = 0; c < NUM_CORES; c++) {
    JsonObject core = cores.createNestedObject(String(c));
    core["tasks"] = coreRuntime[c].taskCount;
    core["load_pct"] = coreLoadPct[c];
    core["cpu_total"] = coreRuntime[c].cpuPercentTotal;
  }
  String output;
  output.reserve(1024);
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}
void sendDebugInfo() {
  DynamicJsonDocument doc(4096);
  uint64_t uptime_ms = esp_timer_get_time() / 1000ULL;
  uint64_t esp_timer_us = esp_timer_get_time();
  
  doc["uptime_ms"] = uptime_ms;
  doc["esp_timer_us"] = esp_timer_us;
  doc["heap_free"] = ESP.getFreeHeap();
  doc["connected"] = (WiFi.status() == WL_CONNECTED);
  float t = getInternalTemperatureC();
  if (!isnan(t)) doc["temp_c"] = t;
  else doc["temp_c"] = nullptr;
  // Simplified totals based on idle-derived load
  // Use cumulative idle-based effective runtime derived in systemTask
  uint64_t total_cpu_time_ms = idleEffectiveMsAccum;
  uint64_t effective_runtime_ms = (NUM_CORES > 0) ? (idleEffectiveMsAccum / NUM_CORES) : 0;
  doc["runtime_counter"] = nullptr;
  doc["runtime_resolution"] = "derived_from_idle";
  doc["total_cpu_time_ms"] = total_cpu_time_ms;
  doc["effective_runtime_ms"] = effective_runtime_ms;
  
  doc["isr_overhead_us"] = nullptr; // deprecated in idle-based effective runtime
  doc["isr_overhead_percent"] = nullptr;
  doc["num_tasks"] = taskCount;
  JsonObject timing = doc.createNestedObject("timing_analysis");
  int64_t diff_ms = (int64_t)uptime_ms - (int64_t)effective_runtime_ms;
  float diff_pct = (uptime_ms > 0) ? ((float)diff_ms / uptime_ms) * 100.0f : 0;
  timing["uptime_vs_effective_diff_ms"] = diff_ms;
  timing["difference_percent"] = diff_pct;
  timing["status"] = (abs(diff_pct) < 5.0f) ? "GOOD" : "ISSUE";
  
  JsonObject cpu = doc.createNestedObject("cpu_load");
  cpu["core0_percent"] = coreLoadPct[0];
  cpu["core1_percent"] = coreLoadPct[1];
  JsonObject coresDetail = doc.createNestedObject("cores_detail");
  for (int c = 0; c < NUM_CORES; c++) {
    JsonObject core = coresDetail.createNestedObject(String(c));
    core["tasks"] = coreRuntime[c].taskCount;
    core["runtime_ticks"] = (uint64_t)coreAccumUs[c];
    core["runtime_ms"] = (double)coreAccumUs[c] / 1000.0;
    core["cpu_total_pct"] = coreRuntime[c].cpuPercentTotal;
    core["idle_load"] = coreLoadPct[c];
  }
  doc["no_affinity_runtime_us"] = (uint64_t)noAffinityAccumUs;
  JsonArray tasks = doc.createNestedArray("top_tasks");
  for (uint8_t i = 0; i < taskCount; i++) {
    JsonObject tt = tasks.createNestedObject();
    tt["name"] = taskData[i].name;
    tt["runtime_ticks"] = taskData[i].runtime;
    double task_ms = (double)taskData[i].runtime / 1000.0;
    tt["runtime_ms"] = task_ms;
    tt["runtime_seconds"] = task_ms / 1000.0;
    tt["percent_of_core"] = taskData[i].cpuPercent;
    tt["state"] = getTaskStateName(taskData[i].state);
    tt["priority"] = taskData[i].priority;
    tt["stack_hwm"] = taskData[i].stackHighWater;
    tt["core_affinity"] = getAffinityString(taskData[i].coreAffinity);
  }
  String output;
  output.reserve(5120);
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}
void sendTaskMonitoring() {
  DynamicJsonDocument doc(5120);
  JsonArray arr = doc.createNestedArray("tasks");
  for (uint8_t i = 0; i < taskCount; i++) {
    JsonObject t = arr.createNestedObject();
    t["name"] = taskData[i].name;
    t["priority"] = taskData[i].priority;
    t["state"] = getTaskStateName(taskData[i].state);
    t["runtime"] = (uint64_t)taskData[i].runtimeAccumUs / 1000000ULL; // seconds (wrap-free)
    t["stack_hwm"] = taskData[i].stackHighWater;
    t["stack_health"] = taskData[i].stackHealth;
    t["cpu_percent"] = taskData[i].cpuPercent;
    t["core"] = getAffinityString(taskData[i].coreAffinity);
  }
  doc["task_count"] = taskCount;
  doc["uptime_ms"] = millis();
  JsonObject coreSummary = doc.createNestedObject("core_summary");
  for (int c = 0; c < NUM_CORES; c++) {
    JsonObject core = coreSummary.createNestedObject(String(c));
    core["tasks"] = coreRuntime[c].taskCount;
    core["cpu_total"] = coreRuntime[c].cpuPercentTotal;
    core["load"] = coreLoadPct[c];
  }
  String out;
  out.reserve(5120);
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}
// ============================================================================
// REST handlers
// ============================================================================
void hNetworkConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"err\":\"no body\"}");
    return;
  }
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"err\":\"bad json\"}");
    return;
  }
  const char* ssid = doc["ssid"] | "";
  const char* pass = doc["pass"] | "";
  bool dhcp = doc["dhcp"] | true;
  if (strlen(ssid) == 0) {
    server.send(400, "application/json", "{\"err\":\"ssid required\"}");
    return;
  }
  if (strlen(ssid) > 0) {
    prefs.putString("wifi_ssid", ssid);
    if (strlen(pass) > 0) {
      prefs.putString("wifi_pass", pass);
    }
  }
  netConfig.useDHCP = dhcp;
  if (!dhcp) {
    if (doc.containsKey("static_ip")) netConfig.staticIP = parseIP(String(doc["static_ip"].as<const char*>()));
    if (doc.containsKey("gateway")) netConfig.gateway = parseIP(String(doc["gateway"].as<const char*>()));
    if (doc.containsKey("subnet")) netConfig.subnet = parseIP(String(doc["subnet"].as<const char*>()));
    if (doc.containsKey("dns")) netConfig.dns = parseIP(String(doc["dns"].as<const char*>()));
    saveNetworkConfig();
  }
  wifiManualDisconnect = false;
  wifiReconnectAttempts = 0;
  wifiState = WIFI_STATE_IDLE;
  StaticJsonDocument<128> resp;
  resp["msg"] = "Saved, reconnecting...";
  String out;
  serializeJson(resp, out);
  server.send(200, "application/json", out);
}
void hExec() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"err\":\"no body\"}");
    return;
  }
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"err\":\"parse error\"}");
    return;
  }
  const char* cmd = doc["cmd"] | "";
  size_t cmdLen = strlen(cmd);
  if (cmdLen == 0 || cmdLen >= MAX_MSG_SIZE) {
    server.send(400, "application/json", "{\"err\":\"invalid cmd\"}");
    return;
  }
  ExecMessage* msg = allocMessage();
  if (!msg) {
    server.send(503, "application/json", "{\"err\":\"pool full\"}");
    return;
  }
  memcpy(msg->payload, cmd, cmdLen);
  msg->payload[cmdLen] = '\0';
  msg->length = cmdLen;
  if (execQ && xQueueSend(execQ, &msg, 0) == pdTRUE) {
    server.send(200, "application/json", "{\"msg\":\"queued\"}");
  } else {
    freeMessage(msg);
    server.send(503, "application/json", "{\"err\":\"queue send failed\"}");
  }
}
void hBizStart() {
  gBizState = BIZ_RUNNING;
  server.send(200, "application/json", "{\"msg\":\"started\"}");
}
void hBizStop() {
  gBizState = BIZ_STOPPED;
  server.send(200, "application/json", "{\"msg\":\"stopped\"}");
}
// ============================================================================
// HTML Dashboard - COPY/PASTE THIS ENTIRE SECTION INTO YOUR .INO FILE
// Replace the INDEX_HTML_DASHBOARD placeholder with these constants
// ============================================================================
// Main HTML - Always included
static const char INDEX_HTML_PART1[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32(x) Per-Core Monitor</title>
<style>
* {box-sizing:border-box;margin:0;padding:0}
body {font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;background:#0d1117;color:#c9d1d9;min-height:100vh;padding:8px}
.container {max-width:1400px;margin:0 auto}
h1 {text-align:center;margin-bottom:12px;font-size:1.8em;background:linear-gradient(90deg,#58a6ff,#bc8cff);-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text}
.card {background:#161b22;border:1px solid #30363d;border-radius:8px;padding:12px;margin-bottom:10px;box-shadow:0 4px 8px rgba(0,0,0,0.4)}
.card h3 {margin-bottom:8px;color:#58a6ff;font-size:1.1em;border-bottom:1px solid #21262d;padding-bottom:6px}
h4 {color:#58a6ff;font-size:1em;margin-bottom:6px}
.row {display:flex;gap:6px;flex-wrap:wrap;align-items:center;margin-bottom:6px}
.pill {background:#21262d;padding:4px 10px;border-radius:12px;font-size:0.8em;white-space:nowrap;border:1px solid #30363d}
.core-badge {padding:3px 8px;border-radius:10px;font-size:0.75em;font-weight:600}
.core-0 {background:#1f6feb;color:#fff}
.core-1 {background:#9e6a03;color:#fff}
.core-any {background:#6e7681;color:#fff}
.status-dot {display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:6px}
.status-dot.on {background:#3fb950;box-shadow:0 0 6px #3fb950}
.status-dot.off {background:#6e7681}
input[type="text"],input[type="password"],textarea {width:100%;padding:8px;border:1px solid #30363d;border-radius:4px;font-size:0.9em;margin-bottom:6px;background:#0d1117;color:#c9d1d9}
textarea {resize:vertical;font-family:monospace}
button,.btn {padding:8px 16px;border:none;border-radius:4px;font-size:0.9em;cursor:pointer;transition:all 0.3s;font-weight:500}
.btn.primary {background:#238636;color:#fff}
.btn.primary:hover {background:#2ea043}
.btn.secondary {background:#21262d;color:#c9d1d9;border:1px solid #30363d}
.btn.danger {background:#da3633;color:#fff}
.small {font-size:0.75em;color:#8b949e;font-weight:normal}
pre {background:#0d1117;color:#c9d1d9;padding:10px;border-radius:4px;overflow-x:auto;font-size:0.8em;border:1px solid #30363d}
.form-group {margin-bottom:8px}
.form-group label {display:block;margin-bottom:3px;color:#8b949e;font-size:0.85em}
.grid {display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:10px}
.task-table {width:100%;border-collapse:collapse;margin-top:6px;font-size:0.85em}
.task-table th {background:#21262d;color:#c9d1d9;padding:6px 8px;text-align:left;font-size:0.85em;border:1px solid #30363d}
.task-table td {padding:5px 8px;border:1px solid #30363d;font-size:0.8em}
.task-table tr:hover {background:#161b22}
.task-name {font-weight:bold;font-family:monospace;color:#58a6ff;font-size:0.8em}
.task-state {padding:3px 6px;border-radius:3px;font-size:0.75em;font-weight:bold}
.state-running {background:#238636;color:#fff}
.state-ready {background:#1f6feb;color:#fff}
.state-blocked {background:#9e6a03;color:#fff}
.health-good {color:#3fb950;font-weight:bold}
.health-ok {color:#58a6ff;font-weight:bold}
.health-low {color:#d29922;font-weight:bold}
.health-critical {color:#f85149;font-weight:bold}
.progress-bar {width:100%;height:6px;background:#21262d;border-radius:3px;overflow:hidden;border:1px solid #30363d}
.progress-fill {height:100%;background:linear-gradient(90deg,#238636,#3fb950);transition:width 0.3s}
.cpu-badge {display:inline-block;padding:2px 5px;border-radius:3px;font-size:0.7em;font-weight:bold}
.cpu-low {background:#238636;color:#fff}
.cpu-med {background:#d29922;color:#000}
.cpu-high {background:#da3633;color:#fff}
.alert {padding:8px;border-radius:4px;margin-bottom:8px;border-left:3px solid;font-size:0.85em}
.alert.success {background:#0d1117;border-color:#238636;color:#3fb950}
.alert.error {background:#0d1117;border-color:#da3633;color:#f85149}
.alert.info {background:#0d1117;border-color:#1f6feb;color:#58a6ff}
.debug-status {padding:4px 8px;border-radius:4px;font-size:0.8em;font-weight:bold}
.debug-good {background:#238636;color:#fff}
.debug-issue {background:#da3633;color:#fff}
</style>
</head>
<body>
<div class="container">
<h1>ESP32(x) Per-Core Monitor</h1>
<div class="card">
 <h3>System Status <span class="small">Per-Core Isolation Active</span></h3>
 <div class="row">
  <div><span class="status-dot off" id="bleDot"></span><span>BLE</span></div>
  <div><span class="status-dot off" id="wifiDot"></span><span>WiFi</span></div>
  <div class="pill" id="ipInfo">IP: -</div>
  <div class="pill" id="rssiInfo">RSSI: --</div>
  <div class="pill">Up: <span id="upt">--:--:--</span></div>
  <div class="pill">Heap: <span id="heap">--</span></div>
  <div class="pill">Temp: <span id="tempC">--</span></div>
 </div>
 <div class="row">
  <div class="pill"><span class="core-badge core-0">C0</span> <span id="c0">-</span>% (<span id="c0Tasks">-</span> tasks)</div>
  <div class="pill" id="c1pill"><span class="core-badge core-1">C1</span> <span id="c1">-</span>% (<span id="c1Tasks">-</span> tasks)</div>
  <div class="pill">Total: <span id="taskCount">-</span></div>
  <div class="pill"><span id="chip">-</span> @ <span id="cpuFreq">-</span>MHz</div>
 </div>
</div>
<div class="grid">
 <div class="card">
  <h3>Business Module</h3>
  <div id="bizStatus" class="alert info">Status: <span id="bizState">Loading...</span></div>
  <div class="row">
   <button class="btn primary" onclick="startBiz()">Start</button>
   <button class="btn danger" onclick="stopBiz()">Stop</button>
   <div class="pill">Q:<span id="bizQueue">-</span> | Proc:<span id="bizProcessed">-</span></div>
  </div>
 </div>
 <div class="card">
  <h3>Command Exec</h3>
  <div class="form-group">
   <input type="text" id="execCmd" placeholder="Enter command...">
  </div>
  <button class="btn primary" onclick="execCommand()">Execute</button>
  <div id="execResult"></div>
 </div>
</div>
<div class="card">
 <h3>WiFi Config</h3>
 <div class="grid">
  <div>
   <div class="form-group">
    <label>SSID</label>
    <input type="text" id="wifiSsid" placeholder="Network name">
   </div>
   <div class="form-group">
    <label>Password</label>
    <input type="password" id="wifiPass" placeholder="Password">
   </div>
   <div class="form-group">
    <label><input type="checkbox" id="wifiDhcp" checked> DHCP</label>
   </div>
  </div>
  <div id="staticIpFields" style="display:none">
   <div class="form-group">
    <label>Static IP</label>
    <input type="text" id="staticIp" placeholder="192.168.1.100">
   </div>
   <div class="form-group">
    <label>Gateway</label>
    <input type="text" id="gateway" placeholder="192.168.1.1">
   </div>
  </div>
 </div>
 <button class="btn primary" onclick="saveNetwork()">Save & Connect</button>
 <div id="networkResult"></div>
</div>
<div class="card">
 <h3>Tasks <span class="small">Per-Core CPU% (Isolated)</span></h3>
 <div id="taskMonitor"><p style="text-align:center;color:#8b949e;padding:12px">Loading...</p></div>
</div>
)rawliteral";
// Debug Panel - Only included if DEBUG_MODE is enabled
#if DEBUG_MODE
static const char INDEX_HTML_DEBUG[] PROGMEM = R"rawliteral(
<div class="card">
 <h3>Debug Panel <span class="small">Timing & Core Analysis</span></h3>
 <div id="debugSummary" style="margin-bottom:10px"></div>
 <pre id="debugOutput" style="max-height:300px"></pre>
 <h4>Timing Drift Log</h4>
 <div class="row">
  <span class="pill">Samples: <span id="timingLogCount">0</span></span>
  <button class="btn secondary" onclick="saveTimingCSV()">Save CSV</button>
  <button class="btn secondary" onclick="clearTimingLog()">Clear</button>
 </div>
 <div style="max-height:260px;overflow:auto;margin-top:8px">
  <table class="task-table" id="timingTable">
   <thead>
    <tr>
     <th>#</th><th>Uptime (ms)</th><th>Runtime (ms)</th>
     <th>Drift (ms)</th><th>Drift Δ</th><th>Drift %</th><th>Status</th>
     <th>C0 %</th><th>C1 %</th><th>Heap</th><th>Temp</th>
     <th>Top Task</th><th>Top %</th>
     <th>Web %</th><th>Biz %</th><th>Sys %</th>
    </tr>
   </thead>
   <tbody></tbody>
  </table>
 </div>
</div>
)rawliteral";
#endif
// JavaScript - Always included
static const char INDEX_HTML_PART2[] PROGMEM = R"rawliteral(
</div>
<script>
const I=id=>document.getElementById(id);
async function api(path,method='GET',body=null){
 try{
  const opts={method};
  if(body){opts.headers={'Content-Type':'application/json'};opts.body=JSON.stringify(body);}
  const r=await fetch(path,opts);
  return await r.json();
 }catch(e){return{error:e.message}}
}
function fmU(ms){const s=Math.floor(ms/1000),h=Math.floor(s/3600),m=Math.floor((s%3600)/60),ss=s%60;return`${String(h).padStart(2,'0')}:${String(m).padStart(2,'0')}:${String(ss).padStart(2,'0')}`}
function fmB(b){if(b<1024)return`${b}B`;if(b<1048576)return`${(b/1024).toFixed(1)}KB`;return`${(b/1048576).toFixed(2)}MB`;}
function cpuCls(v){return v<30?'cpu-low':v<70?'cpu-med':'cpu-high'}
function healthCls(v){return'health-'+v}
function stCls(v){return'state-'+v.toLowerCase()}
function coreCls(c){if(c==='ANY')return'core-any';if(c==='0')return'core-0';if(c==='1')return'core-1';return'core-any';}
I('wifiDhcp').addEventListener('change',()=>{I('staticIpFields').style.display=I('wifiDhcp').checked?'none':'block';});
async function startBiz(){const r=await api('/api/biz/start','POST');if(!r.error){I('bizState').textContent='RUNNING';refresh();}}
async function stopBiz(){const r=await api('/api/biz/stop','POST');if(!r.error){I('bizState').textContent='STOPPED';refresh();}}
async function execCommand(){
 const cmd=I('execCmd').value.trim();
 if(!cmd){alert('Enter a command');return;}
 const r=await api('/api/exec','POST',{cmd});
 const res=I('execResult');
 if(r.error||r.err){res.innerHTML='<div class="alert error">'+(r.error||r.err)+'</div>';}
 else{res.innerHTML='<div class="alert success">'+r.msg+'</div>';I('execCmd').value='';}
 setTimeout(()=>res.innerHTML='',3000);
}
async function saveNetwork(){
 const ssid=I('wifiSsid').value.trim();
 const pass=I('wifiPass').value;
 const dhcp=I('wifiDhcp').checked;
 if(!ssid){alert('Enter SSID');return;}
 const body={ssid,pass,dhcp};
 if(!dhcp){body.static_ip=I('staticIp').value;body.gateway=I('gateway').value;}
 const r=await api('/api/network','POST',body);
 const res=I('networkResult');
 if(r.error||r.err){res.innerHTML='<div class="alert error">'+(r.error||r.err)+'</div>';}
 else{res.innerHTML='<div class="alert success">'+r.msg+'</div>';}
 setTimeout(()=>res.innerHTML='',5000);
}
async function refreshTasks(){
 const j=await api('/api/tasks');
 if(j.error)return;
 if(!j.tasks){I('taskMonitor').innerHTML='<p style="text-align:center;color:#8b949e">No tasks</p>';return;}
 j.tasks.sort((a,b)=>(b.cpu_percent||0)-(a.cpu_percent||0));
 let h='<table class="task-table"><thead><tr><th>Task</th><th>Core</th><th>Pri</th><th>Stack</th><th>State</th><th>CPU%</th><th>Runtime</th></tr></thead><tbody>';
 j.tasks.forEach(t=>{
  const cpuPct = Math.min(100, t.cpu_percent || 0);
  h+=`<tr>
   <td class="task-name">${t.name}</td>
   <td><span class="core-badge ${coreCls(t.core)}">${t.core}</span></td>
   <td>${t.priority}</td>
   <td><span class="${healthCls(t.stack_health)}">${t.stack_hwm}</span></td>
   <td><span class="task-state ${stCls(t.state)}">${t.state}</span></td>
   <td><span class="cpu-badge ${cpuCls(cpuPct)}">${cpuPct.toFixed(1)}%</span>
     <div class="progress-bar"><div class="progress-fill" style="width:${cpuPct}%"></div></div></td>
   <td>${fmU((t.runtime||0)*1000)}</td>
  </tr>`;
 });
 h+='</tbody></table>';
 I('taskMonitor').innerHTML=h;
 I('taskCount').textContent=j.task_count||'-';
 if(j.core_summary){
  if(j.core_summary['0']){
   I('c0Tasks').textContent=j.core_summary['0'].tasks||'0';
  }
  if(j.core_summary['1']){
   I('c1Tasks').textContent=j.core_summary['1'].tasks||'0';
  }
 }
}
async function refresh(){
 const j=await api('/api/status');
 if(j.error)return;
 I('bleDot').classList.toggle('on',!!j.ble);
 I('wifiDot').classList.toggle('on',!!j.connected);
 I('ipInfo').textContent=`IP: ${j.ip||'-'}`;
 I('rssiInfo').textContent=`RSSI: ${j.rssi||'--'}`;
 I('upt').textContent=fmU(j.uptime_ms||0);
 I('heap').textContent=fmB(j.heap_free||0);
 I('c0').textContent=j.core0_load??'-';
 I('c1').textContent=j.core1_load??'-';
 I('chip').textContent=j.chip_model||'-';
 I('cpuFreq').textContent=j.cpu_freq||'-';
 if(j.temp_c===null||j.temp_c===undefined){I('tempC').textContent='n/a';}
 else{I('tempC').textContent=j.temp_c.toFixed?j.temp_c.toFixed(1)+'°C':j.temp_c+'°C';}
 if(j.num_cores===1){I('c1pill').style.display='none';}else{I('c1pill').style.display='inline-block';}
 if(j.biz){
  I('bizState').textContent=j.biz.running?'RUNNING':'STOPPED';
  I('bizQueue').textContent=j.biz.queue??'-';
  I('bizProcessed').textContent=j.biz.processed??'-';
 }
}
)rawliteral";
// Debug JavaScript - Only included if DEBUG_MODE is enabled
#if DEBUG_MODE
static const char INDEX_HTML_DEBUG_JS[] PROGMEM = R"rawliteral(
let timingLog=[];
const MAX_TIMING_ENTRIES=500;
function addTimingLogEntry(j) {
 if (!j.timing_analysis) return;
 const t = j.timing_analysis;
 const uptime = j.uptime_ms;
 const runtime = j.effective_runtime_ms;
 const drift = t.uptime_vs_effective_diff_ms;
 const diffPct = t.difference_percent;
 const status = t.status || 'N/A';
 const core0 = j.cpu_load ? j.cpu_load.core0_percent : '-';
 const core1 = j.cpu_load ? j.cpu_load.core1_percent : '-';
 const temp = (j.temp_c && !isNaN(j.temp_c)) ? j.temp_c.toFixed(1) : '-';
 const heap = j.heap_free ?? 0;
 let top='-', topPct='-', webPct='-', bizPct='-', sysPct='-';
 if (Array.isArray(j.top_tasks)) {
  j.top_tasks.sort((a,b) => b.percent_of_core - a.percent_of_core);
  const topTask = j.top_tasks[0];
  if(topTask){
   top = topTask.name;
   topPct = (topTask.percent_of_core || 0).toFixed(1);
  }
  j.top_tasks.forEach(tt=>{
   const name=tt.name.toLowerCase();
   if(name.includes('web')) webPct=tt.percent_of_core.toFixed(1);
   if(name.includes('biz')) bizPct=tt.percent_of_core.toFixed(1);
   if(name.includes('sys')) sysPct=tt.percent_of_core.toFixed(1);
  });
 }
 let prevDrift = timingLog.length>0 ? timingLog[0].drift : drift;
 const driftDelta = drift - prevDrift;
 const entry = {uptime,runtime,drift,driftDelta,diffPct,status,core0,core1,temp,heap,
        top,topPct,webPct,bizPct,sysPct};
 timingLog.unshift(entry);
 if(timingLog.length>MAX_TIMING_ENTRIES) timingLog.pop();
 const tbody = I('timingTable').querySelector('tbody');
 const row=document.createElement('tr');
 const cls=status==='GOOD'?'health-good':(status==='ISSUE'?'health-critical':'');
 const driftCls=Math.abs(driftDelta)>50?'health-low':'';
 row.innerHTML=`
  <td>${timingLog.length}</td>
  <td>${uptime}</td>
  <td>${runtime}</td>
  <td>${drift}</td>
  <td class="${driftCls}">${driftDelta>0?'+':''}${driftDelta}</td>
  <td>${diffPct.toFixed(2)}</td>
  <td class="${cls}">${status}</td>
  <td>${core0}</td>
  <td>${core1}</td>
  <td>${heap}</td>
  <td>${temp}</td>
  <td>${top}</td>
  <td>${topPct}%</td>
  <td>${webPct}%</td>
  <td>${bizPct}%</td>
  <td>${sysPct}%</td>`;
 tbody.insertBefore(row, tbody.firstChild);
 if(tbody.rows.length>MAX_TIMING_ENTRIES) tbody.deleteRow(-1);
 I('timingLogCount').textContent=timingLog.length;
}
function saveTimingCSV(){
 if(!timingLog.length){alert('No timing data');return;}
 const header='index,uptime_ms,effective_runtime_ms,drift_ms,drift_delta_ms,drift_percent,status,core0,core1,temp_c,heap_free,top_task,top_percent,web_percent,biz_percent,sys_percent\n';
 const csv=header+timingLog.map((e,i)=>
  `${i+1},${e.uptime},${e.runtime},${e.drift},${e.driftDelta},${e.diffPct.toFixed(3)},${e.status},${e.core0},${e.core1},${e.temp},${e.heap},${e.top},${e.topPct},${e.webPct},${e.bizPct},${e.sysPct}`).join('\n');
 const blob=new Blob([csv],{type:'text/csv'});
 const url=URL.createObjectURL(blob);
 const a=document.createElement('a');
 a.href=url;a.download='timing_log.csv';a.click();
 setTimeout(()=>URL.revokeObjectURL(url),1000);
}
function clearTimingLog(){
 if(confirm('Clear all timing data?')){
  timingLog=[];
  I('timingTable').querySelector('tbody').innerHTML='';
  I('timingLogCount').textContent='0';
 }
}
async function refreshDebug(){
 const j=await api('/api/debug');
 if(!j||j.error){I('debugOutput').textContent='Error: '+(j?j.error:'no data');return;}
 let summary=`<span class="pill">Uptime: ${fmU(j.uptime_ms)}</span>`;
 summary+=`<span class="pill">Effective Runtime: ${fmU(j.effective_runtime_ms)}</span>`;
 if(j.timing_analysis){
  const cls=j.timing_analysis.status==='GOOD'?'debug-good':'debug-issue';
  summary+=` <span class="debug-status ${cls}">${j.timing_analysis.status}: ${j.timing_analysis.difference_percent.toFixed(2)}%</span>`;
 }
 I('debugSummary').innerHTML=summary;
 let out='=== TIMING ANALYSIS ===\n';
 out+=`Uptime: ${j.uptime_ms} ms (${fmU(j.uptime_ms)})\n`;
 out+=`Total CPU Time: ${j.total_cpu_time_ms} ms\n`;
 out+=`Effective Runtime: ${j.effective_runtime_ms} ms\n`;
 if(j.timing_analysis){
  out+=`Drift: ${j.timing_analysis.uptime_vs_effective_diff_ms} ms (${j.timing_analysis.difference_percent.toFixed(3)}%)\n`;
  out+=`Status: ${j.timing_analysis.status}\n`;
 }
 if(j.cores_detail){
  out+='\n=== PER-CORE BREAKDOWN ===\n';
  for(let c in j.cores_detail){
   const core=j.cores_detail[c];
   out+=`Core ${c}: ${core.tasks} tasks, ${core.cpu_total_pct}% total, ${core.idle_load}% idle-based\n`;
  }
 }
 if(j.cpu_load){
  out+=`\n=== CPU LOAD (Idle-based) ===\n`;
  out+=`Core 0: ${j.cpu_load.core0_percent}%\n`;
  out+=`Core 1: ${j.cpu_load.core1_percent}%\n`;
 }
 if(j.top_tasks){
  out+='\n=== TOP TASKS (by CPU%) ===\n';
  j.top_tasks.sort((a,b) => b.percent_of_core - a.percent_of_core);
  const top_8 = j.top_tasks.slice(0, 8);
  top_8.forEach(t=>{
   out+=` ${t.name.padEnd(12)} Core ${t.core_affinity} | ${t.percent_of_core.toFixed(1)}% | ${t.runtime_ms.toFixed(0)}ms | ${t.state}\n`;
  });
 }
 I('debugOutput').textContent=out;
 addTimingLogEntry(j);
}
setInterval(refreshDebug, 3000);
refreshDebug();
)rawliteral";
#endif
// Closing JavaScript and HTML - Always included
static const char INDEX_HTML_END[] PROGMEM = R"rawliteral(
setInterval(refresh,2000);
setInterval(refreshTasks,3000);
refresh();
refreshTasks();
</script>
</body>
</html>
)rawliteral";
// ============================================================================
// UPDATED sendIndex() function - Use this in your routes() function
// ============================================================================
static void sendIndex() {
  const uint64_t tStart = micros64();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  // Send main HTML
  server.sendContent_P(INDEX_HTML_PART1);
  delay(1);
#if DEBUG_MODE
  // Send debug panel if enabled
  server.sendContent_P(INDEX_HTML_DEBUG);
  delay(1);
#endif
  // Send JavaScript
  server.sendContent_P(INDEX_HTML_PART2);
  delay(1);
#if DEBUG_MODE
  // Send debug JavaScript if enabled
  server.sendContent_P(INDEX_HTML_DEBUG_JS);
  delay(1);
#endif
  // Send closing tags
  server.sendContent_P(INDEX_HTML_END);
  server.client().flush();
  delay(2);
  server.client().stop();
  const uint64_t tEnd = micros64();
}
// ============================================================================
// routes
// ============================================================================
void routes() {
  server.on("/", HTTP_GET, sendIndex);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/tasks", HTTP_GET, sendTaskMonitoring);
#if DEBUG_MODE
  server.on("/api/debug", HTTP_GET, sendDebugInfo);
#endif
  server.on("/api/network", HTTP_POST, hNetworkConfig);
  server.on("/api/exec", HTTP_POST, hExec);
  server.on("/api/biz/start", HTTP_POST, hBizStart);
  server.on("/api/biz/stop", HTTP_POST, hBizStop);
  server.onNotFound([]() {
    server.send(404, "application/json", "{\"err\":\"not found\"}");
  });
  if (WiFi.getMode() == WIFI_OFF) {
    WiFi.mode(WIFI_STA);
    delay(100);
  }
  server.begin();
}
// ============================================================================
// RTOS tasks
// ============================================================================
void systemTask(void* param) {
  esp_task_wdt_add(NULL);
  bool ledState = false;
  uint32_t ledTs = 0;
  pinMode(BLE_LED_PIN, OUTPUT);
  digitalWrite(BLE_LED_PIN, LOW);
  for (;;) {
    esp_task_wdt_reset();
    checkWiFiConnection();
    handleBLEReconnect();
    updateCpuLoad();
    monitorHeapHealth();
    checkTaskStacks();
    
    if (bleDeviceConnected) {
      uint32_t now = millis();
      if (now - ledTs > BLE_LED_BLINK_MS) {
        ledTs = now;
        ledState = !ledState;
        digitalWrite(BLE_LED_PIN, ledState ? HIGH : LOW);
      }
    } else if (ledState) {
      digitalWrite(BLE_LED_PIN, LOW);
      ledState = false;
    }
    diagRuntimeDrift();
    printRuntimeMetrics();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
void webTask(void* param) {
  esp_task_wdt_add(NULL);
  for (;;) {
    esp_task_wdt_reset();
    server.handleClient();
    updateTaskMonitoring();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
void bizTask(void* param) {
  esp_task_wdt_add(NULL);
  ExecMessage* msg = nullptr;
  for (;;) {
    esp_task_wdt_reset();
    if (gBizState == BIZ_RUNNING) {
      if (execQ && xQueueReceive(execQ, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (msg) {
          vTaskDelay(pdMS_TO_TICKS(50));
          bizProcessed++;
          freeMessage(msg);
          msg = nullptr;
        }
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}
// ============================================================================
// setup + loop
// ============================================================================
void setup() {
  
  delay(300);
  esp_log_level_set("wifi", ESP_LOG_WARN);
  esp_log_level_set("dhcpc", ESP_LOG_WARN);
  esp_log_level_set("phy_init", ESP_LOG_WARN);
  
  esp_task_wdt_deinit();
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  prefs.begin("rngds_base", false);
  loadNetworkConfig();
#if RNGDS_HAS_TEMP
  {
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    if (temperature_sensor_install(&cfg, &s_temp_sensor) == ESP_OK) {
      if (temperature_sensor_enable(s_temp_sensor) == ESP_OK) {
      }
    }
  }
#endif
  initMessagePool();
  initBLE();
  setupWiFi();
  routes();
  execQ = xQueueCreate(MSG_POOL_SIZE, sizeof(ExecMessage*));
  // Initialize per-core runtime structures
  for (int c = 0; c < NUM_CORES; c++) {
    coreRuntime[c].totalRuntime100ms = 0;
    coreRuntime[c].prevTotalRuntime100ms = 0;
    coreRuntime[c].taskCount = 0;
    coreRuntime[c].cpuPercentTotal = 0;
  }
  // Calibrate CPU load BEFORE creating tasks
  startCpuLoadMonitor();
  // Start idle-based effective runtime integrator (100 ms cadence)
  if (s_idleEffTimer == NULL) {
    const esp_timer_create_args_t args = {
      .callback = idleEffTimerCb,
      .arg = NULL,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "idle_eff"
    };
    if (esp_timer_create(&args, &s_idleEffTimer) == ESP_OK) {
      esp_timer_start_periodic(s_idleEffTimer, 100000); // 100 ms
    }
  }
#if NUM_CORES > 1
  xTaskCreatePinnedToCore(bizTask, "biz", 4096, nullptr, 1, &bizTaskHandle, 1);
  xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, &webTaskHandle, 0);
  xTaskCreatePinnedToCore(systemTask, "sys", 3072, nullptr, 2, &sysTaskHandle, 0);
  
#else
  xTaskCreate(bizTask, "biz", 4096, nullptr, 1, &bizTaskHandle);
  xTaskCreate(webTask, "web", 8192, nullptr, 1, &webTaskHandle);
  xTaskCreate(systemTask, "sys", 3072, nullptr, 2, &sysTaskHandle);
  
#endif
  minHeapEver = ESP.getFreeHeap();
  
}
void loop() {
  vTaskDelete(NULL);
}
