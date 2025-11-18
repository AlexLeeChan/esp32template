#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// CONFIGURATION - Modify these settings for your application
// ============================================================================

// Feature Flags
#define DEBUG_MODE 1                 // Enable serial logging, task monitoring, debug APIs
#define ENABLE_OTA 0                 // Enable Over-The-Air firmware updates

// System Timings
#define WDT_TIMEOUT 20               // Watchdog timeout (seconds)
#define STACK_CHECK_INTERVAL 60000   // Task stack monitoring interval (ms)
#define WIFI_RECONNECT_DELAY 15000   // Initial WiFi reconnect delay (ms)
#define WIFI_CONNECT_TIMEOUT 30000   // WiFi connection timeout (ms)
#define MAX_WIFI_RECONNECT_ATTEMPTS 5 // Maximum WiFi reconnection attempts

// Message Pool Configuration
#define MSG_POOL_SIZE 10             // Pre-allocated message pool size
#define MAX_MSG_SIZE 256             // Maximum message payload size (bytes)
#define MAX_BLE_CMD_LENGTH 256       // Maximum BLE command length (bytes)

// NTP Time Synchronization
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"
#define NTP_SERVER_3 "time.google.com"
#define GMT_OFFSET_SEC 0             // UTC offset (seconds)
#define DAYLIGHT_OFFSET_SEC 0        // Daylight saving time offset
#define NTP_SYNC_INTERVAL 3600       // NTP sync interval (seconds, 1 hour)

// Debug Logging (DEBUG_MODE only)
#if DEBUG_MODE
  #define MAX_DEBUG_LOGS 32          // Circular buffer size for each log type
  #define FLASH_WRITE_QUEUE_SIZE 32  // Flash write request queue depth
#endif

// Board-Specific LED Configuration
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
#else // Default ESP32
  #define BLE_LED_PIN 2
  #define LED_INVERTED 0
#endif
#define BLE_LED_BLINK_MS 500         // LED blink interval when BLE connected

// ============================================================================
// FREERTOS RUNTIME STATISTICS CONFIGURATION
// ============================================================================
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

// ============================================================================
// CHIP-SPECIFIC CONFIGURATION
// ============================================================================
#if defined(CONFIG_IDF_TARGET_ESP32)
  #define ESP32_HAS_BLE 1
  #define BLE_ADVERT_NAME "RNGDS_ESP32"
  #define ESP32_HAS_TEMP 0
  
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  #define ESP32_HAS_BLE 1
  #define BLE_ADVERT_NAME "RNGDS_ESP32S3"
  #define ESP32_HAS_TEMP 1
  
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  #define ESP32_HAS_BLE 1
  #define BLE_ADVERT_NAME "RNGDS_ESP32C3"
  #define ESP32_HAS_TEMP 1
  
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
  #define ESP32_HAS_BLE 1
  #define BLE_ADVERT_NAME "RNGDS_ESP32C6"
  #define ESP32_HAS_TEMP 1
  
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
  #define ESP32_HAS_BLE 0
  #define ESP32_HAS_TEMP 1
  
#else
  #define ESP32_HAS_BLE 1
  #define BLE_ADVERT_NAME "RNGDS"
  #define ESP32_HAS_TEMP 0
#endif

// Core Count Detection
#if CONFIG_FREERTOS_UNICORE
  #define NUM_CORES 1
#else
  #define NUM_CORES 2
#endif

#endif // CONFIG_H
