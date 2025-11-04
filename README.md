# ESP32(x) WiFi & BLE Template 
# Including BLE configuration application as simple HTML file 

This project is a comprehensive, RTOS template for building robust applications on the ESP32, ESP32-S3, ESP32-C3, ESP32-C6 and other variants. 
Wifi can be set through BLE Bluetooth application just from your browser! Just open included webpage in browser on Bluetooth device such as your Android/IOS phone.

The core idea is to separate your custom "business logic" (in its own task) from the system's management, networking, and monitoring tasks. The template provides:

1.  A **Web Dashboard** for monitoring and configuration.
2.  A **BLE (Bluetooth Low Energy)** command interface for control. Primarily for intital Wifi SSID and Password setup.
3.  A detailed **Per-Core FreeRTOS Task Monitor** to debug performance.
4.  A **Robust Multi-Task RTOS skeleton** with watchdog support.

## 🚀 Key Features

* **Multi-Chip Support:** Automatically detects and configures for ESP32, ESP32-S3, ESP32-C3, ESP32-C6, and ESP32-S2, enabling/disabling features like BLE and the internal temperature sensor where appropriate.
* **Dynamic Web Dashboard:** A responsive, dark-mode single-page web app (served from `PROGMEM`) for full system control.
* **BLE Command Interface:** Uses `NimBLE` for an efficient BLE server allowing for an alternative method of configuration and control.
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

---

## 🖥️ Web Dashboard & API

The web server (running on port 80) provides a full dashboard and a set of JSON API endpoints.

### Dashboard UI



* **System Status:** Live-updating card shows WiFi/BLE status, IP, RSSI, uptime, heap memory, core loads, chip temp, and hardware info.
* **Business Module:** Start/Stop your custom `bizTask` and see its queue depth and processing count.
* **Command Exec:** Send string commands directly to the `bizTask`'s message queue.
* **WiFi Config:** Set SSID, password, and static IP configuration.
* **Task Monitor (`DEBUG_MODE`):** A detailed, live-updating table of all running FreeRTOS tasks, including:
    * Task Name
    * Core Affinity (0, 1, or ANY)
    * Priority
    * Stack High-Water Mark (with health color-coding)
    * Current State (Running, Blocked, etc.)
    * **Calculated CPU %** (relative to its core)
    * Total Runtime

### JSON API Endpoints

* `GET /api/status`: Returns a JSON object with system-wide status (WiFi, heap, core load, etc.).
* `GET /api/tasks`: Returns a JSON array of all monitored tasks and their stats.
* `POST /api/network`: Sets WiFi and network configuration.
* `POST /api/exec`: Queues a command (JSON: `{"cmd":"your_command"}`) for the `bizTask`.
* `POST /api/biz/start`: Starts the `bizTask`.
* `POST /api/biz/stop`: Pauses the `bizTask`.

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
* `TEMP`: Returns the internal chip temperature.
* `RESTART`: Reboots the device.

---

## 🛠️ RTOS Architecture & How to Use

This project is designed as a template. You add your code primarily to `bizTask`.

### Task Structure

1.  **`systemTask` (Core 0, Priority 2):**
    * Handles WiFi connection state machine.
    * Manages BLE advertising/reconnection.
    * Blinks the status LED.
    * Feeds the Task Watchdog.

2.  **`webTask` (Core 0, Priority 1):**
    * Runs the `WebServer` (`server.handleClient()`).
    * Periodically calls `updateTaskMonitoring()` to gather all task statistics.
    * Feeds the Task Watchdog.

3.  **`bizTask` (Core 1, Priority 1):**
    * **This is your task.**
    * It's intentionally pinned to Core 1 (on dual-core chips) to avoid interfering with the WiFi/BLE stack on Core 0.
    * It idles until `gBizState` is `BIZ_RUNNING`.
    * When running, it waits on `execQ` for new `ExecMessage` commands (sent from the web or BLE).
    * It uses a high-performance **zero-malloc message pool** (`allocMessage`, `freeMessage`) to avoid heap fragmentation in a long-running task.

### How to Add Your Code

1.  **Modify `bizTask`:** This is the main entry point for your application logic.
2.  **Process Messages:** In `bizTask`, add logic to parse the `msg->payload` (a C string) that you receive from the queue.
3.  **Send Commands:** Use the Web UI "Command Exec" box or the `/api/exec` endpoint to send test commands to your task.
4.  **Build:** Your application now automatically has a web dashboard, BLE control, and deep performance monitoring.

# Universal BLE WiFi Configurator PWA

This is a single-file, zero-dependency Progressive Web App (PWA) for configuring the WiFi settings on virtually any BLE-enabled device (like an ESP32, nRF, etc.).

The entire application—HTML, CSS (via Tailwind CDN), JavaScript, PWA Manifest, and Service Worker—is contained within this single `index.html` file. This makes it incredibly easy to deploy by simply hosting it on any web server or even using it from a local copy.



## ✨ Key Features

* **Single-File Application:** No build steps, no `npm install`. Just serve or open the file.
* **Truly Universal:** Automatically discovers the first available `write` (RX) and `notify` (TX) characteristics on *any* BLE device. It does not require specific service or characteristic UUIDs.
* **PWA Ready:** Can be "Added to Home Screen" on iOS and Android for a native-app-like experience, complete with an icon and offline caching.
* **Full WiFi Control:**
    * Set SSID and Password.
    * Configure for DHCP (Automatic IP).
    * Configure a Static IP, Gateway, Subnet, and DNS.
* **Robust Communication:** Includes a JavaScript command queue to prevent "GATT operation already in progress" errors.
* **Modern UI:** Clean, responsive interface built with Tailwind CSS (via CDN).
* **Live Status & Logging:** Features a real-time activity log and WiFi status display.

## 🚀 How to Use

1.  Save this `index.html` file.
2.  Open it in a compatible browser:
    * **Android:** Chrome
    * **iOS (16.4+):** Safari (Web BLE is supported)
    * **iOS (older):** [Bluefy](https://apps.apple.com/us/app/bluefy-web-ble-browser/id1492822055)
    * **Desktop:** Chrome, Edge, or Opera.
3.  Click **Connect BLE** and select your device from the popup.
4.  Once connected, the app will enable the input fields.
5.  Enter your WiFi credentials, configure IP settings, and click **Configure**.

## 펌 Firmware API Contract

To make your hardware compatible with this app, your BLE device (e.g., ESP32) must implement the following simple, newline-terminated (`\n`) string protocol.

### ➡️ Commands (App to Device)

The app sends these commands to your device's `write` characteristic:

* `GET_STATUS`: Request the current WiFi status.
* `SET_WIFI|ssid|password`: Set the WiFi credentials. `password` can be blank to keep a previously saved one.
* `SET_IP|DHCP`: Configure the device to use DHCP.
* `SET_IP|STATIC|ip|gateway|subnet|dns`: Configure a static IP.
* `DISCONNECT`: Command the device to disconnect from the current WiFi network.
* `CLEAR_WIFI`: Command the device to clear all saved WiFi credentials.

### ⬅️ Responses (Device to App)

Your device must send these pipe-delimited (`|`) strings to the app via its `notify` characteristic:

* **Main Status Response:**
    * `STATUS|state|ssid|ip|rssi|ip_mode|static_ip`
    * **Example (Connected):** `STATUS|CONNECTED|MyNetwork|192.168.1.50|-65|STATIC|192.168.1.50`
    * **Example (Disconnected):** `STATUS|DISCONNECTED||||DHCP|`

* **Quick Status Updates:**
    * `CONNECTED|ip`: Sent right after a successful connection.
    * `DISCONNECTED`: Sent after a disconnection.
    * `CONNECTING`: Sent while attempting to connect.

* **Acknowledgements:**
    * `OK:Message`: A generic success message (e.g., `OK:WIFI_CLEARED`).
    * `ERR:Message`: A generic error message (e.g., `ERR:INVALID_IP`).
