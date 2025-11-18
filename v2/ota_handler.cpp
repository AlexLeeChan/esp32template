#include "ota_handler.h"

#if ENABLE_OTA

#include "globals.h"
#include "web_handler.h"
#include "debug_handler.h"
#include "tasks.h"  // ADD THIS - for task function declarations
#include <Update.h>
#include <esp_ota_ops.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_partition.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>  // ADD THIS - for esp_wifi_set_ps
#include <ArduinoJson.h>  // ADD THIS - for JSON handling

// Forward declaration for OTA task function
static void otaTaskFunction(void* param);

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

void dumpPartitionInfo(String& logOutput) {
  logOutput = "--- Partition Table ---\n";
  const esp_partition_t* running = esp_ota_get_running_partition();

  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
  if (!it) {
    logOutput += "Failed to find partitions!\n";
    LOG_ERROR(F("Partition iteration failed"), millis() / 1000);
    return;
  }

  char buf[256];
  while (it != NULL) {
    const esp_partition_t* p = esp_partition_get(it);
    if (!p) {
      it = esp_partition_next(it);
      continue;
    }

    snprintf(buf, sizeof(buf),
             "Label: %-16s | Type: 0x%02x | Sub: 0x%02x | Addr: 0x%08x | Size: %u (%.2fMB) %s\n",
             p->label, p->type, p->subtype,
             p->address, p->size, (float)p->size / 1024.0 / 1024.0,
             (p == running) ? "<- RUNNING" : "");
    logOutput += buf;

    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);

  const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
  if (next) {
    snprintf(buf, sizeof(buf), "Next OTA Partition: %s\n", next->label);
    logOutput += buf;
  } else {
    logOutput += "Next OTA Partition: NOT FOUND!\n";
    LOG_ERROR(F("No OTA partition available"), millis() / 1000);
  }
}

void deleteNonEssentialTasks() {
  if (xSemaphoreTake(taskDeletionMutex, pdMS_TO_TICKS(500)) == pdTRUE) {

    if (bizTaskHandle != NULL) {
      Serial.println(F("\n=== Stopping bizTask for OTA ==="));
      gBizState = BIZ_STOPPED;
      vTaskDelay(pdMS_TO_TICKS(200));
      bizTaskShouldExit = true;

      uint8_t waitCount = 0;
      while (bizTaskHandle != NULL && waitCount < 30) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waitCount++;
      }

      if (bizTaskHandle != NULL) {
        Serial.println(F("Warning: bizTask didn't exit gracefully, force deleting..."));
        LOG_ERROR(F("bizTask force deleted (OTA)"), millis() / 1000);
        vTaskDelete(bizTaskHandle);
        bizTaskHandle = NULL;
      } else {
        Serial.println(F("bizTask exited gracefully"));
      }
      Serial.println(F("=== bizTask Deletion Complete ===\n"));
    }

    vTaskDelay(pdMS_TO_TICKS(200));

    tasksDeleted = true;
    xSemaphoreGive(taskDeletionMutex);
  } else {
    LOG_ERROR(F("Failed to acquire taskDeletionMutex"), millis() / 1000);
  }
}

void deleteWebTask() {
  if (xSemaphoreTake(taskDeletionMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    if (webTaskHandle != NULL) {
      Serial.println(F("\n=== Stopping webTask for OTA Flash ==="));
      webTaskShouldExit = true;

      uint8_t waitCount = 0;
      while (webTaskHandle != NULL && waitCount < 20) {
        vTaskDelay(pdMS_TO_TICKS(50));
        waitCount++;
      }

      if (webTaskHandle != NULL) {
        Serial.println(F("Warning: webTask didn't exit gracefully, force deleting..."));
        LOG_ERROR(F("webTask force deleted (OTA)"), millis() / 1000);
        vTaskDelete(webTaskHandle);
        webTaskHandle = NULL;
      } else {
        Serial.println(F("webTask exited gracefully"));
      }
      Serial.println(F("=== webTask Deletion Complete ===\n"));
    }
    xSemaphoreGive(taskDeletionMutex);
  } else {
    LOG_ERROR(F("Failed to acquire taskDeletionMutex (web)"), millis() / 1000);
  }
}

void recreateTasks() {
  if (xSemaphoreTake(taskDeletionMutex, pdMS_TO_TICKS(500)) == pdTRUE) {

    Serial.println(F("\n=== Recreating Tasks After OTA Failure ==="));

    webTaskShouldExit = false;
    bizTaskShouldExit = false;

    if (bizTaskHandle == NULL) {
      Serial.println(F("Creating bizTask..."));
      #if NUM_CORES > 1
      BaseType_t result = xTaskCreatePinnedToCore(bizTask, "biz", 4096, nullptr, 1, &bizTaskHandle, 1);
      #else
      BaseType_t result = xTaskCreate(bizTask, "biz", 4096, nullptr, 1, &bizTaskHandle);
      #endif
      if (result == pdPASS) {
        Serial.println(F("bizTask created"));
      } else {
        Serial.println(F("FAILED to create bizTask!"));
        LOG_ERROR(F("Failed to recreate bizTask"), millis() / 1000);
      }
    }

    if (webTaskHandle == NULL) {
      Serial.println(F("Creating webTask..."));
      #if NUM_CORES > 1
      BaseType_t result = xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, &webTaskHandle, 0);
      #else
      BaseType_t result = xTaskCreate(webTask, "web", 8192, nullptr, 1, &webTaskHandle);
      #endif
      if (result == pdPASS) {
        Serial.println(F("webTask created"));
      } else {
        Serial.println(F("FAILED to create webTask!"));
        LOG_ERROR(F("Failed to recreate webTask"), millis() / 1000);
      }
    }

    tasksDeleted = false;
    xSemaphoreGive(taskDeletionMutex);

    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.println(F("=== Task Recreation Complete ===\n"));
  } else {
    LOG_ERROR(F("Failed to acquire taskDeletionMutex (recreate)"), millis() / 1000);
  }
}

// ============================================================================
// API HANDLERS
// ============================================================================

void handleOTAStatus() {
  DynamicJsonDocument doc(512);

  const esp_partition_t* ota_partition = esp_ota_get_next_update_partition(NULL);
  otaStatus.available = (ota_partition != NULL);

  if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    doc["available"] = otaStatus.available;
    doc["state"] = (uint8_t)otaStatus.state;
    doc["progress"] = otaStatus.progress;
    doc["error"] = otaStatus.error;
    doc["file_size"] = otaStatus.fileSize;

    if (otaStatus.state == OTA_IDLE || otaStatus.state == OTA_FAILED) {
      const esp_partition_t* running = esp_ota_get_running_partition();
      if (running) {
        doc["current_partition"] = running->label;
      }
      if (ota_partition) {
        doc["next_partition"] = ota_partition->label;
        doc["partition_size"] = ota_partition->size;
      }
    }
    xSemaphoreGive(otaMutex);
  }

  doc["sketch_size"] = ESP.getSketchSize();
  doc["free_sketch_space"] = ESP.getFreeSketchSpace();
  doc["sketch_md5"] = ESP.getSketchMD5();

  String output;
  output.reserve(512);
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleOTAInfo() {
  String partitionInfo;
  dumpPartitionInfo(partitionInfo);
  server.send(200, F("text/plain"), partitionInfo);
}

void handleOTAReset() {
  if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    otaStatus.state = OTA_IDLE;
    otaStatus.progress = 0;
    otaStatus.error = "";
    xSemaphoreGive(otaMutex);
  }

  recreateTasks();

  server.send(200, "application/json", "{\"msg\":\"OTA status reset and tasks restarted\"}");
}

void registerOtaRoutes() {
  server.on("/api/ota/status", HTTP_GET, handleOTAStatus);
  server.on("/api/ota/update", HTTP_POST, handleOTAUpdate);
  server.on("/api/ota/reset", HTTP_POST, handleOTAReset);
  server.on("/api/ota/info", HTTP_GET, handleOTAInfo);
}

// ============================================================================
// MAIN OTA UPDATE HANDLER
// ============================================================================

void handleOTAUpdate() {
  // Check for request body
  if (!server.hasArg("plain")) {
    server.send(400, F("application/json"), F("{\"err\":\"no body\"}"));
    LOG_ERROR(F("OTA: No request body"), millis() / 1000);
    return;
  }

  // Parse JSON
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, F("application/json"), F("{\"err\":\"bad json\"}"));
    LOG_ERROR(F("OTA: Bad JSON"), millis() / 1000);
    return;
  }

  // Validate URL
  const char* url = doc["url"] | "";
  if (strlen(url) == 0) {
    server.send(400, F("application/json"), F("{\"err\":\"url required\"}"));
    LOG_ERROR(F("OTA: No URL provided"), millis() / 1000);
    return;
  }

  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, F("application/json"), F("{\"err\":\"WiFi not connected\"}"));
    LOG_ERROR(F("OTA: WiFi not connected"), millis() / 1000);
    return;
  }

  // Check if OTA is already in progress
  if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (otaStatus.state != OTA_IDLE) {
      xSemaphoreGive(otaMutex);
      server.send(409, F("application/json"), F("{\"err\":\"update in progress\"}"));
      LOG_ERROR(F("OTA: Already in progress"), millis() / 1000);
      return;
    }
    otaStatus.state = OTA_CHECKING;
    otaStatus.progress = 0;
    otaStatus.error = "";
    otaStatus.fileSize = 0;
    xSemaphoreGive(otaMutex);
  }

  // Send "OK" response immediately
  server.send(200, F("application/json"), F("{\"msg\":\"update started\"}"));
  server.client().flush();
  delay(100);

  // Create a heap-allocated copy of the URL for the new task
  String* otaUrlPtr = new String(url);

  uint32_t freeHeap = ESP.getFreeHeap();
  Serial.printf("Pre-OTA Free Heap: %u bytes\n", freeHeap);

  // Check heap before starting task
  if (freeHeap < 50000) {
    Serial.println(F("ERROR: Insufficient memory for OTA!"));
    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      otaStatus.state = OTA_FAILED;
      otaStatus.error = F("Insufficient memory");
      xSemaphoreGive(otaMutex);
    }
    LOG_ERROR(F("OTA: Insufficient memory"), millis() / 1000);
    delete otaUrlPtr;
    return;
  }

  // Create the dedicated OTA task
  BaseType_t taskCreated = xTaskCreate(
    otaTaskFunction,
    "ota_task",
    24576,
    (void*)otaUrlPtr,
    3,
    NULL
  );

  if (taskCreated != pdPASS) {
    Serial.println(F("CRITICAL: Failed to create OTA task!"));

    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      otaStatus.state = OTA_FAILED;
      otaStatus.error = F("Task creation failed");
      xSemaphoreGive(otaMutex);
    }
    LOG_ERROR(F("OTA: Task creation failed"), millis() / 1000);

    delete otaUrlPtr;
  }
}

// ============================================================================
// OTA TASK FUNCTION (runs in dedicated task)
// ============================================================================

static void otaTaskFunction(void* param) {
  // --- This code runs in the new 'ota_task' ---
  esp_task_wdt_add(NULL);
  esp_task_wdt_reset();

  // Get URL and free the heap allocation
  String* urlPtr = (String*)param;
  String url = *urlPtr;
  delete urlPtr;

Serial.println(F("\n==========================================="));
Serial.println(F("         OTA UPDATE STARTED"));
Serial.println(F("==========================================="));
  Serial.printf("URL: %s\n", url.c_str());
  Serial.printf("Free Heap: %u bytes\n\n", ESP.getFreeHeap());

  vTaskDelay(pdMS_TO_TICKS(300));
  esp_task_wdt_reset();

  // Re-register OTA routes in case they were lost
  server.on("/api/ota/status", HTTP_GET, handleOTAStatus);
  server.on("/api/ota/reset", HTTP_POST, handleOTAReset);
  server.on("/api/ota/update", HTTP_POST, handleOTAUpdate);

  // Stop other tasks to free up heap and prevent conflicts
  deleteNonEssentialTasks();
  esp_task_wdt_reset();

  #if ESP32_HAS_BLE
  Serial.println(F("Stopping BLE to free heap..."));
  NimBLEDevice::deinit();
  vTaskDelay(pdMS_TO_TICKS(100));
  Serial.printf("Heap after BLE deinit: %u bytes\n", ESP.getFreeHeap());
  #endif

  // Set global flag to pause WiFi management in systemTask
  otaInProgress = true;
  Serial.println(F("OTA in progress flag set - WiFi management disabled"));

  if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    otaStatus.state = OTA_DOWNLOADING;
    otaStatus.progress = 1;
    xSemaphoreGive(otaMutex);
  }

  // Disable WiFi power save for max download speed
  esp_wifi_set_ps(WIFI_PS_NONE);
  Serial.println(F("WiFi power save disabled"));

  vTaskDelay(pdMS_TO_TICKS(200));
  esp_task_wdt_reset();

  HTTPClient httpClient;
  WiFiClientSecure clientSecure;
  WiFiClient client;

  bool isSecure = url.startsWith("https://");

  // Configure clients
  clientSecure.setInsecure(); // Allow self-signed certs
  clientSecure.setTimeout(30);
  client.setTimeout(30);

  if (isSecure) {
    httpClient.begin(clientSecure, url);
    Serial.println(F("Using HTTPS connection"));
  } else {
    httpClient.begin(client, url);
    Serial.println(F("Using HTTP connection"));
  }

  httpClient.setUserAgent(F("ESP32-OTA/1.0"));
  httpClient.setTimeout(15000);
  httpClient.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpClient.setReuse(false);

  esp_task_wdt_reset();

  // --- 1. HTTP GET Request ---
  Serial.println(F("Sending HTTP GET request..."));
  int httpCode = httpClient.GET();

  // Handle redirects manually (more robust)
  if (httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND ||
      httpCode == HTTP_CODE_SEE_OTHER || httpCode == HTTP_CODE_TEMPORARY_REDIRECT) {

    String newLocation = httpClient.getLocation();
    Serial.printf("Redirect %d to: %s\n", httpCode, newLocation.c_str());
    LOG_ERROR(String("OTA: Redirect ") + httpCode, millis() / 1000);

    httpClient.end();
    if (isSecure) clientSecure.stop(); else client.stop();

    vTaskDelay(pdMS_TO_TICKS(300));
    esp_task_wdt_reset();

    bool newIsSecure = newLocation.startsWith("https://");

    if (newIsSecure) {
      clientSecure.setInsecure();
      clientSecure.setTimeout(30);
      httpClient.begin(clientSecure, newLocation);
    } else {
      client.setTimeout(30);
      httpClient.begin(client, newLocation);
    }

    isSecure = newIsSecure;
    httpClient.setUserAgent(F("ESP32-OTA/1.0"));
    httpClient.setTimeout(15000);
    httpClient.setReuse(false);

    vTaskDelay(pdMS_TO_TICKS(100));
    esp_task_wdt_reset();

    Serial.println(F("Sending redirected GET request..."));
    httpCode = httpClient.GET();
    Serial.printf("After redirect, HTTP code: %d\n", httpCode);
  }

  // Check for HTTP OK
  if (httpCode != HTTP_CODE_OK) {
    char errStr[160];
    String location = httpClient.getLocation();
    if (location.length() > 0 && httpCode >= 300 && httpCode < 400) {
      snprintf(errStr, sizeof(errStr), "HTTP %d -> %s", httpCode, location.c_str());
    } else {
      snprintf(errStr, sizeof(errStr), "HTTP %d: %s",
               httpCode, httpClient.errorToString(httpCode).c_str());
    }
    Serial.printf("ERROR: %s\n", errStr);

    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      otaStatus.state = OTA_FAILED;
      otaStatus.error = errStr;
      xSemaphoreGive(otaMutex);
    }
    LOG_ERROR(String("OTA: ") + errStr, millis() / 1000);

    // --- Failure Cleanup ---
    otaInProgress = false;
    httpClient.end();
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_task_wdt_delete(NULL);
    recreateTasks();
    vTaskDelete(NULL);
    return;
  }

  // --- 2. Check Content Length ---
  int contentLength = httpClient.getSize();
  if (contentLength <= 0) {
    const char* msg = "Invalid content length";
    Serial.println(F("ERROR: Invalid Content-Length"));

    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      otaStatus.state = OTA_FAILED;
      otaStatus.error = msg;
      xSemaphoreGive(otaMutex);
    }
    LOG_ERROR(String("OTA: ") + msg, millis() / 1000);

    // --- Failure Cleanup ---
    otaInProgress = false;
    httpClient.end();
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_task_wdt_delete(NULL);
    recreateTasks();
    vTaskDelete(NULL);
    return;
  }

  Serial.printf("HTTP OK - Content-Length: %d bytes (%.2f MB)\n",
                contentLength, contentLength / 1048576.0);

  if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    otaStatus.fileSize = contentLength;
    xSemaphoreGive(otaMutex);
  }

  esp_task_wdt_reset();

  // --- 3. Check Partition Availability & Size ---
  const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
  if (!update_partition) {
    const char* msg = "No OTA partition";
    Serial.println(F("ERROR: No OTA partition available!"));

    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      otaStatus.state = OTA_FAILED;
      otaStatus.error = msg;
      xSemaphoreGive(otaMutex);
    }
    LOG_ERROR(String("OTA: ") + msg, millis() / 1000);

    // --- Failure Cleanup ---
    otaInProgress = false;
    httpClient.end();
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_task_wdt_delete(NULL);
    recreateTasks();
    vTaskDelete(NULL);
    return;
  }

  if (contentLength > (int)update_partition->size) {
    char errStr[128];
    snprintf(errStr, sizeof(errStr),
             "File too large! %d > %u bytes",
             contentLength, update_partition->size);
    Serial.println(errStr);

    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      otaStatus.state = OTA_FAILED;
      otaStatus.error = errStr;
      xSemaphoreGive(otaMutex);
    }
    LOG_ERROR(String("OTA: ") + errStr, millis() / 1000);

    // --- Failure Cleanup ---
    otaInProgress = false;
    httpClient.end();
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_task_wdt_delete(NULL);
    recreateTasks();
    vTaskDelete(NULL);
    return;
  }

  Serial.printf("Target partition: %s (%.2f MB)\n",
                update_partition->label, update_partition->size / 1048576.0);

  // --- 4. Begin Update (Download & Flash) ---
  Update.abort(); // Ensure no previous update is pending
  vTaskDelay(pdMS_TO_TICKS(100));
  esp_task_wdt_reset();

  Serial.println(F("\nStarting Update.begin()..."));
  if (!Update.begin(contentLength, U_FLASH)) {
    char errStr[128];
    snprintf(errStr, sizeof(errStr), "Update.begin() error: %u", Update.getError());
    Serial.printf("ERROR: %s\n", errStr);
    Serial.println(Update.errorString());

    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      otaStatus.state = OTA_FAILED;
      otaStatus.error = errStr;
      xSemaphoreGive(otaMutex);
    }
    LOG_ERROR(String("OTA: ") + errStr, millis() / 1000);

    // --- Failure Cleanup ---
    otaInProgress = false;
    httpClient.end();
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_task_wdt_delete(NULL);
    recreateTasks();
    vTaskDelete(NULL);
    return;
  }

  Serial.println(F("Update.begin() successful!"));
  esp_task_wdt_reset();
  Serial.println(F("\n--- Starting Download & Flash ---"));

  WiFiClient* stream = isSecure ? (WiFiClient*)&clientSecure : &client;

  size_t written = 0;
  unsigned long lastProgress = 0;
  unsigned long lastWdtReset = 0;
  uint8_t lastPrintedPercent = 0;
  uint8_t buff[1024];

// --- Download and Flash Loop ---
  while (written < (size_t)contentLength) {
    if (millis() - lastWdtReset > 5000) {
      esp_task_wdt_reset();
      lastWdtReset = millis();
    }

    // Check for WiFi disconnect during download
    if (WiFi.status() != WL_CONNECTED) {
      const char* msg = "WiFi disconnected";
      Serial.println(F("\nERROR: WiFi disconnected!"));

      if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        otaStatus.state = OTA_FAILED;
        otaStatus.error = msg;
        xSemaphoreGive(otaMutex);
      }
      LOG_ERROR(String("OTA: ") + msg, millis() / 1000);

      Update.abort();
      otaInProgress = false;
      httpClient.end();
      esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
      esp_task_wdt_delete(NULL);
      recreateTasks();
      vTaskDelete(NULL);
      return;
    }

    size_t available = stream->available();
    if (available) {
      int len = stream->readBytes(buff, min(sizeof(buff), available));

      if (len > 0) {
        if (Update.write(buff, len) != (size_t)len) {
          char errStr[128];
          snprintf(errStr, sizeof(errStr),
                   "Write failed at %u/%d! Error: %u",
                   (unsigned)written, contentLength, Update.getError());
          Serial.printf("\nERROR: %s\n", errStr);

          if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            otaStatus.state = OTA_FAILED;
            otaStatus.error = errStr;
            xSemaphoreGive(otaMutex);
          }
          LOG_ERROR(String("OTA: ") + errStr, millis() / 1000);

          Update.abort();
          otaInProgress = false;
          httpClient.end();
          esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
          esp_task_wdt_delete(NULL);
          recreateTasks();
          vTaskDelete(NULL);
          return;
        }

        written += len;

        // Update progress
        if (millis() - lastProgress > 500 || written >= (size_t)contentLength) {
          lastProgress = millis();
          uint8_t progress = (written * 100) / contentLength;

          if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            otaStatus.progress = progress;
            xSemaphoreGive(otaMutex);
          }

          if (progress / 10 > lastPrintedPercent / 10) {
            Serial.printf("Progress: %u%% (%u/%d bytes)\n",
                          progress, (unsigned)written, contentLength);
            lastPrintedPercent = progress;
          }
        }
      }
    }
    vTaskDelay(1); // Yield for other tasks
  }

  httpClient.end();
  Serial.printf("\nDownload complete! %u bytes written\n", (unsigned)written);

  // --- 5. Finalize Update ---
  Serial.println(F("Setting state to FLASHING"));
  if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    otaStatus.state = OTA_FLASHING;
    otaStatus.progress = 100;
    xSemaphoreGive(otaMutex);
  }

  // Give web UI time to see the "FLASHING" state
  Serial.println(F("Waiting 2s for UI to update..."));
  vTaskDelay(pdMS_TO_TICKS(2000));

  // Stop the web server task (it's served its purpose)
  deleteWebTask();
  vTaskDelay(pdMS_TO_TICKS(200));
  esp_task_wdt_reset();

  Serial.println(F("Finalizing update..."));

  if (!Update.end(true)) {
    char errStr[128];
    snprintf(errStr, sizeof(errStr), "Update.end() error: %u", Update.getError());
    Serial.printf("ERROR: %s\n", errStr);

    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      otaStatus.state = OTA_FAILED;
      otaStatus.error = errStr;
      xSemaphoreGive(otaMutex);
    }
    LOG_ERROR(String("OTA: ") + errStr, millis() / 1000);

    // --- Failure Cleanup ---
    otaInProgress = false;
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_task_wdt_delete(NULL);
    recreateTasks();
    vTaskDelete(NULL);
    return;
  }

  if (!Update.isFinished()) {
    const char* msg = "Update incomplete";
    Serial.println(F("ERROR: Update not finished!"));

    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      otaStatus.state = OTA_FAILED;
      otaStatus.error = msg;
      xSemaphoreGive(otaMutex);
    }
    LOG_ERROR(String("OTA: ") + msg, millis() / 1000);

    // --- Failure Cleanup ---
    otaInProgress = false;
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_task_wdt_delete(NULL);
    recreateTasks();
    vTaskDelete(NULL);
    return;
  }

  // --- 6. Success! ---
Serial.println(F("\n==========================================="));
Serial.println(F("         OTA UPDATE SUCCESS!"));
Serial.println(F("==========================================="));
  Serial.println(F("Rebooting in 2 seconds...\n"));

  if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    otaStatus.state = OTA_SUCCESS;
    otaStatus.progress = 100;
    xSemaphoreGive(otaMutex);
  }

  otaInProgress = false;
  esp_task_wdt_delete(NULL);
  vTaskDelay(pdMS_TO_TICKS(2000));

  #if DEBUG_MODE
  prefs.putBool("userRebootRequested", true);
  #endif
  ESP.restart();
}

#endif // ENABLE_OTA