# ESP32(x) WiFi & BLE Template

This project is a comprehensive, multi-featured template for building robust applications on the ESP32, ESP32-S3, ESP32-C3, and other variants. It provides a "scaffolding" that gives any new project instant, powerful diagnostics and control capabilities right out of the box.

The core idea is to separate your custom "business logic" (in its own task) from the system's management, networking, and monitoring tasks. The template provides:

1.  A **Web Dashboard** for monitoring and configuration.
2.  A **BLE (Bluetooth Low Energy)** command interface for control.
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
