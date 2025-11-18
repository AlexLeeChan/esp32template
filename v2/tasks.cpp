#include "tasks.h"
#include "globals.h"
#include "wifi_handler.h"
#include "ble_handler.h"
#include "time_handler.h"
#include "debug_handler.h"
#include "web_handler.h"
#include "cpu_monitor.h"
#include <esp_task_wdt.h>

#if ENABLE_OTA
  #include <esp_ota_ops.h>
#endif

void initMessagePool() {
  poolMutex = xSemaphoreCreateMutex();
  if (!poolMutex) {
    Serial.println(F("CRITICAL: Failed to create poolMutex!"));
    LOG_ERROR(F("Failed to create poolMutex"), 0);
  }

  wifiMutex = xSemaphoreCreateMutex();
  if (!wifiMutex) {
    Serial.println(F("CRITICAL: Failed to create wifiMutex!"));
    LOG_ERROR(F("Failed to create wifiMutex"), 0);
  }

  for (int i = 0; i < MSG_POOL_SIZE; i++) {
    msgPool[i].inUse = false;
    msgPool[i].length = 0;
  }

#if ENABLE_OTA
  otaMutex = xSemaphoreCreateMutex();
  if (!otaMutex) {
    Serial.println(F("CRITICAL: Failed to create otaMutex!"));
    LOG_ERROR(F("Failed to create otaMutex"), 0);
  }

  taskDeletionMutex = xSemaphoreCreateMutex();
  if (!taskDeletionMutex) {
    Serial.println(F("CRITICAL: Failed to create taskDeletionMutex!"));
    LOG_ERROR(F("Failed to create taskDeletionMutex"), 0);
  }

  tasksDeleted = false;
  webTaskShouldExit = false;
  bizTaskShouldExit = false;
  otaInProgress = false;

  const esp_partition_t* ota_partition = esp_ota_get_next_update_partition(NULL);
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
  LOG_ERROR(F("Message pool exhausted"), millis() / 1000);
  return nullptr;
}

void freeMessage(ExecMessage* msg) {
  if (msg && xSemaphoreTake(poolMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    msg->inUse = false;
    msg->length = 0;
    xSemaphoreGive(poolMutex);
  }
}

void systemTask(void* param) {
  (void)param;
  esp_task_wdt_add(NULL);
  bool ledState = false;
  uint32_t ledTs = 0;
  uint32_t lastNtpCheck = 0;
  uint32_t lastWiFiCheck = 0;
  pinMode(BLE_LED_PIN, OUTPUT);

  digitalWrite(BLE_LED_PIN, LED_INVERTED ? HIGH : LOW);

  for (;;) {
    esp_task_wdt_reset();

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

    // Always update CPU load (not just in DEBUG_MODE)
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
  esp_task_wdt_add(NULL);

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
    server.handleClient();

#if DEBUG_MODE
    if (!isOtaActive()) {
      updateTaskMonitoring();
    } else {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
#endif

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void bizTask(void* param) {
  (void)param;
  esp_task_wdt_add(NULL);
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
            prefs.putBool("userRebootRequested", true);
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