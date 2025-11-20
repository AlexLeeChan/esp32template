/* ==============================================================================
   RNGDS_BASE_CONTROLLER.INO - Main Arduino Sketch
   
   Entry point for the ESP32 application:
   
   setup(): Initializes all subsystems in correct order:
            - Serial communication
            - Hardware detection
            - NVS storage
            - WiFi
            - BLE (if available)
            - Web server
            - Debug logging
            - FreeRTOS tasks
   
   loop(): Mostly empty as FreeRTOS tasks handle all operations.
           Arduino loop() is only used for watchdog feeding and
           periodic maintenance if needed.
   
   This is a FreeRTOS-based multitasking application.
   ============================================================================== */


#include "config.h"
#include "types.h"
#include "globals.h"

#include "hardware.h"
#include "time_handler.h"
#include "wifi_handler.h"
#include "ble_handler.h"
#include "web_handler.h"
#include "tasks.h"

#if DEBUG_MODE
  #include "debug_handler.h"
#endif

#if ENABLE_OTA
  #include "ota_handler.h"
  #include <esp_ota_ops.h>
#endif

#include <esp_system.h>
#include <esp_task_wdt.h>

#if ESP32_HAS_TEMP
  #include "driver/temperature_sensor.h"
#endif

void configureTimerForRunTimeStats(void) {
  runtimeOffsetUs = esp_timer_get_time();
}

uint32_t ulGetRunTimeCounterValue(void) {
  return (uint32_t)((esp_timer_get_time() - runtimeOffsetUs) / 100ULL);
}

/* setup: Arduino entry point - initializes all system components in correct order */
void setup() {
  delay(300);
  Serial.begin(115200);
  Serial.println(F("\n\n========================================"));
  Serial.println(F("   ESP32 BOOT - Flash-Safe Init"));
  Serial.println(F("========================================\n"));

  esp_log_level_set("wifi", ESP_LOG_WARN);
  esp_log_level_set("dhcpc", ESP_LOG_WARN);
  esp_log_level_set("phy_init", ESP_LOG_WARN);

  esp_task_wdt_deinit();
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = (uint32_t)(WDT_TIMEOUT * 1000),
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);

  Serial.println(F("Phase 1: Loading configuration from flash..."));

  #if DEBUG_MODE
    flashWriteMutex = xSemaphoreCreateMutex();
    if (!flashWriteMutex) {
      Serial.println(F("CRITICAL: Failed to create flashWriteMutex!"));
    }
  #endif

  if (!prefs.begin("esp32_base", false)) {
    Serial.println(F("CRITICAL: Failed to initialize Preferences!"));
    delay(5000);
    ESP.restart();
  }

  loadNetworkConfig();
  loadWiFiCredentials();

#if DEBUG_MODE
  loadDebugLogs();

  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("Boot: Reset reason = %d (%s)\n", (int)reason, formatResetReason(reason).c_str());
  
  bool userReq = prefs.getBool(NVS_FLAG_USER_REBOOT, false);
  if (userReq) {
    prefs.putBool(NVS_FLAG_USER_REBOOT, false);
    Serial.println(F("Boot: User-requested reboot (flag was set)"));
  } else {

    if (reason != ESP_RST_POWERON && 
        reason != ESP_RST_DEEPSLEEP && 
        reason != ESP_RST_UNKNOWN) {
      String msg = "Unexpected reboot: " + formatResetReason(reason);
      addRebootLog(msg, 0);
      Serial.printf("Boot: %s (logged)\n", msg.c_str());
    } else {
      Serial.println(F("Boot: Normal boot (not logged)"));
    }
  }
#endif

  prefs.end();

  Serial.println(F("Waiting for flash operations to complete..."));
  delay(200);

  if (!prefs.begin("esp32_base", false)) {
    Serial.println(F("WARNING: Failed to reopen Preferences!"));
  }

  Serial.println(F("Phase 1 complete: All config loaded into RAM\n"));

  Serial.println(F("Phase 2: Initializing non-network components..."));
  Serial.println(F("CRITICAL: Creating ALL mutexes BEFORE tasks start"));

#if ESP32_HAS_TEMP
  temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
  if (temperature_sensor_install(&cfg, &s_temp_sensor) == ESP_OK) {
    temperature_sensor_enable(s_temp_sensor);
  }
#endif

  initMessagePool();

  initBLE();

#if DEBUG_MODE

  flashWriteQueue = xQueueCreate(FLASH_WRITE_QUEUE_SIZE, sizeof(FlashWriteRequest));
  xTaskCreate(flashWriteTask, "flash", 3072, nullptr, 0, &flashWriteTaskHandle);
#endif

  execQ = xQueueCreate(MSG_POOL_SIZE, sizeof(ExecMessage*));

  registerRoutes();
  Serial.println(F("Routes registered"));
  
  Serial.printf("Heap after Phase 2: Free=%u Min=%u\n", 
                ESP.getFreeHeap(), ESP.getMinFreeHeap());

#if DEBUG_MODE

  for (int c = 0; c < NUM_CORES; c++) {
    coreRuntime[c].totalRuntime100ms = 0;
    coreRuntime[c].prevTotalRuntime100ms = 0;
    coreRuntime[c].taskCount = 0;
    coreRuntime[c].cpuPercentTotal = 0;
  }
#endif

  Serial.println(F("Phase 2 complete\n"));

  Serial.println(F("Phase 3: Creating RTOS tasks..."));

#if NUM_CORES > 1
  xTaskCreatePinnedToCore(bizTask, "biz", 4096, nullptr, 1, &bizTaskHandle, 1);

  xTaskCreatePinnedToCore(webTask, "web", 10240, nullptr, 1, &webTaskHandle, 0);

  xTaskCreatePinnedToCore(systemTask, "sys", 12288, nullptr, 2, &sysTaskHandle, 0);
#else
  xTaskCreate(bizTask, "biz", 4096, nullptr, 1, &bizTaskHandle);

  xTaskCreate(webTask, "web", 10240, nullptr, 1, &webTaskHandle);

  xTaskCreate(systemTask, "sys", 10240, nullptr, 2, &sysTaskHandle);
#endif

  delay(500);

  Serial.println(F("Phase 3 complete: Tasks running\n"));

  Serial.println(F("Phase 4: Web server prepared..."));
  

  
  delay(100);

  Serial.println(F("Phase 4 complete\n"));

  Serial.println(F("Phase 5: Initializing WiFi & Web Server..."));

  setupWiFi();
  Serial.println(F("WiFi configured (TCP/IP stack ready)"));
  

  Serial.printf("Heap before server.begin(): Free=%u Min=%u\n", 
                ESP.getFreeHeap(), ESP.getMinFreeHeap());
  
  server.begin();
  Serial.println(F("Web server started"));
  
  Serial.printf("Heap after server.begin(): Free=%u Min=%u\n", 
                ESP.getFreeHeap(), ESP.getMinFreeHeap());
  

  serverStarted = true;
  delay(100);

  initNTP();

  delay(200);

  wifiConfigChanged = true;
  wifiState = WIFI_STATE_IDLE;

  Serial.println(F("Phase 5 complete: WiFi system task will now take over.\n"));

  bootComplete = true;

  Serial.println(F("========================================"));
  Serial.println(F("   ESP32 SYSTEM INITIALIZED"));
  Serial.println(F("========================================"));
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
    Serial.printf("WiFi: Will attempt to connect to %s...\n", wifiCredentials.ssid);
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

/* loop: Arduino main loop - mostly empty as FreeRTOS tasks handle everything */
void loop() {
  vTaskDelete(NULL);
}
