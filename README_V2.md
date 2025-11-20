# ESP32(x) WiFi & BLE Template v2 - Modular Multi-File Version
# Production-Ready Architecture with Enhanced Features

✅ **This is Version 2** - the actively developed, production-ready modular template with enhanced features.

> **Note:** Looking for the simpler single-file version? See **[Version 1 (Single File)](./README_V1.md)**.

---

This is a comprehensive, production-grade RTOS template for building robust applications on ESP32, ESP32-S3, ESP32-C3, ESP32-C6, and other variants. 

**Configure WiFi through BLE from your browser!** Includes OTA firmware updates, web dashboard, advanced task monitoring, persistent logging, NTP time synchronization, and flash-safe operations.

The project uses a **clean modular architecture** with 20+ separate files organized by functionality, making it ideal for production deployments and team collaboration.

---

## 🚀 Key Features

### Core Functionality
* **Multi-Chip Support:** Automatically detects and configures for ESP32, ESP32-S3, ESP32-C3, ESP32-C6, and ESP32-S2
* **Modular Architecture:** Clean separation into 20+ files organized by functionality
* **Dynamic Web Dashboard:** Responsive dark-mode single-page web app
* **BLE Configuration:** Set WiFi from any Bluetooth-enabled device
* **Zero-Malloc Queue:** Message pool for business logic to prevent fragmentation

### Advanced Features (New in V2)
* 🆕 **NTP Time Synchronization:** Accurate timekeeping with configurable NTP servers
* 🆕 **Persistent Logging:** Circular log buffers for errors, WiFi events, and reboots (survives resets)
* 🆕 **Flash-Safe Operations:** Dedicated task for safe NVS writes with mutex protection
* 🆕 **Enhanced Boot Tracking:** Reset reason detection and logging
* 🆕 **Structured Codebase:** Clean module separation (handlers, utilities, types)
* 🆕 **Memory Safety:** Comprehensive mutex protection and resource management

### OTA Updates
* Web-based firmware upload from HTTP/HTTPS URLs
* Real-time progress tracking with file size display
* Graceful task shutdown with proper cleanup
* Automatic partition validation and safety checks
* Watchdog-aware update process (< 20s WDT timeout)
* HTTP redirect support for GitHub releases

### Task Monitoring & Debugging
* Per-core, per-task CPU utilization calculation
* Stack high-water mark monitoring with health indicators
* Runtime statistics tracking
* Persistent debug logs (DEBUG_MODE)
* Reset reason tracking and logging

### Networking
* DHCP and Static IP support
* Credentials saved to NVS with flash-safe writes
* Non-blocking WiFi state machine with exponential backoff
* Configurable via Web or BLE
* WiFi event logging (DEBUG_MODE)

### Robustness
* ESP Task Watchdog Timer on all critical tasks
* Correct task pinning for performance and stability
* Graceful task exit for OTA updates
* Memory monitoring (heap, PSRAM, flash)
* Chip temperature monitoring (on supported chips)

---

## 📁 Project Structure

```
ESP32_Template_V2/
├── rngds_base_controller.ino  # Main entry point
├── config.h                    # System configuration
├── types.h                     # Type definitions
├── globals.h                   # Global variables
├── globals.cpp                 # Global variable implementations
│
├── hardware.h / .cpp           # Hardware detection
├── time_handler.h / .cpp       # NTP time synchronization
├── wifi_handler.h / .cpp       # WiFi management
├── ble_handler.h / .cpp        # Bluetooth Low Energy
├── web_handler.h / .cpp        # Web server & API
├── ota_handler.h / .cpp        # OTA firmware updates
├── tasks.h / .cpp              # FreeRTOS tasks
├── network_utils.h / .cpp      # Network utilities
│
├── debug_handler.h / .cpp      # Logging & monitoring (DEBUG_MODE)
├── cpu_monitor.h / .cpp        # Task monitoring (DEBUG_MODE)
│
└── web_html.h                  # Web dashboard HTML/CSS/JS
```

### Module Responsibilities

| Module | Purpose |
|--------|---------|
| **config.h** | All configurable parameters (timeouts, features, hardware) |
| **types.h** | Data structures and enumerations |
| **globals.h/cpp** | Shared global state and objects |
| **hardware.h/cpp** | Chip detection, LED control, memory info |
| **time_handler** | NTP synchronization and timestamp management |
| **wifi_handler** | WiFi state machine, credential management |
| **ble_handler** | BLE server, command processing |
| **web_handler** | HTTP server, API endpoints |
| **ota_handler** | Firmware update orchestration |
| **tasks** | FreeRTOS task implementations (sys, web, biz) |
| **network_utils** | IP validation, parsing helpers |
| **debug_handler** | Persistent logging system |
| **cpu_monitor** | Task runtime statistics |
| **web_html.h** | Dashboard HTML stored in PROGMEM |

---

## 🖥️ Web Dashboard & API

The web server (running on port 80) provides a full dashboard and comprehensive JSON API.

### Dashboard UI

* **System Status Card:**
    * WiFi/BLE connection status (live indicators)
    * IP address, RSSI, uptime
    * Heap memory (free/total)
    * Per-core CPU load
    * Chip temperature
    * Hardware info (model, frequency, cores)
    * Flash size
    * PSRAM info (if available)
    * Current time (from NTP)

* **Business Module Control:**
    * Start/Stop business logic task
    * Queue depth and processed count
    * Command execution interface

* **WiFi Configuration:**
    * SSID and password entry
    * DHCP / Static IP toggle
    * Static IP settings (IP, gateway, subnet, DNS)
    * Save & connect button

* **OTA Update Interface:**
    * Firmware URL input
    * Free space indicator
    * Update button with progress bar
    * Real-time status messages

* **Debug Logs (DEBUG_MODE):**
    * Error log viewer
    * WiFi event log viewer
    * Reboot log viewer
    * Log clearing functionality

* **Task Monitor (DEBUG_MODE):**
    * Scrollable task table with per-task statistics
    * CPU%, stack health, core affinity
    * State and priority information
    * Runtime tracking

### JSON API Endpoints

#### System & Monitoring
```
GET /api/status
```
Returns comprehensive system status:
```json
{
  "ble": true,
  "connected": true,
  "ip": "192.168.1.100",
  "rssi": -45,
  "net": {
    "ssid": "MyNetwork",
    "dhcp": false,
    "static_ip": "192.168.1.100",
    "gateway": "192.168.1.1",
    "subnet": "255.255.255.0",
    "dns": "8.8.8.8"
  },
  "uptime_ms": 123456,
  "heap_free": 180000,
  "heap_total": 320000,
  "heap_max_alloc": 110000,
  "core0_load": 35,
  "core1_load": 12,
  "chip_model": "ESP32-S3",
  "cpu_freq": 240,
  "num_cores": 2,
  "temp_c": 45.2,
  "time": "2024-11-20 16:30:45",
  "time_valid": true,
  "ota": {
    "enabled": true,
    "available": true,
    "state": 0
  },
  "memory": {
    "flash_mb": 16,
    "psram_total": 8388608,
    "psram_free": 7654321,
    "has_psram": true
  },
  "biz": {
    "running": true,
    "queue": 0,
    "processed": 42
  }
}
```

```
GET /api/tasks
```
Returns task monitoring data (DEBUG_MODE):
```json
{
  "tasks": [
    {
      "name": "sys",
      "priority": 2,
      "state": "RUNNING",
      "runtime": 1234,
      "stack_hwm": 2048,
      "stack_health": "good",
      "cpu_percent": 5,
      "core": "0"
    },
    // ... more tasks
  ],
  "task_count": 12,
  "uptime_ms": 123456,
  "core_summary": {
    "0": {
      "tasks": 7,
      "cpu_total": 45,
      "load": 35
    },
    "1": {
      "tasks": 5,
      "cpu_total": 15,
      "load": 12
    }
  }
}
```

#### Network Configuration
```
POST /api/network
Content-Type: application/json

{
  "ssid": "NetworkName",
  "pass": "password",
  "dhcp": true
}

// OR with static IP:

{
  "ssid": "NetworkName",
  "pass": "password",
  "dhcp": false,
  "static_ip": "192.168.1.100",
  "gateway": "192.168.1.1",
  "subnet": "255.255.255.0",
  "dns": "8.8.8.8"
}
```

#### Business Logic Control
```
POST /api/exec
Content-Type: application/json

{"cmd": "your_command"}
```
Special commands:
* `reset` or `reboot` - Restart device (500ms delay)
* Any custom command - Processed by bizTask

```
POST /api/biz/start   # Start business logic processing
POST /api/biz/stop    # Stop business logic processing
```

#### OTA Updates
```
GET /api/ota/status
```
Returns OTA status and partition info:
```json
{
  "available": true,
  "state": 0,  // 0=IDLE, 1=CHECKING, 2=DOWNLOADING, 3=FLASHING, 4=SUCCESS, 5=FAILED
  "progress": 0,
  "error": "",
  "file_size": 0,
  "current_partition": "app0",
  "next_partition": "app1",
  "partition_size": 1966080,
  "sketch_size": 1234567,
  "free_sketch_space": 731513,
  "sketch_md5": "abc123..."
}
```

```
POST /api/ota/update
Content-Type: application/json

{"url": "http://example.com/firmware.bin"}
```

```
POST /api/ota/reset   # Reset OTA state after failure
GET /api/ota/info     # Get detailed partition table
```

#### Debug Logs (DEBUG_MODE)
```
GET /api/logs/errors  # Get error logs
GET /api/logs/wifi    # Get WiFi event logs
GET /api/logs/reboots # Get reboot logs
POST /api/logs/clear  # Clear all logs
```

---

## 📱 BLE Control

The device advertises under the name `RNGDS_ESP32*` (e.g., `RNGDS_ESP32S3`).

**Connection Details:**
* Service UUID: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
* RX Characteristic: `beb5483e-36e1-4688-b7f5-ea07361b26a8` (write commands here)
* TX Characteristic: `beb5483e-36e1-4688-b7f5-ea07361b26a9` (receive responses)

### BLE Commands

All commands must be newline-terminated (`\n`).

**WiFi Configuration:**
```
SET_WIFI|ssid|password
WIFI:ssid,password        # Alternative format
```

**IP Configuration:**
```
SET_IP|DHCP
SET_IP|STATIC|192.168.1.100|192.168.1.1|255.255.255.0|8.8.8.8
```

**Status & Control:**
```
GET_STATUS               # Returns: STATUS|state|ssid|ip|rssi|mode|static_ip
DISCONNECT_WIFI          # Disconnect from WiFi
DISCONNECT               # Alternative
CLEAR_SAVED              # Clear saved WiFi credentials
CLEAR_WIFI               # Alternative
RESTART                  # Reboot device
```

**System Info:**
```
HEAP                     # Returns: HEAP:FREE=xxx|MIN=yyy|MAX=zzz
TEMP                     # Returns: TEMP:xx.x or TEMP:NOT_AVAILABLE
```

### BLE Response Format

**Status Response:**
```
STATUS|CONNECTED|MyNetwork|192.168.1.100|-65|STATIC|192.168.1.100
STATUS|DISCONNECTED||||DHCP|
```

**Quick Updates:**
```
CONNECTED|192.168.1.100
DISCONNECTED
CONNECTING
```

**Acknowledgements:**
```
OK:WIFI_SAVED
OK:DHCP_ON
OK:STATIC_IP_SET
OK:WIFI_DISCONNECTED
OK:WIFI_CLEARED
OK:RESTARTING

ERR:FORMAT
ERR:INVALID_IP
ERR:UNKNOWN_CMD
```

---

## 🛠️ RTOS Architecture

The system uses a **5-phase boot process** for reliability:

### Boot Phases

**Phase 1: Configuration Loading**
* Load network config from NVS
* Load WiFi credentials
* Load debug logs (DEBUG_MODE)
* Track reset reason

**Phase 2: Non-Network Components**
* Initialize temperature sensor
* Create message pool and mutexes
* Initialize BLE
* Create flash write queue (DEBUG_MODE)
* Register web routes

**Phase 3: Create RTOS Tasks**
* Create bizTask (Core 1)
* Create webTask (Core 0)
* Create systemTask (Core 0)

**Phase 4: Web Server Preparation**
* Route registration complete

**Phase 5: Network Initialization**
* Configure WiFi
* Start web server
* Initialize NTP
* Trigger WiFi connection

### Task Structure

**systemTask (Core 0, Priority 2, Stack 12KB)**
* WiFi state machine with exponential backoff
* BLE advertising/reconnection
* LED blinking (indicates BLE connection)
* System health monitoring
* Watchdog feeding
* Pauses during OTA updates

**webTask (Core 0, Priority 1, Stack 10KB)**
* Handles HTTP requests (server.handleClient())
* Updates task monitoring (DEBUG_MODE)
* API endpoint processing
* Watchdog feeding
* Graceful exit for OTA flash

**bizTask (Core 1, Priority 1, Stack 4KB)**
* **Your custom application logic goes here**
* Idles when BIZ_STOPPED
* Processes queue messages when BIZ_RUNNING
* Zero-malloc message pool
* Handles reset/reboot commands
* Graceful exit for OTA

**flashWriteTask (Core ANY, Priority 0, Stack 3KB)** (DEBUG_MODE only)
* Dedicated task for safe NVS writes
* Prevents flash corruption
* Processes write requests from queue
* Mutex-protected operations

---

## 🔧 Configuration

Edit `config.h` to customize system behavior:

### Feature Flags
```cpp
#define DEBUG_MODE 1        // Enable logging & task monitoring
#define ENABLE_OTA 1        // Enable firmware updates
```

### Timing Parameters
```cpp
#define WDT_TIMEOUT 20                      // Watchdog timeout (seconds)
#define STACK_CHECK_INTERVAL 60000          // Stack check interval (ms)
#define WIFI_RECONNECT_DELAY 15000          // Base reconnect delay (ms)
#define WIFI_CONNECT_TIMEOUT 30000          // Connection timeout (ms)
#define MAX_WIFI_RECONNECT_ATTEMPTS 5       // Max reconnect attempts
```

### Message Pool
```cpp
#define MSG_POOL_SIZE 10    // Number of message slots
#define MAX_MSG_SIZE 256    // Max message length (bytes)
#define MAX_BLE_CMD_LENGTH 256
```

### NTP Configuration
```cpp
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"
#define NTP_SERVER_3 "time.google.com"
#define GMT_OFFSET_SEC 0
#define DAYLIGHT_OFFSET_SEC 0
#define NTP_SYNC_INTERVAL 3600  // Resync every hour
```

### Debug Settings (DEBUG_MODE)
```cpp
#define MAX_DEBUG_LOGS 32           // Log entries per type
#define FLASH_WRITE_QUEUE_SIZE 32   // Flash write queue depth
```

### Hardware-Specific
The template automatically detects your chip and configures:
* LED pin and polarity
* BLE availability
* Temperature sensor availability
* Core count (single/dual)

---

## 📊 Advanced Features

### Persistent Debug Logging (DEBUG_MODE)

Three independent circular log buffers:
* **Error Logs:** System errors and exceptions
* **WiFi Logs:** Connection events, failures, reconnects
* **Reboot Logs:** Unexpected reboots with reset reason

**Features:**
* Survives device reboots
* Stored in NVS flash
* Flash-safe writes via dedicated task
* Viewable through web API
* Includes uptime and epoch timestamp

**Usage:**
```cpp
LOG_ERROR("Sensor timeout", millis()/1000);
LOG_WIFI("Connected to " + String(WiFi.SSID()), millis()/1000);
LOG_REBOOT("Unexpected watchdog reset", 0);
```

### NTP Time Synchronization

**Features:**
* Three configurable NTP servers
* Automatic sync on boot (when WiFi connected)
* Periodic resync (configurable interval)
* Timezone and DST offset support
* Time validity tracking

**API:**
```cpp
initNTP();                      // Initialize (called automatically)
syncNTP();                      // Force sync
bool valid = getTimeInitialized();
String time = getCurrentTimeString();  // "2024-11-20 16:30:45"
uint32_t epoch = getEpochTime();
```

### Flash-Safe Write Operations (DEBUG_MODE)

**Problem:** Direct NVS writes from multiple tasks can cause corruption.

**Solution:** Dedicated `flashWriteTask` with mutex protection.

**How it works:**
1. Task queues a write request
2. flashWriteTask receives request
3. Acquires mutex
4. Performs write
5. Releases mutex
6. Other tasks are never blocked

**Benefits:**
* No flash corruption
* No task blocking
* Automatic retry logic
* Safe concurrent access

### Memory Monitoring

**Features:**
* Flash size detection
* PSRAM detection and tracking
* Heap usage (current, min, max)
* Per-allocation max size
* PSRAM free/used reporting

**API:**
```cpp
MemoryInfo info = getMemoryInfo();
Serial.printf("Flash: %u MB\n", info.flashSizeMB);
if (info.hasPsram) {
  Serial.printf("PSRAM: %u / %u bytes free\n",
                info.psramFreeBytes, info.psramSizeBytes);
}
```

---

## 🔄 OTA Update Process

### Update Flow

1. **User Initiates:** Provides firmware URL via web interface
2. **Validation:** Checks partition availability and heap
3. **Pre-Download:**
    * Stops bizTask (frees resources)
    * Disables BLE (frees memory)
    * Sets `otaInProgress` flag
    * Disables WiFi power save

4. **Download Phase:**
    * HTTP/HTTPS GET with redirect support
    * 1KB chunks written to Update API
    * Progress updates every 500ms
    * Watchdog reset every 5s
    * WiFi monitoring (auto-abort on disconnect)

5. **Flash Phase:**
    * Sets state to FLASHING
    * 2-second UI update delay
    * Stops webTask (critical!)
    * Calls Update.end()
    * Validates write

6. **Completion:**
    * Success: Reboot after 2s
    * Failure: Recreate tasks, reset state

### Safety Mechanisms

**Partition Checks:**
* Validates OTA partition exists
* Checks partition size vs. file size
* Aborts if file too large

**Memory Protection:**
* Requires 50KB free heap minimum
* Monitors heap during download
* Stops BLE to free ~80KB

**Network Protection:**
* Disables WiFi power save
* Monitors connection status
* Aborts on WiFi disconnect

**Watchdog Management:**
* Dedicated OTA task (24KB stack)
* Reset every 5s during download
* All operations < 20s WDT timeout

**Flash Protection:**
* Uses Update API properly
* Validates write size
* Checks Update.end() status
* Graceful abort on error

**Task Management:**
* bizTask exits before download
* webTask exits before flash
* systemTask keeps WiFi alive
* Auto-recreate on failure

---

## 💻 How to Add Your Business Logic

### Basic Template

```cpp
// In bizTask (tasks.cpp), replace the default logic:

void bizTask(void* param) {
  esp_task_wdt_add(NULL);
  ExecMessage* msg = nullptr;

  // Your initialization here
  // Example: sensor setup, variable init, etc.

  for (;;) {
    #if ENABLE_OTA
    if (bizTaskShouldExit) {
      // OTA cleanup
      esp_task_wdt_delete(NULL);
      bizTaskHandle = NULL;
      vTaskDelete(NULL);
      return;
    }
    #endif
    
    esp_task_wdt_reset();

    if (gBizState == BIZ_RUNNING && !isOtaActive()) {
      if (execQ && xQueueReceive(execQ, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (msg) {
          String cmd = String(msg->payload);
          cmd.toLowerCase();

          // Handle your custom commands here
          if (cmd.equals("read_sensor")) {
            float value = readMySensor();
            Serial.printf("Sensor: %.2f\n", value);
          }
          else if (cmd.equals("toggle_led")) {
            toggleMyLED();
          }
          else if (cmd.startsWith("set_threshold:")) {
            int threshold = cmd.substring(14).toInt();
            setMyThreshold(threshold);
          }
          // ... add more commands

          // Built-in reboot handling
          else if (cmd.equals("reset") || cmd.equals("reboot")) {
            freeMessage(msg);
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP.restart();
          }

          bizProcessed++;
          freeMessage(msg);
          msg = nullptr;
        }
      }

      // Your periodic work here (runs ~10x per second)
      // Example: read sensors, update outputs, etc.
      
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}
```

### Sending Commands from Web/BLE

**Via Web:**
```bash
curl -X POST http://192.168.1.100/api/exec \
  -H "Content-Type: application/json" \
  -d '{"cmd":"read_sensor"}'
```

**Via BLE:**
```
SET_WIFI|MyNetwork|password\n
# (device connects to WiFi)
# Then use web interface
```

**From Web Dashboard:**
1. Navigate to "Command Exec" card
2. Type command (e.g., `read_sensor`)
3. Click "Execute"

---

## 📚 API Reference

### Global Functions

**Hardware:**
```cpp
MemoryInfo getMemoryInfo();
float getInternalTemperatureC();  // Returns NAN if unavailable
```

**Time:**
```cpp
void initNTP();
String getCurrentTimeString();    // "2024-11-20 16:30:45"
uint32_t getEpochTime();
bool getTimeInitialized();
```

**WiFi:**
```cpp
void setupWiFi();
void checkWiFiConnection();       // Call periodically
void saveWiFi(String ssid, String pass);
void loadWiFiCredentials();
```

**Network Config:**
```cpp
void saveNetworkConfig();
void loadNetworkConfig();
bool isValidIP(const String& s);
bool isValidSubnet(const String& s);
IPAddress parseIP(const String& s);
```

**Logging (DEBUG_MODE):**
```cpp
LOG_ERROR(msg, uptimeSec);
LOG_WIFI(msg, uptimeSec);
LOG_REBOOT(msg, uptimeSec);
void clearDebugLogs();
```

**OTA:**
```cpp
bool isOtaActive();               // Check if OTA in progress
```

**Message Pool:**
```cpp
ExecMessage* allocMessage();      // Get message from pool
void freeMessage(ExecMessage* msg); // Return to pool
```

### Global Variables

**State:**
```cpp
extern volatile WiFiState wifiState;
extern volatile BizState gBizState;
extern volatile uint32_t bizProcessed;
```

**Handles:**
```cpp
extern TaskHandle_t webTaskHandle;
extern TaskHandle_t bizTaskHandle;
extern TaskHandle_t sysTaskHandle;
```

**Queues:**
```cpp
extern QueueHandle_t execQ;       // Command queue to bizTask
```

**Config:**
```cpp
extern NetworkConfig netConfig;
extern WiFiCredentials wifiCredentials;
```

---

## 🎓 Examples

### Example 1: Simple Sensor Reader

```cpp
// Add to bizTask in tasks.cpp

float temperatureSensor() {
  // Your sensor code
  return 25.5;
}

// In the command handler:
if (cmd.equals("read_temp")) {
  float temp = temperatureSensor();
  LOG_ERROR(String("Temp: ") + String(temp), millis()/1000);
}
```

### Example 2: Scheduled Task

```cpp
// In bizTask, outside the message loop:

static uint32_t lastReading = 0;

if (millis() - lastReading > 60000) {  // Every minute
  lastReading = millis();
  float value = readMySensor();
  LOG_WIFI(String("Sensor: ") + String(value), millis()/1000);
}
```

## 🔍 Troubleshooting

### Common Issues

**OTA fails with "No OTA partition":**
* Your partition table doesn't include an OTA partition
* Use "Minimal SPIFFS" or "Default 4MB with OTA" partition scheme
* Check with `/api/ota/info`

**Flash corruption / random reboots:**
* Enable DEBUG_MODE
* Check logs via `/api/logs/*`
* Verify you're not writing to NVS from multiple tasks directly
* Use the flash write queue

**WiFi won't connect:**
* Check credentials via BLE
* View WiFi logs: `/api/logs/wifi`
* Verify DHCP vs. Static IP settings
* Check `MAX_WIFI_RECONNECT_ATTEMPTS`

**Task stack overflow:**
* Enable DEBUG_MODE
* Check `/api/tasks` for low stack_hwm
* Increase stack size in task creation

**Heap exhausted:**
* Check `/api/status` for heap_free
* Reduce allocated buffers
* Use static allocation where possible

---

## 📈 Performance Tips

1. **Task Priorities:**
   * systemTask: 2 (highest - WiFi is critical)
   * webTask: 1 (medium - HTTP serving)
   * bizTask: 1 (medium - your logic)
   * flashWriteTask: 0 (lowest - background)

2. **Core Affinity:**
   * Core 0: WiFi stack, system tasks
   * Core 1: Your business logic
   * Keep heavy computation on Core 1

3. **Watchdog:**
   * Feed every 5-10 seconds max
   * Never block for > 15 seconds
   * Use vTaskDelay() in loops

4. **Flash Writes:**
   * Use flash write queue (DEBUG_MODE)
   * Batch writes when possible
   * Avoid frequent writes

5. **Memory:**
   * Use static buffers for large allocations
   * Free messages promptly
   * Monitor heap via `/api/status`

---

## 🔐 Security Considerations

### Production Checklist

- [ ] **Add authentication to web interface**
  - Basic Auth, API keys, or OAuth
  - Protect all `/api/*` endpoints

- [ ] **Implement HTTPS**
  - Use ESP32's SSL/TLS support
  - Add certificate validation

- [ ] **Secure BLE pairing**
  - Enable BLE bonding
  - Require PIN or passkey

- [ ] **OTA signature verification**
  - Sign firmware with private key
  - Verify signature before flashing

- [ ] **Input validation**
  - Sanitize all user inputs
  - Validate command formats
  - Rate limit API requests

- [ ] **Network security**
  - Use WPA3 if available
  - Disable mDNS in production
  - Firewall configuration

---

## 🌐 Universal BLE WiFi Configurator PWA

A single-file Progressive Web App for configuring WiFi via BLE. Works on:
* **Android:** Chrome
* **iOS 16.4+:** Safari
* **Desktop:** Chrome, Edge, Opera

**Features:**
* No app installation needed
* Works offline after first load
* Can be added to home screen
* Universal BLE characteristic discovery
* DHCP and Static IP support

**Usage:**
1. Open `index.html` in a compatible browser
2. Click "Connect BLE"
3. Select your device
4. Configure WiFi settings
5. Click "Configure"

---

## 📋 Requirements

### Hardware
* ESP32, ESP32-S3, ESP32-C3, ESP32-C6, or ESP32-S2
* Partition scheme with OTA support (for OTA feature)

### Software
* Arduino framework (PlatformIO or Arduino IDE)
* ESP32 Arduino Core 2.0.0+

### Libraries
* **NimBLE-Arduino** (BLE support) - Auto-installed
* **ArduinoJson** (API responses) - Auto-installed
* ESP32 core libraries (WiFi, WebServer, etc.) - Included

### Development Tools
* **PlatformIO** (recommended) - Professional IDE
* **Arduino IDE 2.x** (alternative) - Simpler interface

---

## 🚀 Getting Started

### PlatformIO (Recommended)

1. Install PlatformIO in VS Code
2. Create new project or copy files
3. Add to `platformio.ini`:
```ini
[env:esp32]
platform = espressif32
board = esp32dev  ; or your board
framework = arduino
monitor_speed = 115200
```

4. Build and upload:
```bash
pio run --target upload
pio device monitor
```

### Arduino IDE

1. Install ESP32 board support
2. Copy all `.h`, `.cpp`, and `.ino` files to sketch folder
3. Open `.ino` file (others appear as tabs)
4. Select your board and port
5. Click Upload


---

## 📝 License

This template is provided as-is for educational and commercial use. Modify as needed for your projects.

---

## 🤝 Contributing

Contributions are welcome! Please:
* Focus on Version 2 (actively developed)
* Follow the existing module structure
* Add documentation for new features
* Test on multiple ESP32 variants

---

## 📚 Additional Resources

* [ESP32 Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
* [FreeRTOS Documentation](https://www.freertos.org/Documentation/RTOS_book.html)
* [NimBLE Documentation](https://github.com/h2zero/NimBLE-Arduino)
* [ArduinoJson Documentation](https://arduinojson.org/)
* [ESP32 OTA Updates](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html)
* [PlatformIO ESP32](https://docs.platformio.org/en/latest/platforms/espressif32.html)

---

**Ready to build?** Clone this repo and start developing your production-ready ESP32 application!

⭐ **Star this repo if it helped you!**
