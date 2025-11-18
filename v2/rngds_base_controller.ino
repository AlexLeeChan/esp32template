/**
 * ============================================================================
 * ESP32(x) Universal Control Template
 * ============================================================================
 * 
 * A production-ready ESP32 firmware template featuring:
 *   • FreeRTOS task management with monitoring
 *   • BLE (NimBLE) for wireless configuration
 *   • WiFi with automatic reconnection
 *   • Web server with REST API
 *   • OTA firmware updates (optional)
 *   • Flash-safe initialization order
 *   • Comprehensive error logging
 * 
 * CHANGELOG (Latest First):
 * ----------------------------------------------------------------------------
 * v1.3 - Multi-File Refactoring
 *   [REFACTOR] Split monolithic code into modular files
 *   [IMPROVE] Proper C/C++ structure with headers
 *   [IMPROVE] Arduino IDE compatible file organization
 *   [IMPROVE] Easier maintenance and debugging
 * 
 * v1.2 - Critical WiFi Bug Fix & Performance Improvements
 *   [CRITICAL] Fixed infinite loop in WiFi state machine (wifiConfigChanged)
 *   [PERF] Smart WiFi check frequency: 2s when connected, 500ms when not
 *   [PERF] Intelligent reconfiguration avoids redundant WiFi resets
 *   [PERF] 2-second debounce on disconnect events prevents thrashing
 *   [PERF] Flash write coalescing: 50ms → 200ms (4x flash wear reduction)
 *   [PERF] WiFi reconnection delays: 100ms → 50ms
 *   [PERF] Exponential backoff using bit-shift operations
 *   [FIX] All WiFi operations now use vTaskDelay (RTOS-safe)
 * 
 * v1.1 - Added OTA and Debug Features
 *   [NEW] Over-the-Air firmware updates with progress tracking
 *   [NEW] Task monitoring with CPU usage statistics
 *   [NEW] Persistent debug logs (reboot, WiFi, errors)
 * 
 * v1.0 - Initial Release
 *   [NEW] Base template with WiFi, BLE, and web server
 * 
 * ARCHITECTURE:
 * ----------------------------------------------------------------------------
 * • Boot Phase 1: All flash/NVS operations (config loading)
 * • Boot Phase 2: Hardware initialization (BLE, sensors)
 * • Boot Phase 3: RTOS task creation
 * • Boot Phase 4: Web server start
 * • Boot Phase 5: WiFi initialization (last to avoid conflicts)
 * 
 * TASK STRUCTURE:
 * ----------------------------------------------------------------------------
 * • systemTask (Core 0, Pri 2): WiFi, NTP, BLE reconnection, system monitoring
 * • webTask (Core 0, Pri 1): HTTP server, client handling, task stats
 * • bizTask (Core 1, Pri 1): Business logic, command processing
 * 
 * FILE STRUCTURE:
 * ----------------------------------------------------------------------------
 * • config.h              - Configuration constants
 * • types.h               - Type definitions and enums
 * • globals.h/.cpp        - Global variable declarations/definitions
 * • hardware.h/.cpp       - Hardware abstraction (temp sensor, memory)
 * • time_handler.h/.cpp   - NTP and RTC functions
 * • wifi_handler.h/.cpp   - WiFi management
 * • ble_handler.h/.cpp    - BLE server and command handling
 * • web_handler.h/.cpp    - Web server and API endpoints
 * • web_html.h            - HTML/CSS/JS content (PROGMEM)
 * • tasks.h/.cpp          - FreeRTOS tasks and message pool
 * • debug_handler.h/.cpp  - Debug logging (if DEBUG_MODE)
 * • ota_handler.h/.cpp    - OTA updates (if ENABLE_OTA)
 * • rngds_base_controller.ino - Main file (this file)
 * 
 * AUTHOR: Auto-generated template
 * LICENSE: MIT
 * ============================================================================
 */

// ============================================================================
// INCLUDES
// ============================================================================

// Configuration and Types
#include "config.h"
#include "types.h"
#include "globals.h"

// Core Handlers
#include "hardware.h"
#include "time_handler.h"
#include "wifi_handler.h"
#include "ble_handler.h"
#include "web_handler.h"
#include "tasks.h"

// Optional Modules
#if DEBUG_MODE
#include "debug_handler.h"
#endif

#if ENABLE_OTA
#include "ota_handler.h"
#include <esp_ota_ops.h>
#endif

// ESP32 System Libraries
#include <esp_system.h>
#include <esp_task_wdt.h>

#if ESP32_HAS_TEMP
#include "driver/temperature_sensor.h"
#endif

// ============================================================================
// FreeRTOS Runtime Statistics Support
// ============================================================================

/**
 * @brief Configure timer for runtime statistics (called by FreeRTOS)
 */
void configureTimerForRunTimeStats(void) {
  runtimeOffsetUs = esp_timer_get_time();
}

/**
 * @brief Get current runtime counter value in 100us ticks
 * @return Runtime counter value
 */
uint32_t ulGetRunTimeCounterValue(void) {
  return (uint32_t)((esp_timer_get_time() - runtimeOffsetUs) / 100ULL);
}

// ============================================================================
// SETUP - Flash-Safe Initialization Order
// ============================================================================

// ============================================================================
// SETUP - Fixed Flash-Safe Initialization Order
// ============================================================================

void setup() {
  delay(300);
  Serial.begin(115200);
  Serial.println(F("\n\n==========================================="));
  Serial.println(F("    ESP32 BOOT - Flash-Safe Init"));
  Serial.println(F("===========================================\n"));

  // Reduce log verbosity for common IDF messages
  esp_log_level_set("wifi", ESP_LOG_WARN);
  esp_log_level_set("dhcpc", ESP_LOG_WARN);
  esp_log_level_set("phy_init", ESP_LOG_WARN);

  // Initialize Task Watchdog
  esp_task_wdt_deinit();
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = (uint32_t)(WDT_TIMEOUT * 1000),
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);

  // ===== PHASE 1: ALL FLASH OPERATIONS (NVS) =====
  Serial.println(F("Phase 1: Loading configuration from flash..."));

#if DEBUG_MODE
  flashWriteMutex = xSemaphoreCreateMutex();
  if (!flashWriteMutex) {
    Serial.println(F("CRITICAL: Failed to create flashWriteMutex!"));
  }
#endif

  // Open preferences ONCE and keep it open
  if (!prefs.begin("esp32_base", false)) {
    Serial.println(F("CRITICAL: Failed to initialize Preferences!"));
    delay(5000);
    ESP.restart();
  }

  // Load ALL configuration from NVS into RAM
  loadNetworkConfig();
  loadWiFiCredentials();

#if DEBUG_MODE
  loadDebugLogs();

  // Check reboot reason
  esp_reset_reason_t reason = esp_reset_reason();
  bool userReq = prefs.getBool("userRebootRequested", false);
  if (userReq) {
    prefs.putBool("userRebootRequested", false);
    Serial.println(F("Last reboot was user-requested"));
  } else {
    if (reason != ESP_RST_POWERON && reason != ESP_RST_DEEPSLEEP) {
      String msg = "Unexpected reboot: " + formatResetReason(reason);
      addRebootLog(msg, 0);
      Serial.printf("Boot: %s\n", msg.c_str());
    }
  }
#endif

  // DON'T CLOSE PREFERENCES - Keep them open for runtime use
  // This avoids the problematic close/reopen pattern

  Serial.println(F("Phase 1 complete: All config loaded into RAM\n"));

  // Brief delay to ensure any pending NVS operations complete
  delay(100);

  // ===== PHASE 2: NON-NETWORK INITIALIZATION =====
  Serial.println(F("Phase 2: Initializing non-network components..."));

#if ESP32_HAS_TEMP
  temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
  if (temperature_sensor_install(&cfg, &s_temp_sensor) == ESP_OK) {
    temperature_sensor_enable(s_temp_sensor);
  }
#endif

  // Initialize message pool and mutexes
  initMessagePool();

  // Initialize BLE (doesn't use TCP/IP)
  initBLE();

#if DEBUG_MODE
  // Start the background flash-writing task
  flashWriteQueue = xQueueCreate(FLASH_WRITE_QUEUE_SIZE, sizeof(FlashWriteRequest));
  xTaskCreate(flashWriteTask, "flash", 3072, nullptr, 0, &flashWriteTaskHandle);
#endif

  // Create command queue
  execQ = xQueueCreate(MSG_POOL_SIZE, sizeof(ExecMessage*));

#if DEBUG_MODE
  // Initialize core runtime data
  for (int c = 0; c < NUM_CORES; c++) {
    coreRuntime[c].totalRuntimeTicks = 0;
    coreRuntime[c].prevTotalRuntimeTicks = 0;
    coreRuntime[c].taskCount = 0;
    coreRuntime[c].loadPercent = 0;
  }
#endif

  Serial.println(F("Phase 2 complete\n"));

  // ===== PHASE 3: RTOS TASKS =====
  Serial.println(F("Phase 3: Creating RTOS tasks..."));

#if NUM_CORES > 1
  xTaskCreatePinnedToCore(bizTask, "biz", 4096, nullptr, 1, &bizTaskHandle, 1);
  xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, &webTaskHandle, 0);
  xTaskCreatePinnedToCore(systemTask, "sys", 4096, nullptr, 2, &sysTaskHandle, 0);
#else
  xTaskCreate(bizTask, "biz", 4096, nullptr, 1, &bizTaskHandle);
  xTaskCreate(webTask, "web", 8192, nullptr, 1, &webTaskHandle);
  xTaskCreate(systemTask, "sys", 3072, nullptr, 2, &sysTaskHandle);
#endif

  // Give tasks time to fully initialize
  delay(300);

  Serial.println(F("Phase 3 complete: Tasks running\n"));

  // ===== PHASE 4: WIFI (LAST!) =====
  Serial.println(F("Phase 5: Initializing WiFi subsystem..."));

  // Configure WiFi (event handlers only - no connection)
  setupWiFi();

  // Initialize NTP *after* the TCP/IP stack is ready
  initNTP();

  // Final delay before WiFi connection
  delay(200);

  // CRITICAL FIX: Reset reconnect attempts on boot
  wifiReconnectAttempts = 0;
  wifiFirstConnectDone = false;

  // Set the initial config flag so systemTask applies config on its first run
  wifiConfigChanged = true;
  wifiState = WIFI_STATE_IDLE;

  Serial.println(F("Phase 5 complete: WiFi system task will now take over.\n"));

  // ===== PHASE 5: WEB SERVER =====
  Serial.println(F("Phase 4: Starting web server..."));

  registerRoutes();
  server.begin();
  serverStarted = true;

  // Ensure server is fully ready
  delay(200);

  Serial.println(F("Phase 4 complete: Web server ready\n"));
  // ===== BOOT COMPLETE =====
  Serial.println(F("==========================================="));
  Serial.println(F("       ESP32 SYSTEM INITIALIZED"));
  Serial.println(F("==========================================="));
  Serial.printf("Chip: %s @ %dMHz\n", ESP.getChipModel(), ESP.getCpuFreqMHz());
  Serial.printf("Cores: %d\n", NUM_CORES);
  Serial.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("Flash: %u MB\n", ESP.getFlashChipSize() / (1024 * 1024));

  MemoryInfo memInfo = getMemoryInfo();
  if (memInfo.hasPsram) {
    Serial.printf("PSRAM: %u bytes (Free: %u bytes)\n",
                  memInfo.psramSizeBytes, memInfo.psramFreeBytes);
  }

  if (wifiCredentials.hasCredentials) {
    Serial.printf("WiFi: Will attempt to connect to '%s'...\n", wifiCredentials.ssid);
  } else {
    Serial.println(F("WiFi: No credentials configured"));
  }

#if ENABLE_OTA
  Serial.println(F("OTA: Enabled"));
  const esp_partition_t* ota_partition = esp_ota_get_next_update_partition(NULL);
  if (ota_partition) {
    Serial.printf("OTA: Target partition '%s' available (%.2f MB)\n",
                  ota_partition->label,
                  ota_partition->size / 1048576.0);
  } else {
    Serial.println(F("OTA: WARNING - No OTA partition found!"));
  }
#else
  Serial.println(F("OTA: Disabled"));
#endif

#if DEBUG_MODE
  Serial.println(F("Debug: Enabled (task monitoring, logs, flash queue)"));
#else
  Serial.println(F("Debug: Disabled"));
#endif

  Serial.println(F("\nSystem ready!\n"));
  Serial.println(F("Connect via:"));
  if (wifiCredentials.hasCredentials) {
    Serial.println(F("  - WiFi (IP will be shown after connection)"));
  }
#if ESP32_HAS_BLE
  Serial.printf("  - BLE (%s)\n", BLE_ADVERT_NAME);
#endif
  Serial.println();
}

// ============================================================================
// LOOP - Deleted (all work done in FreeRTOS tasks)
// ============================================================================

/**
 * @brief Main loop (Arduino context)
 * We delete this task as all work is done in FreeRTOS tasks
 */
void loop() {
  vTaskDelete(NULL);
}