/* ==============================================================================
   OTA_HANDLER.CPP - Over-The-Air Update Implementation
   
   Implements firmware update functionality:
   1. Downloads new firmware from URL (HTTP/HTTPS)
   2. Validates file size and partition availability
   3. Flashes firmware to inactive partition
   4. Verifies update and triggers reboot on success
   
   Process involves stopping non-essential tasks during update to free memory
   and ensure stability. Progress is reported via mutex-protected status structure.
   ============================================================================== */

#include "ota_handler.h"

#if ENABLE_OTA

/* OTA (Over-The-Air) update functionality is enabled */

#include "globals.h"
#include "web_handler.h"
#include "debug_handler.h"
#include "tasks.h"
#include <Update.h>
#include <esp_ota_ops.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_partition.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>

static void otaTaskFunction(void* param);

/* dumpPartitionInfo: Lists all flash partitions and identifies the active/next OTA partitions */
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

/* deleteNonEssentialTasks: Safely stops business logic task before OTA update to free memory */
void deleteNonEssentialTasks() {
  /* Acquire taskDeletionMutex mutex (wait up to 500ms) to safely access shared resource */
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

/* deleteWebTask: Stops web server task before final OTA flash to ensure stability */
void deleteWebTask() {
  /* Acquire taskDeletionMutex mutex (wait up to 500ms) to safely access shared resource */
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

/* recreateTasks: Restarts tasks after OTA failure to restore normal operation */
void recreateTasks() {
  /* Acquire taskDeletionMutex mutex (wait up to 500ms) to safely access shared resource */
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

/* handleOTAStatus: API endpoint that returns current OTA update status and partition info */

/* ============================================================================
   API ENDPOINTS - Functions called by HTTP requests
   ============================================================================ */

/* handleOTAStatus: API endpoint that returns current OTA update status and partition info */
void handleOTAStatus() {
  DynamicJsonDocument doc(512);

  const esp_partition_t* ota_partition = esp_ota_get_next_update_partition(NULL);
  otaStatus.available = (ota_partition != NULL);

  /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
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
  /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
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

/* handleOTAUpdate: Main API endpoint that initiates OTA update process from provided URL */
void handleOTAUpdate() {

  if (!server.hasArg("plain")) {
    server.send(400, F("application/json"), F("{\"err\":\"no body\"}"));
    LOG_ERROR(F("OTA: No request body"), millis() / 1000);
    return;
  }

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, F("application/json"), F("{\"err\":\"bad json\"}"));
    LOG_ERROR(F("OTA: Bad JSON"), millis() / 1000);
    return;
  }

  const char* url = doc["url"] | "";
  if (strlen(url) == 0) {
    server.send(400, F("application/json"), F("{\"err\":\"url required\"}"));
    LOG_ERROR(F("OTA: No URL provided"), millis() / 1000);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, F("application/json"), F("{\"err\":\"WiFi not connected\"}"));
    LOG_ERROR(F("OTA: WiFi not connected"), millis() / 1000);
    return;
  }

  /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
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

  server.send(200, F("application/json"), F("{\"msg\":\"update started\"}"));
  server.client().flush();
  delay(100);

  
  Serial.println(F("\n=== Pre-OTA Memory Cleanup ==="));
  Serial.printf("Initial Free Heap: %u bytes\n", ESP.getFreeHeap());

  #if ESP32_HAS_BLE
  Serial.println(F("Deinitializing BLE..."));
  NimBLEDevice::deinit();
  vTaskDelay(pdMS_TO_TICKS(200));
  Serial.printf("After BLE deinit: %u bytes\n", ESP.getFreeHeap());
  #endif

  Serial.println(F("Deleting non-essential tasks..."));
  /* Free memory by stopping business logic task (OTA needs ~150KB free) */
  deleteNonEssentialTasks();
  vTaskDelay(pdMS_TO_TICKS(300));
  Serial.printf("After task deletion: %u bytes\n", ESP.getFreeHeap());

  otaInProgress = true;

  String* otaUrlPtr = new String(url);

  uint32_t freeHeap = ESP.getFreeHeap();
  Serial.printf("Final Free Heap: %u bytes\n", freeHeap);
  Serial.println(F("=== Cleanup Complete ===\n"));

  if (freeHeap < 35000) {
    Serial.println(F("ERROR: Insufficient memory for OTA!"));
    /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      otaStatus.state = OTA_FAILED;
      otaStatus.error = F("Insufficient memory");
      xSemaphoreGive(otaMutex);
    }
    LOG_ERROR(F("OTA: Insufficient memory"), millis() / 1000);
    delete otaUrlPtr;
    

    otaInProgress = false;
    recreateTasks();
    return;
  }

  BaseType_t taskCreated = xTaskCreate(
    otaTaskFunction,
    "ota_task",
    20480,
    (void*)otaUrlPtr,
    3,
    NULL
  );

  if (taskCreated != pdPASS) {
    Serial.println(F("CRITICAL: Failed to create OTA task!"));

    /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      otaStatus.state = OTA_FAILED;
      otaStatus.error = F("Task creation failed");
      xSemaphoreGive(otaMutex);
    }
    LOG_ERROR(F("OTA: Task creation failed"), millis() / 1000);

    delete otaUrlPtr;
  }
}

/* ============================================================================
   OTA BACKGROUND TASK
   
   Background task that downloads and flashes new firmware:
   1. Connects to update server (HTTP/HTTPS with auto-redirect)
   2. Downloads firmware file
   3. Flashes to inactive partition
   4. Verifies integrity and reboots on success
   
   Runs as separate FreeRTOS task to avoid blocking the web server during
   the potentially lengthy download process.
   ============================================================================ */

static void otaTaskFunction(void* param) {

  esp_task_wdt_add(NULL);
  esp_task_wdt_reset();

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

  server.on("/api/ota/status", HTTP_GET, handleOTAStatus);
  server.on("/api/ota/reset", HTTP_POST, handleOTAReset);
  server.on("/api/ota/update", HTTP_POST, handleOTAUpdate);

  
  /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    otaStatus.state = OTA_DOWNLOADING;
    otaStatus.progress = 1;
    xSemaphoreGive(otaMutex);
  }

  esp_wifi_set_ps(WIFI_PS_NONE);
  Serial.println(F("WiFi power save disabled"));

  vTaskDelay(pdMS_TO_TICKS(200));
  esp_task_wdt_reset();

  HTTPClient httpClient;
  WiFiClientSecure clientSecure;
  WiFiClient client;

  bool isSecure = url.startsWith("https://");

  clientSecure.setInsecure();
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

  Serial.println(F("Sending HTTP GET request..."));
  int httpCode = httpClient.GET();

  if (httpCode == -1 && !isSecure) {
    Serial.println(F("HTTP connection refused. Retrying with HTTPS..."));
    
    httpClient.end();
    client.stop();
    vTaskDelay(pdMS_TO_TICKS(200));
    

    String httpsUrl = url;
    httpsUrl.replace("http://", "https://");
    
    Serial.printf("Retrying with HTTPS: %s\n", httpsUrl.c_str());
    

    clientSecure.setInsecure();
    clientSecure.setTimeout(30);
    httpClient.begin(clientSecure, httpsUrl);
    httpClient.setUserAgent(F("ESP32-OTA/1.0"));
    httpClient.setTimeout(15000);
    httpClient.setReuse(false);
    
    isSecure = true;
    
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_task_wdt_reset();
    
    Serial.println(F("Sending HTTPS GET request..."));
    httpCode = httpClient.GET();
    Serial.printf("HTTPS attempt result: HTTP code %d\n", httpCode);
  }

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

  if (httpCode != HTTP_CODE_OK) {
    char errStr[200];
    String location = httpClient.getLocation();
    if (location.length() > 0 && httpCode >= 300 && httpCode < 400) {
      snprintf(errStr, sizeof(errStr), "HTTP %d -> %s", httpCode, location.c_str());
    } else if (httpCode == -1) {

      if (isSecure) {
        snprintf(errStr, sizeof(errStr), "Connection refused (tried HTTPS). Check URL.");
      } else {
        snprintf(errStr, sizeof(errStr), "Connection refused. Check URL and network.");
      }
    } else if (httpCode == -2) {
      snprintf(errStr, sizeof(errStr), "Send header failed. Check WiFi connection.");
    } else if (httpCode == -3) {
      snprintf(errStr, sizeof(errStr), "Connection lost during request.");
    } else if (httpCode == -11) {
      snprintf(errStr, sizeof(errStr), "Read timeout. Server not responding.");
    } else {
      snprintf(errStr, sizeof(errStr), "HTTP %d: %s",
               httpCode, httpClient.errorToString(httpCode).c_str());
    }
    Serial.printf("ERROR: %s\n", errStr);

    /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      otaStatus.state = OTA_FAILED;
      otaStatus.error = errStr;
      xSemaphoreGive(otaMutex);
    }
    LOG_ERROR(String("OTA: ") + errStr, millis() / 1000);

    otaInProgress = false;
    httpClient.end();
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_task_wdt_delete(NULL);
    recreateTasks();
    vTaskDelete(NULL);
    return;
  }

  int contentLength = httpClient.getSize();
  if (contentLength <= 0) {
    const char* msg = "Invalid content length";
    Serial.println(F("ERROR: Invalid Content-Length"));

    /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      otaStatus.state = OTA_FAILED;
      otaStatus.error = msg;
      xSemaphoreGive(otaMutex);
    }
    LOG_ERROR(String("OTA: ") + msg, millis() / 1000);

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

  /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    otaStatus.fileSize = contentLength;
    xSemaphoreGive(otaMutex);
  }

  esp_task_wdt_reset();

  const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
  if (!update_partition) {
    const char* msg = "No OTA partition";
    Serial.println(F("ERROR: No OTA partition available!"));

    /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      otaStatus.state = OTA_FAILED;
      otaStatus.error = msg;
      xSemaphoreGive(otaMutex);
    }
    LOG_ERROR(String("OTA: ") + msg, millis() / 1000);

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

    /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      otaStatus.state = OTA_FAILED;
      otaStatus.error = errStr;
      xSemaphoreGive(otaMutex);
    }
    LOG_ERROR(String("OTA: ") + errStr, millis() / 1000);

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

  Update.abort();
  vTaskDelay(pdMS_TO_TICKS(100));
  esp_task_wdt_reset();

  Serial.println(F("\nStarting Update.begin()..."));
  if (!Update.begin(contentLength, U_FLASH)  /* Initialize OTA flash writer with file size */) {
    char errStr[128];
    snprintf(errStr, sizeof(errStr), "Update.begin() error: %u", Update.getError());
    Serial.printf("ERROR: %s\n", errStr);
    Serial.println(Update.errorString());

    /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      otaStatus.state = OTA_FAILED;
      otaStatus.error = errStr;
      xSemaphoreGive(otaMutex);
    }
    LOG_ERROR(String("OTA: ") + errStr, millis() / 1000);

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

  while (written < (size_t)contentLength) {
    if (millis() - lastWdtReset > 5000) {
      esp_task_wdt_reset();
      lastWdtReset = millis();
    }

    if (WiFi.status() != WL_CONNECTED) {
      const char* msg = "WiFi disconnected";
      Serial.println(F("\nERROR: WiFi disconnected!"));

      /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
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
        /* Write downloaded chunk to flash - must write exactly len bytes */
        if (Update.write(buff, len) != (size_t)len) {
          char errStr[128];
          snprintf(errStr, sizeof(errStr),
                   "Write failed at %u/%d! Error: %u",
                   (unsigned)written, contentLength, Update.getError());
          Serial.printf("\nERROR: %s\n", errStr);

          /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
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

        if (millis() - lastProgress > 500 || written >= (size_t)contentLength) {
          lastProgress = millis();
          uint8_t progress = (written * 100) / contentLength;

          /* Acquire otaMutex mutex (wait up to 5ms) to safely access shared resource */
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
    vTaskDelay(1);
  }

  httpClient.end();
  Serial.printf("\nDownload complete! %u bytes written\n", (unsigned)written);

  Serial.println(F("Setting state to FLASHING"));
  /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    otaStatus.state = OTA_FLASHING;
    otaStatus.progress = 100;
    xSemaphoreGive(otaMutex);
  }

  Serial.println(F("Waiting 2s for UI to update..."));
  vTaskDelay(pdMS_TO_TICKS(2000));

  deleteWebTask();
  vTaskDelay(pdMS_TO_TICKS(200));
  esp_task_wdt_reset();

  Serial.println(F("Finalizing update..."));

  if (!Update.end(true)) {
    char errStr[128];
    snprintf(errStr, sizeof(errStr), "Update.end() error: %u", Update.getError());
    Serial.printf("ERROR: %s\n", errStr);

    /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      otaStatus.state = OTA_FAILED;
      otaStatus.error = errStr;
      xSemaphoreGive(otaMutex);
    }
    LOG_ERROR(String("OTA: ") + errStr, millis() / 1000);

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

    /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      otaStatus.state = OTA_FAILED;
      otaStatus.error = msg;
      xSemaphoreGive(otaMutex);
    }
    LOG_ERROR(String("OTA: ") + msg, millis() / 1000);

    otaInProgress = false;
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_task_wdt_delete(NULL);
    recreateTasks();
    vTaskDelete(NULL);
    return;
  }

Serial.println(F("\n==========================================="));
Serial.println(F("         OTA UPDATE SUCCESS!"));
Serial.println(F("==========================================="));
  Serial.println(F("Rebooting in 2 seconds...\n"));

  /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
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
  ESP.restart();  /* Reboot to boot from newly flashed partition */
}

#endif