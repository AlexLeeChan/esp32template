#include "time_handler.h"
#include "globals.h"
#include <esp_sntp.h>
#include <time.h>
#include <sys/time.h>
#include "debug_handler.h"

void initNTP() {
  if (timeMutex == nullptr) {
    timeMutex = xSemaphoreCreateMutex();
    if (!timeMutex) {
      Serial.println(F("CRITICAL: Failed to create timeMutex!"));
      LOG_ERROR(F("Failed to create timeMutex"), 0);
      return;
    }
  }

  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, (char*)NTP_SERVER_1);
  sntp_setservername(1, (char*)NTP_SERVER_2);
  sntp_setservername(2, (char*)NTP_SERVER_3);
  sntp_init();

  Serial.println(F("NTP client initialized"));
}

void syncNTP() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println(F("Syncing time with NTP..."));

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);

  int retry = 0;
  const int retry_count = 15;
  while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
    Serial.print(".");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  Serial.println();

  if (retry < retry_count) {
    time_t now;
    time(&now);

    if (timeMutex && xSemaphoreTake(timeMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      lastNtpSync = now;
      timeInitialized = true;
      xSemaphoreGive(timeMutex);
    }

    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      Serial.printf("Time synced: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                    timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
  } else {
    Serial.println(F("Failed to sync time with NTP"));
    LOG_ERROR(F("NTP sync timeout"), millis() / 1000);
  }
}

bool getTimeInitialized() {
  bool initialized = false;
  if (timeMutex && xSemaphoreTake(timeMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    initialized = timeInitialized;
    xSemaphoreGive(timeMutex);
  }
  return initialized;
}

String getCurrentTimeString() {
  if (!getTimeInitialized()) {
    return "Not synced";
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time error";
  }

  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
           timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return String(buffer);
}

uint32_t getEpochTime() {
  time_t now;
  time(&now);
  return (uint32_t)now;
}

bool shouldSyncNTP() {
  if (!getTimeInitialized()) {
    return true;
  }

  time_t currentTime;
  time(&currentTime);

  bool needsSync = false;
  if (timeMutex && xSemaphoreTake(timeMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    needsSync = ((currentTime - lastNtpSync) > NTP_SYNC_INTERVAL);
    xSemaphoreGive(timeMutex);
  }

  return needsSync;
}