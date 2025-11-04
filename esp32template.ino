/*
  ESP32(x) Per-Core Monitor Template
  Features: Per-core CPU tracking, BLE control, WiFi config, Task monitoring
*/

#define DEBUG_MODE 1

// FreeRTOS runtime stats configuration
#ifndef CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
#define CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS 1
#endif
#ifndef CONFIG_FREERTOS_USE_CUSTOM_RUN_TIME_STATS_HOOKS
#define CONFIG_FREERTOS_USE_CUSTOM_RUN_TIME_STATS_HOOKS 1
#endif
#ifndef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
#define CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS 1
#endif
#ifndef CONFIG_FREERTOS_USE_TRACE_FACILITY
#define CONFIG_FREERTOS_USE_TRACE_FACILITY 1
#endif

#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS configureTimerForRunTimeStats
#define portGET_RUN_TIME_COUNTER_VALUE ulGetRunTimeCounterValue

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
#include <pgmspace.h>

// Target chip detection
#if defined(CONFIG_IDF_TARGET_ESP32)
#define ESP32_HAS_BLE 1
#define BLE_ADVERT_NAME "RNGDS_ESP32"
#define ESP32_HAS_TEMP 0
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define ESP32_HAS_BLE 1
#define BLE_ADVERT_NAME "RNGDS_ESP32S3"
#define ESP32_HAS_TEMP 1
#include "driver/temperature_sensor.h"
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#define ESP32_HAS_BLE 1
#define BLE_ADVERT_NAME "RNGDS_ESP32C3"
#define ESP32_HAS_TEMP 1
#include "driver/temperature_sensor.h"
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
#define ESP32_HAS_BLE 1
#define BLE_ADVERT_NAME "RNGDS_ESP32C6"
#define ESP32_HAS_TEMP 1
#include "driver/temperature_sensor.h"
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
#define ESP32_HAS_BLE 0
#define ESP32_HAS_TEMP 1
#include "driver/temperature_sensor.h"
#else
#define ESP32_HAS_BLE 1
#define BLE_ADVERT_NAME "RNGDS"
#define ESP32_HAS_TEMP 0
#endif

#if ESP32_HAS_BLE
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#endif

// Message pool for zero-malloc execution
#define MSG_POOL_SIZE 10
#define MAX_MSG_SIZE 256
struct ExecMessage {
  char payload[MAX_MSG_SIZE];
  uint16_t length;
  bool inUse;
};

// CPU load monitoring (declare early for use in callbacks)
volatile uint8_t coreLoadPct[2] = { 0, 0 };

// FreeRTOS runtime counter (microsecond resolution, 584k year wrap time)
static uint64_t runtimeOffsetUs = 0;

extern "C" void configureTimerForRunTimeStats(void) {
  runtimeOffsetUs = esp_timer_get_time();
}

extern "C" uint32_t ulGetRunTimeCounterValue(void) {
  return (uint32_t)(esp_timer_get_time() - runtimeOffsetUs);
}

// System configuration
#define WDT_TIMEOUT 20
#define MAX_BLE_CMD_LENGTH 256
#define STACK_CHECK_INTERVAL 60000
#define WIFI_RECONNECT_DELAY 15000
#define WIFI_CONNECT_TIMEOUT 30000

// Board-specific LED pins
#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define BLE_LED_PIN 8
#define LED_INVERTED 1  
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
#define BLE_LED_PIN 15
#define LED_INVERTED 0
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define BLE_LED_PIN 48
#define LED_INVERTED 0
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
#define BLE_LED_PIN 18
#define LED_INVERTED 0
#else
#define BLE_LED_PIN 2
#define LED_INVERTED 0
#endif
#define BLE_LED_BLINK_MS 500

// Core count
#if CONFIG_FREERTOS_UNICORE
#define NUM_CORES 1
#else
#define NUM_CORES 2
#endif

// BLE UUIDs
static const char* BLE_SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
static const char* BLE_CHAR_UUID_RX = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
static const char* BLE_CHAR_UUID_TX = "beb5483e-36e1-4688-b7f5-ea07361b26a9";

#if ESP32_HAS_BLE
NimBLEServer* pBLEServer = nullptr;
NimBLECharacteristic* pTxCharacteristic = nullptr;
#endif

bool bleDeviceConnected = false;
bool bleOldDeviceConnected = false;

// Network configuration stored in NVS
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

// Per-task monitoring data
#define MAX_TASKS_MONITORED 64
struct TaskMonitorData {
  String name;
  UBaseType_t priority;
  eTaskState state;
  uint32_t runtime;
  uint32_t prevRuntime;
  uint64_t runtimeAccumUs;
  uint32_t stackHighWater;
  uint8_t cpuPercent;
  String stackHealth;
  BaseType_t coreAffinity;
  TaskHandle_t handle;
};

TaskMonitorData taskData[MAX_TASKS_MONITORED];
uint8_t taskCount = 0;
uint32_t lastTaskSample = 0;
bool statsInitialized = false;

// Per-core runtime tracking
struct CoreRuntimeData {
  uint64_t totalRuntime100ms;
  uint64_t prevTotalRuntime100ms;
  uint8_t taskCount;
  uint8_t cpuPercentTotal;
};

CoreRuntimeData coreRuntime[2];
uint64_t noAffinityRuntime100ms = 0;

uint64_t coreAccumUs[2] = { 0, 0 };
uint64_t noAffinityAccumUs = 0;
uint32_t lastStackCheck = 0;

// Business logic
enum BizState : uint8_t { BIZ_STOPPED = 0,
                          BIZ_RUNNING = 1 };
volatile BizState gBizState = BIZ_STOPPED;
QueueHandle_t execQ = nullptr;
volatile uint32_t bizProcessed = 0;
TaskHandle_t webTaskHandle = nullptr;
TaskHandle_t bizTaskHandle = nullptr;
TaskHandle_t sysTaskHandle = nullptr;

// WiFi state machine
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

#if ESP32_HAS_TEMP
static temperature_sensor_handle_t s_temp_sensor = NULL;
#endif

ExecMessage msgPool[MSG_POOL_SIZE];
SemaphoreHandle_t poolMutex = nullptr;

// Memory information structure
struct MemoryInfo {
  uint32_t flashSizeMB;
  uint32_t psramSizeBytes;
  uint32_t psramFreeBytes;
  bool hasPsram;
};

// Forward declarations
void handleBLECommand(String cmd);
void setupWiFi();
void checkWiFiConnection();
void updateTaskMonitoring();
void sendTaskMonitoring();
void routes();
void systemTask(void* param);
void webTask(void* param);
void bizTask(void* param);
static void handleApiStatus();
static MemoryInfo getMemoryInfo();

// ============================================================================
// Message Pool (zero-malloc message handling)
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

void updateCpuLoad() {
  // This function now just copies the CPU load from the main monitor,
  // which is already calculating it correctly for the "top_tasks" list.
  // We just invert the IDLE task's percentage.

  // Find the IDLE0 task in our monitored data
  for (uint8_t i = 0; i < taskCount; i++) {
    if (taskData[i].name.equals("IDLE0") || (NUM_CORES == 1 && taskData[i].name.equals("IDLE"))) {
      coreLoadPct[0] = (taskData[i].cpuPercent > 100) ? 0 : (100 - taskData[i].cpuPercent);
    }

#if NUM_CORES > 1
    else if (taskData[i].name.equals("IDLE1")) {
      coreLoadPct[1] = (taskData[i].cpuPercent > 100) ? 0 : (100 - taskData[i].cpuPercent);
    }
#endif
  }
}

// ============================================================================
// Health Monitoring
// ============================================================================

void checkTaskStacks() {
  uint32_t now = millis();
  if (now - lastStackCheck < STACK_CHECK_INTERVAL) return;
  lastStackCheck = now;

  if (webTaskHandle) uxTaskGetStackHighWaterMark(webTaskHandle);
  if (bizTaskHandle) uxTaskGetStackHighWaterMark(bizTaskHandle);
  if (sysTaskHandle) uxTaskGetStackHighWaterMark(sysTaskHandle);
}

// ============================================================================
// String Helpers
// ============================================================================

String cleanString(const String& input) {
  if (input.length() == 0) return String();
  const char* str = input.c_str();
  int len = input.length();
  int start = 0, end = len - 1;

  while (start < len && isspace((unsigned char)str[start])) start++;
  while (end >= start && isspace((unsigned char)str[end])) end--;

  return (start > end) ? String() : input.substring(start, end + 1);
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
// NVS Storage
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
// Temperature Sensor
// ============================================================================

float getInternalTemperatureC() {
#if ESP32_HAS_TEMP
  if (s_temp_sensor == NULL) return NAN;
  float tsens_out;
  if (temperature_sensor_get_celsius(s_temp_sensor, &tsens_out) == ESP_OK) {
    return tsens_out;
  }
#endif
  return NAN;
}

// ============================================================================
// Memory Information
// ============================================================================
MemoryInfo getMemoryInfo() {
  MemoryInfo info;
  info.flashSizeMB = ESP.getFlashChipSize() / (1024 * 1024);

#if CONFIG_SPIRAM_SUPPORT || CONFIG_ESP32_SPIRAM_SUPPORT
  info.hasPsram = psramFound();
  if (info.hasPsram) {
    info.psramSizeBytes = ESP.getPsramSize();
    info.psramFreeBytes = ESP.getFreePsram();
  } else {
    info.psramSizeBytes = 0;
    info.psramFreeBytes = 0;
  }
#else
  info.hasPsram = false;
  info.psramSizeBytes = 0;
  info.psramFreeBytes = 0;
#endif

  return info;
}

// ============================================================================
// BLE Functions
// ============================================================================

void sendBLE(const String& m) {
#if ESP32_HAS_BLE
  if (!bleDeviceConnected || !pTxCharacteristic) return;

  const char* data = m.c_str();
  size_t len = m.length();

  if (len <= 512) {
    pTxCharacteristic->setValue((uint8_t*)data, len);
    pTxCharacteristic->notify();
  } else {
    for (size_t i = 0; i < len; i += 512) {
      size_t chunk_size = (len - i > 512) ? 512 : (len - i);  // FIXED
      pTxCharacteristic->setValue((uint8_t*)(data + i), chunk_size);
      pTxCharacteristic->notify();
      delay(20);
    }
  }
#else
  (void)m;
#endif
}

#if ESP32_HAS_BLE

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
    size_t to_copy = min(len, available);
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
  NimBLEDevice::init(BLE_ADVERT_NAME);
  NimBLEDevice::setMTU(256);

  pBLEServer = NimBLEDevice::createServer();
  pBLEServer->setCallbacks(new MyServerCallbacks());

  NimBLEService* pService = pBLEServer->createService(BLE_SERVICE_UUID);
  pTxCharacteristic = pService->createCharacteristic(BLE_CHAR_UUID_TX, NIMBLE_PROPERTY::NOTIFY);

  NimBLECharacteristic* pRxCharacteristic = pService->createCharacteristic(
    BLE_CHAR_UUID_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pRxCharacteristic->setCallbacks(new MyCharCallbacks());

  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  NimBLEAdvertisementData advData;
  advData.setName(BLE_ADVERT_NAME);
  advData.addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setAdvertisementData(advData);

  NimBLEAdvertisementData scanData;
  scanData.setName(BLE_ADVERT_NAME);
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
void initBLE() {}
void handleBLEReconnect() {}
#endif

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
      WiFi.config(netConfig.staticIP, netConfig.gateway, netConfig.subnet, netConfig.dns);
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
    response += "|" + WiFi.SSID() + "|";
    response += connected ? WiFi.localIP().toString() : "-";
    response += "|";
    response += connected ? String(WiFi.RSSI()) : "-";
    response += "|";
    response += netConfig.useDHCP ? "DHCP" : "STATIC";
    if (!netConfig.useDHCP) {
      response += "|" + netConfig.staticIP.toString();
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
    response = "HEAP:FREE=" + String(ESP.getFreeHeap());
    response += "|MIN=" + String(ESP.getMinFreeHeap());
    response += "|MAX=" + String(ESP.getMaxAllocHeap()) + "\n";
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
// WiFi Management
// ============================================================================

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);

  String ssid = prefs.getString("wifi_ssid", "");
  String pass = prefs.getString("wifi_pass", "");
  if (ssid.length() == 0) return;

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
      case ARDUINO_EVENT_WIFI_STA_CONNECTED:
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
      break;

    case WIFI_STATE_CONNECTING:
      if (status == WL_CONNECTED) {
        wifiState = WIFI_STATE_CONNECTED;
        wifiReconnectAttempts = 0;
      } else if (now - wifiLastConnectAttempt > WIFI_CONNECT_TIMEOUT) {
        wifiState = WIFI_STATE_DISCONNECTED;
      }
      break;

    case WIFI_STATE_CONNECTED:
      if (status != WL_CONNECTED) {
        wifiState = WIFI_STATE_DISCONNECTED;
      }
      break;

    case WIFI_STATE_DISCONNECTED:
      uint8_t maxShift = min(wifiReconnectAttempts, (uint8_t)4);
      uint32_t backoffDelay = WIFI_RECONNECT_DELAY * (1 << maxShift);
      if (now - wifiLastConnectAttempt > backoffDelay) {
        wifiState = WIFI_STATE_IDLE;
      }
      break;
  }
}

// ============================================================================
// Task Monitoring Helpers
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

// ============================================================================
// Per-Core Task Monitoring (Main Algorithm)
// ============================================================================

void updateTaskMonitoring() {
  if (millis() - lastTaskSample < 500) return;
  lastTaskSample = millis();

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

  // Calculate per-core totals
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

  // Calculate per-core deltas
  uint64_t coreDelta[2] = { 0, 0 };
  for (int c = 0; c < NUM_CORES; c++) {
    coreDelta[c] = coreRuntime[c].totalRuntime100ms - coreRuntime[c].prevTotalRuntime100ms;
    if (coreDelta[c] == 0) coreDelta[c] = 1;
  }

  // First time initialization
  if (!statsInitialized) {
    taskCount = min(numTasks, (UBaseType_t)MAX_TASKS_MONITORED);
    for (uint8_t i = 0; i < taskCount; i++) {
      taskData[i].name = String(statusArray[i].pcTaskName);
      taskData[i].priority = statusArray[i].uxCurrentPriority;
      taskData[i].state = statusArray[i].eCurrentState;
      taskData[i].runtime = statusArray[i].ulRunTimeCounter;
      taskData[i].prevRuntime = statusArray[i].ulRunTimeCounter;
      taskData[i].runtimeAccumUs = 0;  // ✅ FIX: Start at 0
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

  // Update existing tasks with per-core CPU calculation
  for (uint8_t i = 0; i < taskCount; i++) {
    bool found = false;
    for (uint8_t j = 0; j < numTasks; j++) {
      if (taskData[i].handle == statusArray[j].xHandle) {
        uint32_t currentRuntime100ms = statusArray[j].ulRunTimeCounter;

        // ✅ FIX: Calculate runtime delta with proper overflow handling
        uint32_t taskDelta;
        if (currentRuntime100ms >= taskData[i].prevRuntime) {
          // Normal case: no overflow
          taskDelta = currentRuntime100ms - taskData[i].prevRuntime;
        } else {
          // Overflow case: counter wrapped around
          taskDelta = (0xFFFFFFFF - taskData[i].prevRuntime) + currentRuntime100ms + 1;
        }

        taskData[i].runtimeAccumUs += (uint64_t)taskDelta;

        BaseType_t affinity = getSafeAffinity(statusArray[j].xHandle);
        taskData[i].coreAffinity = affinity;

        if (affinity == tskNO_AFFINITY) {
          noAffinityAccumUs += taskDelta;
        } else if (affinity >= 0 && affinity < NUM_CORES) {
          coreAccumUs[affinity] += taskDelta;
        }

        // Calculate CPU% based on core affinity
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

  // Add new tasks
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
      taskData[taskCount].runtimeAccumUs = 0;  // ✅ FIX: Start at 0
      taskData[taskCount].stackHighWater = statusArray[j].xHandle ? uxTaskGetStackHighWaterMark(statusArray[j].xHandle) : 0;
      taskData[taskCount].stackHealth = getStackHealth(taskData[taskCount].stackHighWater);
      taskData[taskCount].cpuPercent = 0;
      taskData[taskCount].handle = statusArray[j].xHandle;
      taskData[taskCount].coreAffinity = getSafeAffinity(statusArray[j].xHandle);
      taskCount++;
    }
  }

  // Store totals for next sample
  for (int c = 0; c < NUM_CORES; c++) {
    coreRuntime[c].prevTotalRuntime100ms = coreRuntime[c].totalRuntime100ms;
  }
}

// ============================================================================
// API Endpoints
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
  doc["heap_max_alloc"] = ESP.getMaxAllocHeap();
  doc["core0_load"] = coreLoadPct[0];
  doc["core1_load"] = coreLoadPct[1];
  doc["chip_model"] = ESP.getChipModel();
  doc["cpu_freq"] = ESP.getCpuFreqMHz();
  doc["num_cores"] = NUM_CORES;
  doc["temp_c"] = getInternalTemperatureC();

  // Memory information
  MemoryInfo memInfo = getMemoryInfo();
  JsonObject memory = doc.createNestedObject("memory");
  memory["flash_mb"] = memInfo.flashSizeMB;
  memory["psram_total"] = memInfo.psramSizeBytes;
  memory["psram_free"] = memInfo.psramFreeBytes;
  memory["has_psram"] = memInfo.hasPsram;

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

void sendTaskMonitoring() {
  DynamicJsonDocument doc(5120);
  JsonArray arr = doc.createNestedArray("tasks");

  uint8_t activeTaskCount = 0;
  for (uint8_t i = 0; i < taskCount; i++) {
    // Skip deleted tasks
    if (taskData[i].state == eDeleted) continue;
    activeTaskCount++;

    JsonObject t = arr.createNestedObject();
    t["name"] = taskData[i].name;
    t["priority"] = taskData[i].priority;
    t["state"] = getTaskStateName(taskData[i].state);
    t["runtime"] = (uint64_t)taskData[i].runtimeAccumUs / 1000000ULL;
    t["stack_hwm"] = taskData[i].stackHighWater;
    t["stack_health"] = taskData[i].stackHealth;
    t["cpu_percent"] = taskData[i].cpuPercent;
    t["core"] = getAffinityString(taskData[i].coreAffinity);
  }

  doc["task_count"] = activeTaskCount;
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
// REST Handlers
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
// HTML Dashboard
// ============================================================================

static const char INDEX_HTML_PART1[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32(x) Controller</title>
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
.status-dot.on {background:#00ff41;box-shadow:0 0 10px 2px #00ff41}
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
.task-table-wrapper {
  max-height: 500px;
  overflow-y: auto;
  overflow-x: auto;
  margin-top: 6px;
  border: 1px solid #30363d;
  border-radius: 4px;
}
.task-table {
  width:100%;
  border-collapse:collapse;
  font-size:0.85em;
  margin:0;
}
.task-table thead {
  position: sticky;
  top: 0;
  z-index: 10;
}
</style>
</head>
<body>
<div class="container">
<h1>ESP32(x) Per-Core Monitor</h1>
<div class="card">
 <h3>System Status <span class="small">Active state</span></h3>
 <div class="row">
  <div><span class="status-dot off" id="bleDot"></span><span>BLE</span></div>
  <div><span class="status-dot off" id="wifiDot"></span><span>WiFi</span></div>
  <div class="pill" id="ipInfo">IP: -</div>
  <div class="pill" id="rssiInfo">RSSI: --</div>
  <div class="pill">Up: <span id="upt">--:--:--</span></div>
  <div class="pill">Free Heap: <span id="heap">--</span>/<span id="heapTot">--</span></div>  
  <div class="pill">Temp: <span id="tempC">--</span></div>
  <div class="pill"><span class="core-badge core-0">C0</span> <span id="c0">-</span>% (<span id="c0Tasks">-</span> tasks)</div>
  <div class="pill" id="c1pill"><span class="core-badge core-1">C1</span> <span id="c1">-</span>% (<span id="c1Tasks">-</span> tasks)</div>
  <div class="pill">Tasks: <span id="taskCount">-</span></div>
  <div class="pill"><span id="chip">-</span> @ <span id="cpuFreq">-</span>MHz</div>
  <div class="pill">Flash: <span id="flashSize">--</span>MB</div>
  <div class="pill" id="psramPill" style="display:none">PSRAM: <span id="psramUsed">--</span>/<span id="psramTotal">--</span></div>
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
)rawliteral";

#if DEBUG_MODE
static const char INDEX_HTML_TASKS[] PROGMEM = R"rawliteral(
<div class="card">
 <h3>Tasks <span class="small">Per-Core CPU%</span></h3>
 <div id="taskMonitor"><p style="text-align:center;color:#8b949e;padding:12px">Loading...</p></div>
</div>
)rawliteral";
#endif

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
 const taskMonitorEl = I('taskMonitor');
 if(!taskMonitorEl) return; // ✅ Exit early if Tasks card doesn't exist
 
 const j=await api('/api/tasks');
 if(j.error)return;
 if(!j.tasks){taskMonitorEl.innerHTML='<p style="text-align:center;color:#8b949e">No tasks</p>';return;}
 j.tasks.sort((a,b)=>(b.runtime||0)-(a.runtime||0));
 let h='<div class="task-table-wrapper"><table class="task-table"><thead><tr><th>Task</th><th>Core</th><th>Pri</th><th>Stack</th><th>State</th><th>CPU%</th><th>Runtime</th></tr></thead><tbody>';
 j.tasks.forEach(t=>{
  const cpuPct = Math.min(100, t.cpu_percent || 0);
  h+=`<tr>
   <td class="task-name">${t.name}</td>
   <td><span class="core-badge ${coreCls(t.core)}">${t.core}</span></td>
   <td>${t.priority}</td>
   <td><span class="${healthCls(t.stack_health)}">${t.stack_hwm}</span></td>
   <td><span class="task-state ${stCls(t.state)}">${t.state}</span></td>
   <td><span class="cpu-badge ${cpuCls(cpuPct)}">${cpuPct}%</span>
     <div class="progress-bar"><div class="progress-fill" style="width:${cpuPct}%"></div></div></td>
   <td>${fmU((t.runtime||0)*1000)}</td>
  </tr>`;
 });
 h+='</tbody></table></div>';
 taskMonitorEl.innerHTML=h;
 
 // Update task count in System Status (always present)
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
 I('heapTot').textContent=fmB(j.heap_total||0);
 I('c0').textContent=j.core0_load??'-';
 I('c1').textContent=j.core1_load??'-';
 I('chip').textContent=j.chip_model||'-';
 I('cpuFreq').textContent=j.cpu_freq||'-';
 if(j.temp_c===null||j.temp_c===undefined){I('tempC').textContent='n/a';}
 else{I('tempC').textContent=j.temp_c.toFixed?j.temp_c.toFixed(1)+'°C':j.temp_c+'°C';}
 
 // Memory information
 if(j.memory){
  I('flashSize').textContent=j.memory.flash_mb||'--';
  if(j.memory.has_psram){
   I('psramPill').style.display='inline-block';
   const psramUsed = (j.memory.psram_total - j.memory.psram_free);
   I('psramUsed').textContent=fmB(psramUsed);
   I('psramTotal').textContent=fmB(j.memory.psram_total);
  }else{
   I('psramPill').style.display='none';
  }
 }
 
 if(j.num_cores===1){I('c1pill').style.display='none';}else{I('c1pill').style.display='inline-block';}
 if(j.biz){
  I('bizState').textContent=j.biz.running?'RUNNING':'STOPPED';
  I('bizQueue').textContent=j.biz.queue??'-';
  I('bizProcessed').textContent=j.biz.processed??'-';
 }
}
)rawliteral";

static const char INDEX_HTML_END[] PROGMEM = R"rawliteral(
setInterval(refresh,2000);
setInterval(refreshTasks,3000);
refresh();
refreshTasks();
</script>
</body>
</html>
)rawliteral";

static void sendIndex() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent_P(INDEX_HTML_PART1);
  delay(1);
#if DEBUG_MODE
  server.sendContent_P(INDEX_HTML_TASKS);
  delay(1);
#endif
  server.sendContent_P(INDEX_HTML_PART2);
  delay(1);
  server.sendContent_P(INDEX_HTML_END);
  server.client().flush();
  delay(2);
  server.client().stop();
}

// ============================================================================
// Routes
// ============================================================================

void routes() {
  server.on("/", HTTP_GET, sendIndex);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/tasks", HTTP_GET, sendTaskMonitoring);
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
// RTOS Tasks
// ============================================================================
void systemTask(void* param) {
  esp_task_wdt_add(NULL);
  bool ledState = false;
  uint32_t ledTs = 0;
  pinMode(BLE_LED_PIN, OUTPUT);
  
  // Set initial state based on LED polarity
  digitalWrite(BLE_LED_PIN, LED_INVERTED ? HIGH : LOW);

  for (;;) {
    esp_task_wdt_reset();
    checkWiFiConnection();
    handleBLEReconnect();
    updateCpuLoad();
    checkTaskStacks();

    if (bleDeviceConnected) {
      uint32_t now = millis();
      if (now - ledTs > BLE_LED_BLINK_MS) {
        ledTs = now;
        ledState = !ledState;
        digitalWrite(BLE_LED_PIN, LED_INVERTED ? (ledState ? LOW : HIGH) : (ledState ? HIGH : LOW));
      }
    } else {
      digitalWrite(BLE_LED_PIN, LED_INVERTED ? HIGH : LOW);
      ledState = false;
    }

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
// Setup & Loop
// ============================================================================

void setup() {
  delay(300);

  // Suppress verbose ESP logs
  esp_log_level_set("wifi", ESP_LOG_WARN);
  esp_log_level_set("dhcpc", ESP_LOG_WARN);
  esp_log_level_set("phy_init", ESP_LOG_WARN);

  // Configure watchdog
  esp_task_wdt_deinit();
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);

  // Initialize NVS and load config
  prefs.begin("esp32_base", false);
  loadNetworkConfig();

  // Initialize temperature sensor if available
#if ESP32_HAS_TEMP
  temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
  if (temperature_sensor_install(&cfg, &s_temp_sensor) == ESP_OK) {
    temperature_sensor_enable(s_temp_sensor);
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

  // Create RTOS tasks
#if NUM_CORES > 1
  xTaskCreatePinnedToCore(bizTask, "biz", 4096, nullptr, 1, &bizTaskHandle, 1);
  xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, &webTaskHandle, 0);
  xTaskCreatePinnedToCore(systemTask, "sys", 3072, nullptr, 2, &sysTaskHandle, 0);
#else
  xTaskCreate(bizTask, "biz", 4096, nullptr, 1, &bizTaskHandle);
  xTaskCreate(webTask, "web", 8192, nullptr, 1, &webTaskHandle);
  xTaskCreate(systemTask, "sys", 3072, nullptr, 2, &sysTaskHandle);
#endif
}

void loop() {
  vTaskDelete(NULL);
}
