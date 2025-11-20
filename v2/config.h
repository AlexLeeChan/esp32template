/* ============================================================================== 
   CONFIG.H - System Configuration
   
   Contains all configurable parameters for the ESP32 system including:
   - Feature enable/disable flags (DEBUG_MODE, ENABLE_OTA)
   - Timing parameters (timeouts, intervals)
   - Hardware-specific settings (LED pins, chip capabilities)
   - Network settings (NTP servers, WiFi parameters)
   
   Modify values here to customize system behavior.
   ============================================================================== */

/* Header guard to prevent multiple inclusion of config.h */
#ifndef CONFIG_H
#define CONFIG_H

#define DEBUG_MODE 1
#define ENABLE_OTA 1

#define WDT_TIMEOUT 20
#define STACK_CHECK_INTERVAL 60000
#define WIFI_RECONNECT_DELAY 15000
#define WIFI_CONNECT_TIMEOUT 30000
#define MAX_WIFI_RECONNECT_ATTEMPTS 5

#define MSG_POOL_SIZE 10
#define MAX_MSG_SIZE 256
#define MAX_BLE_CMD_LENGTH 256

#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"
#define NTP_SERVER_3 "time.google.com"
#define GMT_OFFSET_SEC 0
#define DAYLIGHT_OFFSET_SEC 0
#define NTP_SYNC_INTERVAL 3600

#if DEBUG_MODE
  #define MAX_DEBUG_LOGS 32
  #define FLASH_WRITE_QUEUE_SIZE 32
#endif

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

#if CONFIG_FREERTOS_UNICORE
  #define NUM_CORES 1
#else
  #define NUM_CORES 2
#endif

#endif
