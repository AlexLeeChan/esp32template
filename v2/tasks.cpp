/* ==============================================================================
   TASKS.CPP - FreeRTOS Task Implementations
   
   Implements concurrent task functions:
   
   webTask: Handles HTTP server operations, processes incoming requests,
            manages web-based API endpoints
   
   bizTask: Contains main application logic, business rules, and periodic
            operations specific to your application
   
   Tasks communicate via queues and synchronize via mutexes. Each task
   includes watchdog resets and graceful shutdown support for OTA updates.
   ============================================================================== */

#include "tasks.h"
#include "globals.h"
#include "wifi_handler.h"
#include "ble_handler.h"
#include "time_handler.h"
#include "cpu_monitor.h"
#include "debug_handler.h"
#include "web_handler.h"
#include <esp_task_wdt.h>

#if ENABLE_OTA
#include <esp_ota_ops.h>
#endif

void initMessagePool() {

  poolMutex = xSemaphoreCreateMutex();
  if (!poolMutex) {
    Serial.println(F("FATAL: poolMutex creation failed!"));
    LOG_ERROR(F("poolMutex creation failed"), 0);
    while (1) { delay(1000); }
  }
  Serial.println(F("poolMutex created"));

  wifiMutex = xSemaphoreCreateMutex();
  if (!wifiMutex) {
    Serial.println(F("FATAL: wifiMutex creation failed!"));
    LOG_ERROR(F("wifiMutex creation failed"), 0);
    while (1) { delay(1000); }
  }
  Serial.println(F("wifiMutex created"));

  timeMutex = xSemaphoreCreateMutex();
  if (!timeMutex) {
    Serial.println(F("FATAL: timeMutex creation failed!"));
    LOG_ERROR(F("timeMutex creation failed"), 0);
    while (1) { delay(1000); }
  }
  Serial.println(F("timeMutex created"));

  for (int i = 0; i < MSG_POOL_SIZE; i++) {
    msgPool[i].inUse = false;
    msgPool[i].length = 0;
  }

#if ENABLE_OTA
  otaMutex = xSemaphoreCreateMutex();
  if (!otaMutex) {
    Serial.println(F("FATAL: otaMutex creation failed!"));
    LOG_ERROR(F("otaMutex creation failed"), 0);
    while (1) { delay(1000); }
  }
  Serial.println(F("otaMutex created"));

  taskDeletionMutex = xSemaphoreCreateMutex();
  if (!taskDeletionMutex) {
    Serial.println(F("FATAL: taskDeletionMutex creation failed!"));
    LOG_ERROR(F("taskDeletionMutex creation failed"), 0);
    while (1) { delay(1000); }
  }
  Serial.println(F("taskDeletionMutex created"));

  tasksDeleted = false;
  webTaskShouldExit = false;
  bizTaskShouldExit = false;
  otaInProgress = false;

  const esp_partition_t* ota_partition = esp_ota_get_next_update_partition(NULL);
  /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    otaStatus.available = (ota_partition != NULL);
    #if DEBUG_MODE
    if (!otaStatus.available) {
      addErrorLog(F("No OTA partition found"), 0);
    }
    #endif
    xSemaphoreGive(otaMutex);
  }
#endif
}

ExecMessage* allocMessage() {
  if (poolMutex && xSemaphoreTake(poolMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    for (int i = 0; i < MSG_POOL_SIZE; i++) {
      if (!msgPool[i].inUse) {
        msgPool[i].inUse = true;
        xSemaphoreGive(poolMutex);
        return &msgPool[i];
      }
    }
    xSemaphoreGive(poolMutex);
  }
  LOG_ERROR(F("Message pool exhausted or mutex NULL"), millis() / 1000);
  return nullptr;
}

void freeMessage(ExecMessage* msg) {
  if (msg && poolMutex && xSemaphoreTake(poolMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    msg->inUse = false;
    msg->length = 0;
    xSemaphoreGive(poolMutex);
  }
}

void systemTask(void* param) {
  (void)param;
  esp_task_wdt_add(NULL);  /* Register this task with watchdog timer */
  bool ledState = false;
  uint32_t ledTs = 0;
  uint32_t lastNtpCheck = 0;
  uint32_t lastWiFiCheck = 0;
  pinMode(BLE_LED_PIN, OUTPUT);

  digitalWrite(BLE_LED_PIN, LED_INVERTED ? HIGH : LOW);

  for (;;) {
    esp_task_wdt_reset();

    if (!bootComplete) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

#if ENABLE_OTA
    if (otaInProgress) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
#endif

    uint32_t now = millis();
    
    bool isConnected = (WiFi.status() == WL_CONNECTED);
    uint32_t wifiCheckInterval = isConnected ? 2000 : 500;
    
    if (now - lastWiFiCheck >= wifiCheckInterval) {
      lastWiFiCheck = now;
      checkWiFiConnection();
    }

    if (isConnected) {
      if (now - lastNtpCheck > 5000) {
        lastNtpCheck = now;
        if (shouldSyncNTP()) {
          syncNTP();
        }
      }
    }

    handleBLEReconnect();

    updateCpuLoad();

#if DEBUG_MODE
    checkTaskStacks();
#endif

    if (bleDeviceConnected) {
      uint32_t now = millis();
      if (now - ledTs > BLE_LED_BLINK_MS) {
        ledTs = now;
        ledState = !ledState;
        digitalWrite(BLE_LED_PIN,
                     LED_INVERTED ? (ledState ? LOW : HIGH) : (ledState ? HIGH : LOW));
      }
    } else {
      digitalWrite(BLE_LED_PIN, LED_INVERTED ? HIGH : LOW);
      ledState = false;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void webTask(void* param) {
  (void)param;
  esp_task_wdt_add(NULL);  /* Register this task with watchdog timer */

  for (;;) {
#if ENABLE_OTA
    if (webTaskShouldExit) {
      Serial.println(F("webTask: Received exit signal, cleaning up..."));
      esp_task_wdt_delete(NULL);
      webTaskHandle = NULL;
      Serial.println(F("webTask: Exiting"));
      vTaskDelete(NULL);
      return;
    }
#endif

    esp_task_wdt_reset();
    

    if (serverStarted) {
      server.handleClient();
    }

#if DEBUG_MODE
    if (!isOtaActive()) {
      updateTaskMonitoring();
    } else {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
#endif

    vTaskDelay(pdMS_TO_TICKS(10));  /* Yield to other tasks (10ms sleep) */
  }
}

void bizTask(void* param) {
  (void)param;
  esp_task_wdt_add(NULL);  /* Register this task with watchdog timer */
  ExecMessage* msg = nullptr;

  for (;;) {
#if ENABLE_OTA
    if (bizTaskShouldExit) {
      Serial.println(F("bizTask: Received exit signal, cleaning up..."));
      if (msg) {
        freeMessage(msg);
        msg = nullptr;
      }
      esp_task_wdt_delete(NULL);
      bizTaskHandle = NULL;
      Serial.println(F("bizTask: Exiting"));
      vTaskDelete(NULL);
      return;
    }
#endif

    esp_task_wdt_reset();

    if (gBizState == BIZ_RUNNING && !isOtaActive()) {
      if (execQ && xQueueReceive(execQ, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {

        Serial.println("\n[bizTask] Received a message from queue.");

        if (msg) {
          Serial.printf("[bizTask] Message payload: '%s' (Length: %d)\n", msg->payload, msg->length);

          String cmd = String(msg->payload);
          cmd.toLowerCase();

          if (cmd.equals("reset") || cmd.equals("reboot")) {
            Serial.println("[bizTask] Command MATCHED 'reset' or 'reboot'.");
            Serial.println(F("bizTask: Reboot command received. Restarting in 500ms..."));

            freeMessage(msg);
            msg = nullptr;
            vTaskDelay(pdMS_TO_TICKS(500));
            #if DEBUG_MODE
            prefs.putBool(NVS_FLAG_USER_REBOOT, true);
            prefs.end();
            delay(50);
            prefs.begin("esp32_base", false);
            #endif
            ESP.restart();
          } else {
            Serial.printf("[bizTask] Command '%s' did NOT match reboot logic. Processing as other command.\n", cmd.c_str());
            vTaskDelay(pdMS_TO_TICKS(50));
            bizProcessed++;
            freeMessage(msg);
            msg = nullptr;
          }
        } else {
          Serial.println("[bizTask] Received a NULL message from queue. This should not happen.");
          LOG_ERROR(F("bizTask: NULL message received"), millis() / 1000);
        }
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}