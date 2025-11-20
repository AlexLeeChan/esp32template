# ESP32(x) WiFi & BLE Template v1 - Single File Version
# Including BLE Configuration + OTA Updates

⚠️ **Note:** This is Version 1 - the simple, single-file template. It is **no longer actively developed**. For production use and new projects, please see **[Version 2 (Modular)](./README_V2.md)**.

---

This project is a comprehensive, RTOS template for building robust applications on the ESP32, ESP32-S3, ESP32-C3, ESP32-C6 and other variants. 

WiFi can be set through BLE Bluetooth application just from your browser! Just open the included webpage in browser on a Bluetooth device such as your Android/iOS phone. **Now with secure Over-The-Air (OTA) firmware updates!**

The core idea is to separate your custom "business logic" (in its own task) from the system's management, networking, and monitoring tasks. The template provides:

1.  A **Web Dashboard** for monitoring, configuration, and OTA updates.
2.  A **BLE (Bluetooth Low Energy)** command interface for control. Primarily for initial WiFi SSID and Password setup.
3.  A detailed **Per-Core FreeRTOS Task Monitor** to debug performance.
4.  A **Robust Multi-Task RTOS skeleton** with watchdog support.
5.  **OTA Firmware Updates** with graceful task shutdown and progress tracking.

---

## 🚀 Key Features

* **Multi-Chip Support:** Automatically detects and configures for ESP32, ESP32-S3, ESP32-C3, ESP32-C6, and ESP32-S2, enabling/disabling features like BLE and the internal temperature sensor where appropriate.
* **Dynamic Web Dashboard:** A responsive, dark-mode single-page web app (served from `PROGMEM`) for full system control.
* **BLE Command Interface:** Uses `NimBLE` for an efficient BLE server allowing for an alternative method of configuration and control.
* **OTA Firmware Updates:**
    * Web-based firmware upload from any HTTP/HTTPS URL.
    * Real-time progress tracking with percentage and file size display.
    * Graceful task shutdown to ensure reliable updates.
    * Automatic partition validation and safety checks.
    * Watchdog-aware update process (< 20s WDT timeout).
    * Dedicated "updating" UI that polls for completion and handles device reboot.
* **Advanced Task Monitoring:** Goes beyond `vTaskGetRunTimeStats` by calculating and displaying **per-core, per-task CPU utilization**, stack high-water mark, and task state.
* **Robust Networking:**
    * Configurable via Web or BLE.
    * Saves credentials (SSID/Password) to NVS (`Preferences`).
    * Supports both DHCP and Static IP configurations.
    * Includes a custom, non-blocking WiFi state machine with a reconnect backoff strategy.
* **Business Logic Skeleton:**
    * A dedicated `bizTask` (pinned to Core 1 on dual-core chips) for your custom application code, isolating it from system/comms tasks.
    * Features a **zero-malloc message pool** and a FreeRTOS queue (`execQ`) for safely sending commands to your logic task.
* **System-Wide Robustness:**
    * Uses the **ESP Task Watchdog Timer** on all critical tasks (`sys`, `web`, `biz`) to ensure the system recovers from deadlocks.
    * Correctly pins critical tasks to specific cores for performance and stability.
    * Graceful task exit mechanisms for safe OTA updates.
* **Memory Monitoring:**
    * Flash size detection and reporting.
    * PSRAM detection and usage tracking (if available).
    * Heap usage monitoring with min/max tracking.

---

## 🖥️ Web Dashboard & API

The web server (running on port 80) provides a full dashboard and a set of JSON API endpoints.

### Dashboard UI

* **System Status:** Live-updating card shows WiFi/BLE status, IP, RSSI, uptime, heap memory, core loads, chip temp, hardware info, flash size, and PSRAM (if available).
* **Business Module:** Start/Stop your custom `bizTask` and see its queue depth and processing count.
* **Command Exec:** Send string commands directly to the `bizTask`'s message queue (including `reset`/`reboot` commands).
* **WiFi Config:** Set SSID, password, and static IP configuration.
* **OTA Update:** Upload firmware from HTTP/HTTPS URLs with real-time progress tracking.
* **Task Monitor (`DEBUG_MODE`):** A detailed, live-updating table of all running FreeRTOS tasks, including:
    * Task Name
    * Core Affinity (0, 1, or ANY)
    * Priority
    * Stack High-Water Mark (with health color-coding)
    * Current State (Running, Blocked, etc.)
    * **Calculated CPU %** (relative to its core)
    * Total Runtime

### JSON API Endpoints

#### System & Monitoring
* `GET /api/status`: Returns a JSON object with system-wide status (WiFi, heap, core load, OTA availability, memory info, etc.).
* `GET /api/tasks`: Returns a JSON array of all monitored tasks and their stats.

#### Network Configuration
* `POST /api/network`: Sets WiFi and network configuration.
  ```json
  {
    "ssid": "NetworkName",
    "pass": "password",
    "dhcp": true,
    "static_ip": "192.168.1.100",  // if dhcp=false
    "gateway": "192.168.1.1",       // if dhcp=false
    "subnet": "255.255.255.0",      // if dhcp=false
    "dns": "8.8.8.8"                // if dhcp=false
  }
  ```

#### Business Logic Control
* `POST /api/exec`: Queues a command for the `bizTask`.
  ```json
  {"cmd": "your_command"}
  ```
  Special commands: `reset` or `reboot` will restart the device after 500ms.
* `POST /api/biz/start`: Starts the `bizTask`.
* `POST /api/biz/stop`: Pauses the `bizTask`.

#### OTA Updates (if `ENABLE_OTA` is 1)
* `GET /api/ota/status`: Returns current OTA status, partition info, and available space.
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
* `POST /api/ota/update`: Initiates an OTA update from a URL.
  ```json
  {"url": "http://example.com/firmware.bin"}
  ```
* `POST /api/ota/reset`: Resets OTA status and recreates tasks after a failed update.
* `GET /api/ota/info`: Returns detailed partition table information (for debugging).

---

## 📱 BLE Control

The device advertises under the name `RNGDS_ESP32*` (e.g., `RNGDS_ESP32S3`). You can connect with any BLE serial app (like nRF Connect) to the service `4faf...` and write to the RX characteristic (`beb5...`) to send commands (must be newline-terminated).

### Key BLE Commands

* `SET_WIFI|ssid|password`: Saves new WiFi credentials and reconnects.
* `SET_IP|DHCP`: Enables DHCP.
* `SET_IP|STATIC|ip|gateway|subnet|dns`: Sets a static IP configuration.
* `GET_STATUS`: Returns the current network status.
* `DISCONNECT_WIFI`: Manually disconnects from WiFi.
* `CLEAR_SAVED`: Clears saved WiFi credentials from NVS.
* `HEAP`: Returns free and minimum free heap.
* `TEMP`: Returns the internal chip temperature (if available).
* `RESTART`: Reboots the device.

---

## 🛠️ RTOS Architecture & How to Use

This project is designed as a template. You add your code primarily to `bizTask`.

### Task Structure

1.  **`systemTask` (Core 0, Priority 2):**
    * Handles WiFi connection state machine with exponential backoff.
    * Manages BLE advertising/reconnection.
    * Blinks the status LED based on BLE connection state.
    * Monitors system health.
    * Feeds the Task Watchdog.
    * Pauses WiFi management during OTA updates.

2.  **`webTask` (Core 0, Priority 1):**
    * Runs the `WebServer` (`server.handleClient()`).
    * Periodically calls `updateTaskMonitoring()` to gather all task statistics.
    * Serves the web dashboard and API endpoints.
    * Feeds the Task Watchdog.
    * Gracefully exits during OTA flash operation.

3.  **`bizTask` (Core 1, Priority 1):**
    * **This is your task.**
    * It's intentionally pinned to Core 1 (on dual-core chips) to avoid interfering with the WiFi/BLE stack on Core 0.
    * It idles until `gBizState` is `BIZ_RUNNING`.
    * When running, it waits on `execQ` for new `ExecMessage` commands (sent from the web or BLE).
    * It uses a high-performance **zero-malloc message pool** (`allocMessage`, `freeMessage`) to avoid heap fragmentation in a long-running task.
    * Handles `reset`/`reboot` commands with a 500ms delay for response delivery.
    * Gracefully exits during OTA updates.

### How to Add Your Code

1.  **Modify `bizTask`:** This is the main entry point for your application logic.
2.  **Process Messages:** In `bizTask`, add logic to parse the `msg->payload` (a C string) that you receive from the queue.
3.  **Send Commands:** Use the Web UI "Command Exec" box or the `/api/exec` endpoint to send test commands to your task.
4.  **Build:** Your application now automatically has a web dashboard, BLE control, OTA updates, and deep performance monitoring.

---

## 🔄 OTA Update Process

The OTA system is designed for reliability and safety:

### Update Flow
1. **Initiation:** User provides firmware URL via web interface.
2. **Pre-checks:** System validates available partition space and heap memory.
3. **Task Shutdown:** Non-essential tasks (`bizTask`) are gracefully stopped to free resources.
4. **Download:** Firmware is downloaded with real-time progress updates (supports HTTP redirects).
5. **Validation:** Content length and partition size are verified.
6. **Flash:** Download is written to the OTA partition (webTask continues during download for progress updates).
7. **Final Shutdown:** `webTask` exits just before flash finalization.
8. **Verification:** Update integrity is checked.
9. **Reboot:** Device automatically reboots to the new firmware.

### Safety Features
* **Watchdog Protection:** All operations complete within 20s WDT timeout.
* **Partition Validation:** Ensures target partition exists and has sufficient space.
* **Heap Monitoring:** Refuses updates if free heap < 50KB.
* **WiFi Protection:** Disables power save during updates; prevents WiFi management interference.
* **BLE Shutdown:** BLE stack is deinitialized to free memory.
* **Graceful Recovery:** Failed updates automatically recreate tasks and restore normal operation.
* **Progress Persistence:** OTA state survives brief disconnections for UI polling.

### OTA Configuration
Enable/disable OTA by setting `#define ENABLE_OTA 1` (or `0`) at the top of the sketch. This completely removes OTA code when disabled, saving flash space.

---

## 📊 Task Monitoring Details

The template includes sophisticated per-core task monitoring:

* **CPU Utilization:** Calculated per-task, relative to the core it's running on.
* **Core Affinity Tracking:** Shows which tasks are pinned to which core.
* **Stack Health:** Color-coded stack high-water marks (good/ok/low/critical).
* **Runtime Tracking:** Cumulative runtime in seconds for each task.
* **Dynamic Updates:** All metrics update every 500ms-3s without polling overhead.

---

## 🔧 Configuration Options

At the top of the sketch:
```cpp
#define DEBUG_MODE 1        // Enable detailed task monitoring table
#define ENABLE_OTA 1        // Enable OTA firmware updates
#define WDT_TIMEOUT 20      // Watchdog timeout in seconds
#define MSG_POOL_SIZE 10    // Message queue pool size
#define MAX_MSG_SIZE 256    // Maximum command length
```

---

## 🌐 Universal BLE WiFi Configurator PWA

This template includes a single-file, zero-dependency Progressive Web App (PWA) for configuring WiFi settings via BLE from any modern browser.

### ✨ Key Features

* **Single-File Application:** No build steps, no `npm install`. Just serve or open the file.
* **Truly Universal:** Automatically discovers the first available `write` (RX) and `notify` (TX) characteristics on *any* BLE device.
* **PWA Ready:** Can be "Added to Home Screen" on iOS and Android for a native-app-like experience.
* **Full WiFi Control:**
    * Set SSID and Password.
    * Configure for DHCP (Automatic IP).
    * Configure a Static IP, Gateway, Subnet, and DNS.
* **Robust Communication:** Includes a JavaScript command queue to prevent "GATT operation already in progress" errors.
* **Modern UI:** Clean, responsive interface built with Tailwind CSS (via CDN).
* **Live Status & Logging:** Features a real-time activity log and WiFi status display.

### 🚀 How to Use the PWA

1.  Save the included `index.html` file (from the PWA section of the docs).
2.  Open it in a compatible browser:
    * **Android:** Chrome
    * **iOS (16.4+):** Safari (Web BLE is supported)
    * **iOS (older):** [Bluefy](https://apps.apple.com/us/app/bluefy-web-ble-browser/id1492822055)
    * **Desktop:** Chrome, Edge, or Opera.
3.  Click **Connect BLE** and select your device from the popup.
4.  Once connected, the app will enable the input fields.
5.  Enter your WiFi credentials, configure IP settings, and click **Configure**.

---

## 📋 Requirements

* **Platform:** ESP32, ESP32-S3, ESP32-C3, ESP32-C6, or ESP32-S2
* **Framework:** Arduino (PlatformIO or Arduino IDE)
* **Libraries:**
    * NimBLE-Arduino (for BLE support on compatible chips)
    * ArduinoJson (for API responses)
    * ESP32 core libraries (WiFi, WebServer, Preferences, Update, etc.)

---

## 🎯 Use Cases

* **IoT Devices:** Sensors, actuators, and controllers with remote monitoring and OTA updates.
* **Prototyping:** Rapid development with built-in networking and debugging.
* **Production:** Robust watchdog protection and task isolation for reliable operation.
* **Education:** Learn RTOS concepts with a working, well-documented example.
* **Field Deployment:** OTA updates enable bug fixes and feature additions without physical access.

---

## 🔒 Security Considerations

* **BLE:** The BLE interface has no authentication. Implement pairing/bonding if needed for your application.
* **Web Interface:** Served over HTTP with no authentication. Consider adding basic auth or serving over HTTPS for production deployments.
* **OTA Updates:** Accepts firmware from any HTTP/HTTPS URL. In production, validate firmware signatures or restrict update sources.
* **Commands:** The `/api/exec` endpoint allows arbitrary command execution. Implement authentication and command validation for production use.

---

## ⚠️ Migration to Version 2

For new projects or production deployments, we strongly recommend **[Version 2 (Modular)](./README_V2.md)**, which offers:

* ✅ Better code organization with 20+ modules
* ✅ Enhanced debugging with persistent logging
* ✅ NTP time synchronization
* ✅ Flash-safe write operations
* ✅ More robust error handling
* ✅ Easier to maintain and extend

**[Migrate to Version 2 →](./README_V2.md)**

---

## 📝 License

This template is provided as-is for educational and commercial use. Modify as needed for your projects.

---

## 📚 Additional Resources

* [ESP32 Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
* [FreeRTOS Documentation](https://www.freertos.org/Documentation/RTOS_book.html)
* [NimBLE Documentation](https://github.com/h2zero/NimBLE-Arduino)
* [ArduinoJson Documentation](https://arduinojson.org/)
* [ESP32 OTA Updates Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html)
