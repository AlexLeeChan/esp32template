#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>
#include "config.h"

#if ESP32_HAS_TEMP
  #include "driver/temperature_sensor.h"
#endif

// ============================================================================
// TYPE DEFINITIONS
// ============================================================================

/**
 * Message structure for zero-malloc command execution
 */
struct ExecMessage {
  char payload[MAX_MSG_SIZE];
  uint16_t length;
  bool inUse;
};

/**
 * WiFi state machine states
 */
enum WiFiState : uint8_t {
  WIFI_STATE_IDLE = 0,
  WIFI_STATE_CONNECTING,
  WIFI_STATE_CONNECTED,
  WIFI_STATE_DISCONNECTED
};

/**
 * Business logic module states
 */
enum BizState : uint8_t {
  BIZ_STOPPED = 0,
  BIZ_RUNNING = 1
};

/**
 * Memory information structure
 */
struct MemoryInfo {
  uint32_t flashSizeMB;
  uint32_t psramSizeBytes;
  uint32_t psramFreeBytes;
  bool hasPsram;
};

/**
 * Network configuration structure
 */
struct NetworkConfig {
  bool useDHCP;
  IPAddress staticIP;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns;
};

/**
 * WiFi credentials cache (RAM only)
 */
struct WiFiCredentials {
  char ssid[64];
  char password[64];
  bool hasCredentials;
};

#if DEBUG_MODE
/**
 * Debug log entry structure
 */
struct LogEntry {
  uint32_t uptime;
  uint32_t epoch;
  char msg[60];
};

/**
 * Flash write operation types
 */
enum FlashWriteType : uint8_t {
  FLASH_WRITE_REBOOT_LOGS = 0,
  FLASH_WRITE_WIFI_LOGS = 1,
  FLASH_WRITE_ERROR_LOGS = 2
};

/**
 * Flash write request structure
 */
struct FlashWriteRequest {
  FlashWriteType type;
  uint32_t timestamp;
};

/**
 * Task monitoring data structure
 * 
 * IMPORTANT: FreeRTOS ulRunTimeCounter returns time in 100μs ticks
 * 1 tick = 100 microseconds = 0.1 milliseconds
 */
struct TaskMonitorData {
  String name;
  UBaseType_t priority;
  eTaskState state;
  
  // Runtime tracking (FreeRTOS native 100μs ticks)
  uint32_t runtimeTicks;          // Current runtime in 100μs ticks
  uint32_t prevRuntimeTicks;      // Previous sample in 100μs ticks
  
  // Cumulative runtime for display (converted to seconds)
  uint64_t totalRuntimeSec;       // Total runtime in seconds
  
  // Stack and CPU monitoring
  uint32_t stackHighWater;
  uint8_t cpuPercent;             // Per-task CPU% (0-100)
  String stackHealth;
  BaseType_t coreAffinity;
  TaskHandle_t handle;
};

/**
 * Core runtime data structure
 * 
 * Tracks total runtime per core for CPU% calculation
 * All values in FreeRTOS native 100μs ticks
 */
struct CoreRuntimeData {
  uint64_t totalRuntimeTicks;     // Current total runtime in 100μs ticks
  uint64_t prevTotalRuntimeTicks; // Previous sample in 100μs ticks
  uint8_t taskCount;              // Number of tasks on this core
  uint8_t loadPercent;            // Core load percentage (0-100)
};
#endif // DEBUG_MODE

#if ENABLE_OTA
/**
 * OTA update states
 */
enum OTAState : uint8_t {
  OTA_IDLE = 0,
  OTA_CHECKING,
  OTA_DOWNLOADING,
  OTA_FLASHING,
  OTA_SUCCESS,
  OTA_FAILED
};

/**
 * OTA status structure
 */
struct OTAStatus {
  volatile OTAState state;
  volatile uint8_t progress;
  String error;
  bool available;
  volatile uint32_t fileSize;
};
#endif // ENABLE_OTA

#endif // TYPES_H