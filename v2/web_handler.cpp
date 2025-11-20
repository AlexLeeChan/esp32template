/* ==============================================================================
   WEB_HANDLER.CPP - Web Server Implementation
   
   Implements HTTP server with REST API providing:
   - System information (uptime, memory, CPU, temperature)
   - WiFi configuration and status
   - OTA update management
   - Task monitoring and debugging
   - Configuration management
   
   All endpoints return JSON for easy parsing by web applications.
   Runs in dedicated FreeRTOS task (webTask) for concurrent request handling.
   ============================================================================== */

#include "web_handler.h"
#include "globals.h"
#include "hardware.h"
#include "time_handler.h"
#include "wifi_handler.h"
#include "debug_handler.h"
#include "tasks.h"  
#include <ArduinoJson.h>
#include <pgmspace.h>

#if ENABLE_OTA
#include "ota_handler.h"
#endif

#include "web_html.h"

static String cleanString(const String& input);
static bool isValidIP(const String& s);
static bool isValidSubnet(const String& s);
static IPAddress parseIP(const String& s);

#if ENABLE_OTA
bool isOtaActive() {

  if (!otaMutex) return false;
  
  bool active = false;
  /* Acquire otaMutex mutex (wait up to 10ms) to safely access shared resource */
    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    active = (otaStatus.state == OTA_CHECKING || 
              otaStatus.state == OTA_DOWNLOADING || 
              otaStatus.state == OTA_FLASHING);
    xSemaphoreGive(otaMutex);
  }
  return active;
}

void sendBusyJson(const char* msg) {
  server.send(503, "application/json", String("{\"err\":\"") + msg + "\"}");
}
#endif

void sendIndex() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  server.sendContent_P(INDEX_HTML_PART1);
  delay(1);

#if DEBUG_MODE
  server.sendContent_P(INDEX_HTML_PART1_DEBUG);
  delay(1);
#endif

  server.sendContent_P(INDEX_HTML_PART1_END);
  delay(1);

  server.sendContent_P(PSTR("<div class=\"grid\">"));
  delay(1);

  server.sendContent_P(INDEX_HTML_BIZ_CARD);
  delay(1);
  server.sendContent_P(INDEX_HTML_CMD_CARD);
  delay(1);
  server.sendContent_P(INDEX_HTML_WIFI_CARD);
  delay(1);

#if ENABLE_OTA
  server.sendContent_P(INDEX_HTML_OTA);
  delay(1);
#endif

  server.sendContent_P(PSTR("</div>"));
  delay(1);

#if DEBUG_MODE
  server.sendContent_P(INDEX_HTML_TASKS);
  delay(1);
#endif

  server.sendContent_P(INDEX_HTML_PART2);
  delay(1);

#if DEBUG_MODE
  server.sendContent_P(INDEX_HTML_DEBUG_FUNCTIONS);
  delay(1);
#endif

  server.sendContent_P(INDEX_HTML_REFRESH);
  delay(1);

  server.sendContent_P(INDEX_HTML_REFRESH_END);
  delay(1);

  server.sendContent_P(INDEX_HTML_END);
  delay(1);

#if DEBUG_MODE
  server.sendContent_P(INDEX_HTML_END_DEBUG);
  delay(1);
#endif

  server.sendContent_P(INDEX_HTML_END_FINAL);
  server.client().flush();
  delay(2);
  server.client().stop();
}

void handleApiStatus() {
  if (isOtaActive()) {
    DynamicJsonDocument doc(256);
    doc["connected"] = (WiFi.status() == WL_CONNECTED);
    doc["ble"] = false;
    doc["uptime_ms"] = millis();
    doc["ota_active"] = true;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
    return;
  }

  DynamicJsonDocument doc(1536);
  doc["ble"] = bleDeviceConnected;

  bool connected = (WiFi.status() == WL_CONNECTED);
  doc["connected"] = connected;

  JsonObject net = doc.createNestedObject("net");
  net["dhcp"] = netConfig.useDHCP;
  
  if (connected) {
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    net["ssid"] = WiFi.SSID();
  } else if (wifiCredentials.hasCredentials) {

    net["ssid"] = String(wifiCredentials.ssid);
  }
  

  if (!netConfig.useDHCP) {
    net["static_ip"] = netConfig.staticIP.toString();
    net["gateway"] = netConfig.gateway.toString();
    net["subnet"] = netConfig.subnet.toString();
    net["dns"] = netConfig.dns.toString();
  }

  doc["uptime_ms"] = millis();
  doc["heap_free"] = ESP.getFreeHeap();
  doc["heap_total"] = ESP.getHeapSize();
  doc["heap_max_alloc"] = ESP.getMaxAllocHeap();

  doc["time_synced"] = getTimeInitialized();
  if (getTimeInitialized()) {
    doc["current_time"] = getCurrentTimeString();
    doc["epoch"] = getEpochTime();
  }

  doc["core0_load"] = coreLoadPct[0];
  doc["core1_load"] = coreLoadPct[1];

  doc["chip_model"] = ESP.getChipModel();
  doc["cpu_freq"] = ESP.getCpuFreqMHz();
  doc["num_cores"] = NUM_CORES;
  doc["temp_c"] = getInternalTemperatureC();

  JsonObject ota = doc.createNestedObject("ota");
#if ENABLE_OTA
  ota["enabled"] = true;
  /* Acquire otaMutex mutex (wait up to 100ms) to safely access shared resource */
    if (xSemaphoreTake(otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    ota["available"] = otaStatus.available;
    ota["state"] = (uint8_t)otaStatus.state;
    xSemaphoreGive(otaMutex);
  }
#else
  ota["enabled"] = false;
#endif

  MemoryInfo memInfo = getMemoryInfo();
  JsonObject memory = doc.createNestedObject("memory");
  memory["flash_mb"] = memInfo.flashSizeMB;
  memory["psram_total"] = memInfo.psramSizeBytes;
  memory["psram_free"] = memInfo.psramFreeBytes;
  memory["has_psram"] = memInfo.hasPsram;

  JsonObject biz = doc.createNestedObject("biz");
  biz["running"] = (gBizState == BIZ_RUNNING);
  if (execQ) biz["queue"] = uxQueueMessagesWaiting(execQ);
  biz["processed"] = bizProcessed;

#if DEBUG_MODE
  JsonObject cores = doc.createNestedObject("cores");
  for (int c = 0; c < NUM_CORES; c++) {
    JsonObject core = cores.createNestedObject(String(c));
    core["tasks"] = coreRuntime[c].taskCount;
    core["load_pct"] = coreLoadPct[c];
    core["cpu_total"] = coreRuntime[c].cpuPercentTotal;
  }
#endif

  String output;
  output.reserve(1024);
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleApiBizStart() {
  if (isOtaActive()) {
    sendBusyJson("OTA in progress");
    return;
  }
  gBizState = BIZ_RUNNING;
  server.send(200, "application/json", "{\"msg\":\"started\"}");
}

void handleApiBizStop() {
  if (isOtaActive()) {
    sendBusyJson("OTA in progress");
    return;
  }
  gBizState = BIZ_STOPPED;
  server.send(200, "application/json", "{\"msg\":\"stopped\"}");
}

void handleApiExec() {
  if (isOtaActive()) {
    sendBusyJson("OTA in progress");
    return;
  }

  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"err\":\"no body\"}");
    return;
  }

  DynamicJsonDocument doc(256);
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"err\":\"invalid JSON\"}");
    return;
  }

  String cmd = doc["cmd"] | "";
  if (cmd.length() == 0) {
    server.send(400, "application/json", "{\"err\":\"cmd required\"}");
    return;
  }

  if (cmd.length() >= MAX_MSG_SIZE) {
    server.send(400, "application/json", "{\"err\":\"cmd too long\"}");
    return;
  }

  ExecMessage* msg = allocMessage();
  if (!msg) {
    server.send(503, "application/json", "{\"err\":\"queue full\"}");
    return;
  }

  strncpy(msg->payload, cmd.c_str(), MAX_MSG_SIZE - 1);
  msg->payload[MAX_MSG_SIZE - 1] = '\0';
  msg->length = strlen(msg->payload);

  if (execQ && xQueueSend(execQ, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
    server.send(200, "application/json", "{\"msg\":\"queued\"}");
  } else {
    freeMessage(msg);
    server.send(503, "application/json", "{\"err\":\"queue send failed\"}");
  }
}

void handleApiNetwork() {
  if (isOtaActive()) {
    sendBusyJson("OTA in progress");
    return;
  }

  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"err\":\"no body\"}");
    return;
  }

  DynamicJsonDocument doc(512);
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"err\":\"invalid JSON\"}");
    return;
  }

  String ssid = doc["ssid"] | "";
  String pass = doc["pass"] | "";
  bool dhcp = doc["dhcp"] | true;
  
  bool wifiCredentialsChanged = false;

  if (ssid.length() > 0) {
    saveWiFi(ssid, pass);
    wifiCredentialsChanged = true;
  }

  netConfig.useDHCP = dhcp;
  if (!dhcp) {
    String sip = doc["static_ip"] | "";
    String gw = doc["gateway"] | "";
    String sn = doc["subnet"] | "";
    String dns = doc["dns"] | "";

    if (sip.length() > 0) netConfig.staticIP.fromString(sip);
    if (gw.length() > 0) netConfig.gateway.fromString(gw);
    if (sn.length() > 0) netConfig.subnet.fromString(sn);
    if (dns.length() > 0) netConfig.dns.fromString(dns);
  }

  saveNetworkConfig();

  if (wifiCredentials.hasCredentials || wifiCredentialsChanged) {
    /* Acquire wifiMutex mutex (wait up to 100ms) to safely access shared resource */
    if (xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      wifiConfigChanged = true;
      wifiState = WIFI_STATE_IDLE;
      xSemaphoreGive(wifiMutex);
    }
    server.send(200, "application/json", "{\"msg\":\"saved, reconnecting\"}");
  } else {

    server.send(200, "application/json", "{\"msg\":\"network config saved\"}");
  }

  LOG_WIFI(F("Network config updated"), millis() / 1000);
}

#if DEBUG_MODE
void handleApiTasks() {
  if (isOtaActive()) {
    sendBusyJson("OTA in progress");
    return;
  }

  DynamicJsonDocument doc(5120);
  JsonArray arr = doc.createNestedArray("tasks");

  uint8_t activeTaskCount = 0;
  for (uint8_t i = 0; i < taskCount; i++) {
    if (taskData[i].state == eDeleted) continue;
    activeTaskCount++;

    JsonObject t = arr.createNestedObject();
    t["name"] = taskData[i].name;
    t["priority"] = taskData[i].priority;
    
    String stateName;
    switch (taskData[i].state) {
      case eRunning: stateName = "RUNNING"; break;
      case eReady: stateName = "READY"; break;
      case eBlocked: stateName = "BLOCKED"; break;
      case eSuspended: stateName = "SUSPENDED"; break;
      case eDeleted: stateName = "DELETED"; break;
      default: stateName = "UNKNOWN"; break;
    }
    t["state"] = stateName;
    
    t["runtime"] = (uint64_t)taskData[i].runtimeAccumUs / 1000000ULL;
    t["stack_hwm"] = taskData[i].stackHighWater;
    t["stack_health"] = taskData[i].stackHealth;
    t["cpu_percent"] = taskData[i].cpuPercent;
    
    String coreStr = (taskData[i].coreAffinity == tskNO_AFFINITY) ? "ANY" : String(taskData[i].coreAffinity);
    t["core"] = coreStr;
  }

  doc["task_count"] = activeTaskCount;
  doc["uptime_ms"] = millis();

  JsonObject coreSummary = doc.createNestedObject("core_summary");
  for (int c = 0; c < NUM_CORES; c++) {
    JsonObject core = coreSummary.createNestedObject(String(c));
    core["tasks"] = coreRuntime[c].taskCount;
    core["cpu_total"] = coreRuntime[c].cpuPercentTotal;
    core["load"] = coreLoadPct[c];
  }

  String out;
  out.reserve(5120);
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleApiDebugLogs() {
  if (isOtaActive()) {
    sendBusyJson("OTA in progress");
    return;
  }

  DynamicJsonDocument doc(5120);

  JsonArray r = doc.createNestedArray("reboots");
  for (uint8_t i = 0; i < rebootLogCount; i++) {
    JsonObject o = r.createNestedObject();
    o["t"] = rebootLogs[i].uptime;
    o["epoch"] = rebootLogs[i].epoch;
    o["msg"] = rebootLogs[i].msg;
  }

  JsonArray w = doc.createNestedArray("wifi");
  for (uint8_t i = 0; i < wifiLogCount; i++) {
    JsonObject o = w.createNestedObject();
    o["t"] = wifiLogs[i].uptime;
    o["epoch"] = wifiLogs[i].epoch;
    o["msg"] = wifiLogs[i].msg;
  }

  JsonArray e = doc.createNestedArray("errors");
  for (uint8_t i = 0; i < errorLogCount; i++) {
    JsonObject o = e.createNestedObject();
    o["t"] = errorLogs[i].uptime;
    o["epoch"] = errorLogs[i].epoch;
    o["msg"] = errorLogs[i].msg;
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleApiDebugClear() {
  if (isOtaActive()) {
    sendBusyJson("OTA in progress");
    return;
  }
  clearDebugLogs();
  server.send(200, "application/json", "{\"msg\":\"logs cleared\"}");
}
#endif

void registerRoutes() {
  server.on("/", HTTP_GET, []() {
#if ENABLE_OTA
    if (isOtaActive()) {
      server.sendHeader(F("Cache-Control"), F("no-store, no-cache, must-revalidate, max-age=0"));
      server.sendHeader(F("Pragma"), F("no-cache"));
      server.setContentLength(sizeof(OTA_BUSY_HTML) - 1);
      server.send_P(200, PSTR("text/html"), OTA_BUSY_HTML);
      return;
    }
#endif
    sendIndex();
  });

  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/biz/start", HTTP_POST, handleApiBizStart);
  server.on("/api/biz/stop", HTTP_POST, handleApiBizStop);
  server.on("/api/exec", HTTP_POST, handleApiExec);
  server.on("/api/network", HTTP_POST, handleApiNetwork);

#if DEBUG_MODE
  server.on("/api/tasks", HTTP_GET, handleApiTasks);
  server.on("/api/debug/logs", HTTP_GET, handleApiDebugLogs);
  server.on("/api/debug/clear", HTTP_POST, handleApiDebugClear);
#endif

#if ENABLE_OTA
  registerOtaRoutes();
#endif

  server.onNotFound([]() {
    server.send(404, "application/json", "{\"err\":\"not found\"}");
  });
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