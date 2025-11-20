/* ==============================================================================
   BLE_HANDLER.CPP - Bluetooth Low Energy Implementation
   
   Implements BLE server with custom service for device configuration:
   - TX characteristic: Device sends status to phone/computer
   - RX characteristic: Device receives commands from phone/computer
   - Handles WiFi credential provisioning
   - Processes system control commands (reboot, clear WiFi, etc.)
   - Manages connection LED indication
   
   BLE enables wireless configuration when WiFi is not yet set up.
   Commands are JSON formatted for structured communication.
   ============================================================================== */

#include "ble_handler.h"
#include "globals.h"
#include "wifi_handler.h"
#include "hardware.h" 
#include "debug_handler.h"

#if ESP32_HAS_BLE

static String cleanString(const String& input);
static bool isValidIP(const String& s);
static bool isValidSubnet(const String& s);
static IPAddress parseIP(const String& s);

class BLECommandBuffer {
 private:
  char buffer[MAX_BLE_CMD_LENGTH];
  size_t pos;

 public:
  BLECommandBuffer() : pos(0) {
    buffer[0] = '\0';
  }

  void append(const uint8_t* data, size_t len) {
    size_t available = MAX_BLE_CMD_LENGTH - pos - 1;
    size_t to_copy = min(len, available);
    if (to_copy > 0) {
      memcpy(buffer + pos, data, to_copy);
      pos += to_copy;
      buffer[pos] = '\0';
    }
  }

  bool hasComplete() const {
    return strchr(buffer, '\n') != nullptr;
  }

  String extractCommand() {
    char* newline = strchr(buffer, '\n');
    if (!newline) return String();

    *newline = '\0';
    String cmd(buffer);
    size_t remaining = pos - (newline - buffer + 1);
    if (remaining > 0) {
      memmove(buffer, newline + 1, remaining);
    }
    pos = remaining;
    buffer[pos] = '\0';
    return cmd;
  }
};

static BLECommandBuffer cmdBuffer;

class MyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    bleDeviceConnected = true;
    NimBLEDevice::getAdvertising()->stop();
    pServer->updateConnParams(connInfo.getConnHandle(), 30, 60, 0, 400);
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    (void)pServer;
    (void)connInfo;
    (void)reason;
    bleDeviceConnected = false;
  }
};

class MyCharCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
    (void)connInfo;
    std::string value = pChar->getValue();
    if (value.length() > 0) {
      cmdBuffer.append((uint8_t*)value.data(), value.length());
      while (cmdBuffer.hasComplete()) {
        String cmd = cmdBuffer.extractCommand();
        cmd = cleanString(cmd);
        if (cmd.length() > 0) {
          handleBLECommand(cmd);
        }
      }
    }
  }
};

/* initBLE: Initializes Bluetooth Low Energy server with custom service for configuration */
void initBLE() {
  NimBLEDevice::init(BLE_ADVERT_NAME);
  NimBLEDevice::setMTU(256);

  pBLEServer = NimBLEDevice::createServer();
  if (!pBLEServer) {
    Serial.println(F("CRITICAL: Failed to create BLE server!"));
    LOG_ERROR(F("BLE server creation failed"), 0);
    return;
  }
  pBLEServer->setCallbacks(new MyServerCallbacks());

  NimBLEService* pService = pBLEServer->createService(BLE_SERVICE_UUID);
  if (!pService) {
    Serial.println(F("CRITICAL: Failed to create BLE service!"));
    LOG_ERROR(F("BLE service creation failed"), 0);
    return;
  }

  pTxCharacteristic = pService->createCharacteristic(BLE_CHAR_UUID_TX, NIMBLE_PROPERTY::NOTIFY);
  if (!pTxCharacteristic) {
    Serial.println(F("CRITICAL: Failed to create BLE TX characteristic!"));
    LOG_ERROR(F("BLE TX characteristic failed"), 0);
  }

  NimBLECharacteristic* pRxCharacteristic = pService->createCharacteristic(
      BLE_CHAR_UUID_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  if (!pRxCharacteristic) {
    Serial.println(F("CRITICAL: Failed to create BLE RX characteristic!"));
    LOG_ERROR(F("BLE RX characteristic failed"), 0);
  } else {
    pRxCharacteristic->setCallbacks(new MyCharCallbacks());
  }

  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  NimBLEAdvertisementData advData;
  advData.setName(BLE_ADVERT_NAME);
  advData.addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setAdvertisementData(advData);

  NimBLEAdvertisementData scanData;
  scanData.setName(BLE_ADVERT_NAME);
  pAdvertising->setScanResponseData(scanData);

  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  pAdvertising->start();

  Serial.println(F("BLE initialized successfully"));
}

void handleBLEReconnect() {
  if (!bleDeviceConnected && bleOldDeviceConnected) {
    delay(500);
    NimBLEDevice::startAdvertising();
    bleOldDeviceConnected = bleDeviceConnected;
  }
  if (bleDeviceConnected && !bleOldDeviceConnected) {
    bleOldDeviceConnected = bleDeviceConnected;
  }
}

void sendBLE(const String& m) {
  if (!bleDeviceConnected || !pTxCharacteristic) return;

  const char* data = m.c_str();
  size_t len = m.length();

  if (len <= 512) {
    pTxCharacteristic->setValue((uint8_t*)data, len);
    pTxCharacteristic->notify();
  } else {
    for (size_t i = 0; i < len; i += 512) {
      size_t chunk_size = (len - i > 512) ? 512 : (len - i);
      pTxCharacteristic->setValue((uint8_t*)(data + i), chunk_size);
      pTxCharacteristic->notify();
      delay(20);
    }
  }
}

void handleBLECommand(String cmd) {
  cmd = cleanString(cmd);
  cmd.trim();
  String upper = cmd;
  upper.toUpperCase();
  String response;
  response.reserve(128);

  if (upper.startsWith("SET_WIFI|") || upper.startsWith("WIFI:")) {
    String ssid, pass;
    if (upper.startsWith("SET_WIFI|")) {
      String rest = cmd.substring(9);
      int sep = rest.indexOf('|');
      if (sep > 0) {
        ssid = rest.substring(0, sep);
        pass = rest.substring(sep + 1);
      } else {
        sendBLE("ERR:FORMAT\n");
        LOG_ERROR(F("BLE: Invalid WiFi format"), millis() / 1000);
        return;
      }
    } else {
      String rest = cmd.substring(5);
      int sep = rest.indexOf(',');
      if (sep > 0) {
        ssid = rest.substring(0, sep);
        pass = rest.substring(sep + 1);
      } else {
        sendBLE("ERR:FORMAT\n");
        LOG_ERROR(F("BLE: Invalid WiFi format"), millis() / 1000);
        return;
      }
    }

    if (pass.length() == 0 && wifiCredentials.hasCredentials) {
      if (ssid.equals(String(wifiCredentials.ssid))) {

        pass = String(wifiCredentials.password);
        Serial.println(F("BLE: Preserving existing WiFi password (SSID unchanged)"));
      }
    }

    saveWiFi(ssid, pass);
    sendBLE("OK:WIFI_SAVED\n");
    delay(100);

    /* Acquire wifiMutex mutex (wait up to 1000ms) to safely access shared resource */
    if (xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      wifiManualDisconnect = false;
      wifiReconnectAttempts = 0;
      wifiConfigChanged = true;
      wifiState = WIFI_STATE_IDLE;
      xSemaphoreGive(wifiMutex);
    }

  } else if (upper.startsWith("SET_IP|")) {
    String rest = cmd.substring(7);
    if (rest.equalsIgnoreCase("DHCP")) {
      netConfig.useDHCP = true;
      saveNetworkConfig();
      sendBLE("OK:DHCP_ON\n");
      delay(100);

      if (wifiCredentials.hasCredentials) {
        /* Acquire wifiMutex mutex (wait up to 1000ms) to safely access shared resource */
    if (xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
          wifiManualDisconnect = false;
          wifiReconnectAttempts = 0;
          wifiConfigChanged = true;
          wifiState = WIFI_STATE_IDLE;
          xSemaphoreGive(wifiMutex);
        }
      }

    } else if (rest.startsWith("STATIC|") || rest.startsWith("static|")) {
      rest = rest.substring(7);
      int p1 = rest.indexOf('|');
      int p2 = rest.indexOf('|', p1 + 1);
      int p3 = rest.indexOf('|', p2 + 1);

      if (p1 > 0 && p2 > 0 && p3 > 0) {
        String ip = rest.substring(0, p1);
        String gw = rest.substring(p1 + 1, p2);
        String sub = rest.substring(p2 + 1, p3);
        String dns = rest.substring(p3 + 1);

        if (isValidIP(ip) && isValidIP(gw)) {
          netConfig.staticIP = parseIP(ip);
          netConfig.gateway = parseIP(gw);
          netConfig.subnet = isValidSubnet(sub) ? parseIP(sub) : IPAddress(255, 255, 255, 0);
          netConfig.dns = isValidIP(dns) ? parseIP(dns) : IPAddress(8, 8, 8, 8);
          netConfig.useDHCP = false;
          saveNetworkConfig();
          sendBLE("OK:STATIC_IP_SET\n");
          delay(100);

          if (wifiCredentials.hasCredentials) {
            /* Acquire wifiMutex mutex (wait up to 1000ms) to safely access shared resource */
    if (xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
              wifiManualDisconnect = false;
              wifiReconnectAttempts = 0;
              wifiConfigChanged = true;
              wifiState = WIFI_STATE_IDLE;
              xSemaphoreGive(wifiMutex);
            }
          }

        } else {
          sendBLE("ERR:INVALID_IP\n");
          LOG_ERROR(F("BLE: Invalid IP address"), millis() / 1000);
        }
      } else {
        sendBLE("ERR:FORMAT\n");
        LOG_ERROR(F("BLE: Invalid IP format"), millis() / 1000);
      }
    } else {
      sendBLE("ERR:FORMAT\n");
      LOG_ERROR(F("BLE: Invalid SET_IP format"), millis() / 1000);
    }

  } else if (upper == "GET_STATUS" || upper == "STATUS") {
    bool connected = (WiFi.status() == WL_CONNECTED);
    response = "STATUS|";
    response += connected ? "CONNECTED" : "DISCONNECTED";
    response += "|" + WiFi.SSID() + "|";
    response += connected ? WiFi.localIP().toString() : "-";
    response += "|";
    response += connected ? String(WiFi.RSSI()) : "-";
    response += "|";
    response += netConfig.useDHCP ? "DHCP" : "STATIC";
    if (!netConfig.useDHCP) {
      response += "|" + netConfig.staticIP.toString();
    }
    response += "\n";
    sendBLE(response);

  } else if (upper == "DISCONNECT_WIFI" || upper == "DISCONNECT") {
    /* Acquire wifiMutex mutex (wait up to 1000ms) to safely access shared resource */
    if (xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      wifiManualDisconnect = true;
      wifiState = WIFI_STATE_IDLE;
      xSemaphoreGive(wifiMutex);
    }
    sendBLE("OK:WIFI_DISCONNECTED\n");

  } else if (upper == "CLEAR_SAVED" || upper == "CLEAR_WIFI") {
    prefs.remove("wifi_ssid");
    prefs.remove("wifi_pass");
    saveWiFi("", "");
    /* Acquire wifiMutex mutex (wait up to 1000ms) to safely access shared resource */
    if (xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      wifiManualDisconnect = true;
      wifiState = WIFI_STATE_IDLE;
      xSemaphoreGive(wifiMutex);
    }
    sendBLE("OK:WIFI_CLEARED\n");
    Serial.println(F("=== WiFi Credentials Cleared ==="));

  } else if (upper == "RESTART") {
    Serial.println(F("\n=== BLE: Restart Command Received ==="));
    sendBLE("OK:RESTARTING\n");
    delay(500);
    #if DEBUG_MODE
    prefs.putBool(NVS_FLAG_USER_REBOOT, true);
    vTaskDelay(pdMS_TO_TICKS(100));
    #endif
    ESP.restart();

  } else if (upper == "HEAP") {
    response = "HEAP:FREE=" + String(ESP.getFreeHeap());
    response += "|MIN=" + String(ESP.getMinFreeHeap());
    response += "|MAX=" + String(ESP.getMaxAllocHeap()) + "\n";
    sendBLE(response);

  } else if (upper == "TEMP") {
    float t = getInternalTemperatureC();
    if (isnan(t)) {
      sendBLE("TEMP:NOT_AVAILABLE\n");
    } else {
      sendBLE("TEMP:" + String(t, 2) + "\n");
    }

  } else {
    sendBLE("ERR:UNKNOWN_CMD\n");
    LOG_ERROR(String("BLE: Unknown cmd: ") + cmd, millis() / 1000);
  }
}

static String cleanString(const String& input) {
  if (input.length() == 0) return String();
  const char* str = input.c_str();
  int len = input.length();
  int start = 0, end = len - 1;

  while (start < len && isspace((unsigned char)str[start])) start++;
  while (end >= start && isspace((unsigned char)str[end])) end--;

  return (start > end) ? String() : input.substring(start, end + 1);
}

static bool isValidIP(const String& s) {
  IPAddress ip;
  if (!ip.fromString(s)) return false;
  return !(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
}

static bool isValidSubnet(const String& s) {
  IPAddress sub;
  if (!sub.fromString(s)) return false;
  uint32_t m = ~(uint32_t)sub;
  return (m & (m + 1)) == 0;
}

static IPAddress parseIP(const String& s) {
  IPAddress ip;
  if (ip.fromString(s)) return ip;

  int p[4] = { 0 };
  int idx = 0, st = 0;
  for (int i = 0; i <= (int)s.length() && idx < 4; i++) {
    if (i == (int)s.length() || s[i] == '.') {
      p[idx++] = s.substring(st, i).toInt();
      st = i + 1;
    }
  }
  return IPAddress(p[0], p[1], p[2], p[3]);
}

#else

/* initBLE: Initializes Bluetooth Low Energy server with custom service for configuration */
void initBLE() {}
void handleBLEReconnect() {}
void sendBLE(const String& m) { (void)m; }
void handleBLECommand(String cmd) { (void)cmd; }
#endif