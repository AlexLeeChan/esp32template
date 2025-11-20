/* ==============================================================================
   TYPES.H - Type Definitions
   
   Defines all custom data structures and enumerations used throughout the system:
   - Message structures for inter-task communication
   - State machine enums (WiFi, OTA, Business logic)
   - Configuration structures (Network, Memory, WiFi credentials)
   - Debug logging structures (when DEBUG_MODE enabled)
   
   These types ensure type safety and code clarity across modules.
   ============================================================================== */

/* Header guard to prevent multiple inclusion of types.h */
#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>
#include "config.h"

#if ESP32_HAS_TEMP
  #include "driver/temperature_sensor.h"
#endif

struct ExecMessage {
  char payload[MAX_MSG_SIZE];
  uint16_t length;
  bool inUse;
};

enum WiFiState : uint8_t {
  WIFI_STATE_IDLE = 0,
  WIFI_STATE_CONNECTING,
  WIFI_STATE_CONNECTED,
  WIFI_STATE_DISCONNECTED
};

enum BizState : uint8_t {
  BIZ_STOPPED = 0,
  BIZ_RUNNING = 1
};

struct MemoryInfo {
  uint32_t flashSizeMB;
  uint32_t psramSizeBytes;
  uint32_t psramFreeBytes;
  bool hasPsram;
};

struct NetworkConfig {
  bool useDHCP;
  IPAddress staticIP;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns;
};

struct WiFiCredentials {
  char ssid[64];
  char password[64];
  bool hasCredentials;
};

#if DEBUG_MODE

struct LogEntry {
  uint32_t uptime;
  uint32_t epoch;
  char msg[60];
};

enum FlashWriteType : uint8_t {
  FLASH_WRITE_REBOOT_LOGS = 0,
  FLASH_WRITE_WIFI_LOGS = 1,
  FLASH_WRITE_ERROR_LOGS = 2
};

struct FlashWriteRequest {
  FlashWriteType type;
  uint32_t timestamp;
};

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

struct CoreRuntimeData {
  uint64_t totalRuntime100ms;
  uint64_t prevTotalRuntime100ms;
  uint8_t taskCount;
  uint8_t cpuPercentTotal;
};
#endif

#if ENABLE_OTA

enum OTAState : uint8_t {
  OTA_IDLE = 0,
  OTA_CHECKING,
  OTA_DOWNLOADING,
  OTA_FLASHING,
  OTA_SUCCESS,
  OTA_FAILED
};

struct OTAStatus {
  volatile OTAState state;
  volatile uint8_t progress;
  String error;
  bool available;
  volatile uint32_t fileSize;
};
#endif

#endif
