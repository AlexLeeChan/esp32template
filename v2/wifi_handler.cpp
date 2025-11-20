/* ==============================================================================
   WIFI_HANDLER.CPP - WiFi Management Implementation
   
   Implements WiFi connectivity with automatic reconnection:
   - Loads credentials from NVS (non-volatile storage)
   - Connects to WiFi with configurable timeout
   - Monitors connection status and reconnects on failure
   - Applies DHCP or static IP configuration
   - Synchronizes time via NTP when connected
   
   State machine ensures robust WiFi connectivity with exponential backoff
   on repeated failures to prevent connection storms.
   ============================================================================== */

#include "wifi_handler.h"
#include "globals.h"
#include <esp_wifi.h>
#include "debug_handler.h"

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    (void)info;
    switch (event) {
      case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        break;
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        if (wifiMutex && xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          wifiState = WIFI_STATE_CONNECTED;
          xSemaphoreGive(wifiMutex);
        }
        Serial.printf("WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
        break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        if (!wifiManualDisconnect) {
          if (wifiMutex && xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            wifiState = WIFI_STATE_DISCONNECTED;
            xSemaphoreGive(wifiMutex);
          }
          LOG_WIFI(F("WiFi disconnected"), millis() / 1000);
        }
        break;
      default:
        break;
    }
  });

  Serial.println(F("WiFi configured (event handlers registered)"));
}

void startWiFiConnection() {
  if (!wifiCredentials.hasCredentials) {
    Serial.println(F("No WiFi credentials available, skipping connection."));
    return;
  }

  if (wifiMutex && xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    WiFi.begin(wifiCredentials.ssid, wifiCredentials.password);
    wifiState = WIFI_STATE_CONNECTING;
    wifiLastConnectAttempt = millis();
    wifiReconnectAttempts++;

    xSemaphoreGive(wifiMutex);

    Serial.printf("WiFi connecting to: %s\n", wifiCredentials.ssid);
  }
}

void checkWiFiConnection() {
#if ENABLE_OTA
  if (otaInProgress) return;
#endif

  if (!wifiMutex || xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
    return;
  }

  uint32_t now = millis();
  wl_status_t status = WiFi.status();

  if (wifiManualDisconnect || wifiConfigChanged) {
    bool isManualDisconnect = wifiManualDisconnect;
    bool isConfigChange = wifiConfigChanged && !wifiManualDisconnect;
    
    if (status != WL_DISCONNECTED) {
      if (isManualDisconnect) {
        Serial.println(F("WiFi: Manual disconnect triggered"));
      } else if (isConfigChange) {
        Serial.println(F("WiFi: Config change - reconnecting..."));
      }
      WiFi.disconnect(true, true);
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    wifiConfigChanged = false;
    wifiState = WIFI_STATE_IDLE;

    if (wifiManualDisconnect) {
      WiFi.mode(WIFI_OFF);
      Serial.println(F("WiFi: Radio OFF"));
    }

    xSemaphoreGive(wifiMutex);
    return;
  }

  switch (wifiState) {
    case WIFI_STATE_IDLE:
      if (wifiCredentials.hasCredentials) {
        if (wifiReconnectAttempts >= MAX_WIFI_RECONNECT_ATTEMPTS) {
          Serial.println(F("WiFi: Max reconnect attempts reached. Will not retry."));
          wifiManualDisconnect = true;
          LOG_ERROR(F("WiFi: Max reconnect attempts"), millis() / 1000);
          xSemaphoreGive(wifiMutex);
          return;
        }

        bool needsFullReconfig = !wifiHasBeenConfigured || 
                                 (now - wifiLastDisconnectTime > 2000);

        if (needsFullReconfig) {
          Serial.println(F("WiFi: IDLE, re-configuring..."));
          WiFi.disconnect(true, true);
          vTaskDelay(pdMS_TO_TICKS(200));
          WiFi.mode(WIFI_OFF);
          vTaskDelay(pdMS_TO_TICKS(200));
          WiFi.mode(WIFI_STA);
          vTaskDelay(pdMS_TO_TICKS(500));

          if (!netConfig.useDHCP) {
            Serial.println(F("WiFi: Applying Static IP config"));
            WiFi.config(netConfig.staticIP, netConfig.gateway, netConfig.subnet, netConfig.dns);
          } else {
            Serial.println(F("WiFi: Applying DHCP config"));
            WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
          }
          
          wifiHasBeenConfigured = true;
        } else {
          Serial.println(F("WiFi: IDLE, quick reconnect..."));
        }

        xSemaphoreGive(wifiMutex);
        startWiFiConnection();
        return;
      } else {
        Serial.println(F("WiFi: IDLE, no credentials"));
      }
      break;

    case WIFI_STATE_CONNECTING:
      if (status == WL_CONNECTED) {
        wifiState = WIFI_STATE_CONNECTED;
        wifiReconnectAttempts = 0;
        wifiLastConnectAttempt = now;
      } else if (now - wifiLastConnectAttempt > WIFI_CONNECT_TIMEOUT) {
        wifiState = WIFI_STATE_DISCONNECTED;
        wifiLastConnectAttempt = now;
        LOG_ERROR(F("WiFi: Connection timeout"), millis() / 1000);
      }
      break;

    case WIFI_STATE_CONNECTED:
      if (status != WL_CONNECTED) {
        wifiState = WIFI_STATE_DISCONNECTED;
        wifiLastConnectAttempt = now;
        wifiLastDisconnectTime = now;
        Serial.println(F("WiFi: Connection lost"));
      } else {
        if (wifiReconnectAttempts > 0 && (now - wifiLastConnectAttempt > 300000)) {
          wifiReconnectAttempts = 0;
        }
      }
      break;

    case WIFI_STATE_DISCONNECTED: {
      uint8_t maxShift = (wifiReconnectAttempts < 4) ? wifiReconnectAttempts : 4;
      uint32_t backoffDelay = WIFI_RECONNECT_DELAY << maxShift;
      if (now - wifiLastConnectAttempt > backoffDelay) {
        wifiState = WIFI_STATE_IDLE;
      }
    } break;
  }

  xSemaphoreGive(wifiMutex);

  bool nowConnected = (WiFi.status() == WL_CONNECTED);
  if (nowConnected && !wifiWasConnected) {
    #if DEBUG_MODE
    uint32_t t = millis() / 1000;
    if (wifiFirstConnectDone) {
      addWifiLog(F("WiFi reconnected"), t);
    }
    #endif
    wifiFirstConnectDone = true;
  }
  wifiWasConnected = nowConnected;
}

void loadWiFiCredentials() {
  String ssid = prefs.getString("wifi_ssid", "");
  String pass = prefs.getString("wifi_pass", "");

  if (ssid.length() > 0 && ssid.length() < 64) {
    strncpy(wifiCredentials.ssid, ssid.c_str(), sizeof(wifiCredentials.ssid) - 1);
    wifiCredentials.ssid[sizeof(wifiCredentials.ssid) - 1] = '\0';

    if (pass.length() < 64) {
      strncpy(wifiCredentials.password, pass.c_str(), sizeof(wifiCredentials.password) - 1);
      wifiCredentials.password[sizeof(wifiCredentials.password) - 1] = '\0';
    } else {
      wifiCredentials.password[0] = '\0';
    }

    wifiCredentials.hasCredentials = true;
    Serial.printf("Loaded WiFi credentials for: %s\n", wifiCredentials.ssid);
  } else {
    wifiCredentials.hasCredentials = false;
    Serial.println("No WiFi credentials found");
  }
}

void saveWiFi(const String& s, const String& p) {
  if (!prefs.putString("wifi_ssid", s)) {
    LOG_ERROR(F("Failed to save WiFi SSID"), millis() / 1000);
  }

  if (!prefs.putString("wifi_pass", p) && p.length() > 0) {
    LOG_ERROR(F("Failed to save WiFi password"), millis() / 1000);
  }

  if (s.length() > 0 && s.length() < 64) {
    strncpy(wifiCredentials.ssid, s.c_str(), sizeof(wifiCredentials.ssid) - 1);
    wifiCredentials.ssid[sizeof(wifiCredentials.ssid) - 1] = '\0';

    if (p.length() < 64) {
      strncpy(wifiCredentials.password, p.c_str(), sizeof(wifiCredentials.password) - 1);
      wifiCredentials.password[sizeof(wifiCredentials.password) - 1] = '\0';
    } else {
      wifiCredentials.password[0] = '\0';
    }

    wifiCredentials.hasCredentials = true;
  } else {
    wifiCredentials.hasCredentials = false;
  }
}

void loadNetworkConfig() {
  netConfig.useDHCP = prefs.getBool("use_dhcp", true);
  netConfig.staticIP = (uint32_t)prefs.getUInt("static_ip", (uint32_t)IPAddress(192, 168, 1, 100));
  netConfig.gateway = (uint32_t)prefs.getUInt("gateway", (uint32_t)IPAddress(192, 168, 1, 1));
  netConfig.subnet = (uint32_t)prefs.getUInt("subnet", (uint32_t)IPAddress(255, 255, 255, 0));
  netConfig.dns = (uint32_t)prefs.getUInt("dns", (uint32_t)IPAddress(8, 8, 8, 8));
}

void saveNetworkConfig() {
  if (!prefs.putBool("use_dhcp", netConfig.useDHCP)) {
    LOG_ERROR(F("Failed to save DHCP setting"), millis() / 1000);
  }
  if (!prefs.putUInt("static_ip", (uint32_t)netConfig.staticIP)) {
    LOG_ERROR(F("Failed to save static IP"), millis() / 1000);
  }
  if (!prefs.putUInt("gateway", (uint32_t)netConfig.gateway)) {
    LOG_ERROR(F("Failed to save gateway"), millis() / 1000);
  }
  if (!prefs.putUInt("subnet", (uint32_t)netConfig.subnet)) {
    LOG_ERROR(F("Failed to save subnet"), millis() / 1000);
  }
  if (!prefs.putUInt("dns", (uint32_t)netConfig.dns)) {
    LOG_ERROR(F("Failed to save DNS"), millis() / 1000);
  }
}