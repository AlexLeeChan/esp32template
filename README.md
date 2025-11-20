# ESP32(x) WiFi & BLE Template

A comprehensive, production-ready RTOS template for building robust applications on the ESP32, ESP32-S3, ESP32-C3, ESP32-C6, and other variants.

**Configure WiFi through BLE from your browser!** Includes OTA firmware updates, web dashboard, and advanced task monitoring.

---

## 📦 Available Versions

This project is available in two versions to suit different needs:

### [Version 1 - Simple Single-File](./README_V1.md) 
**Status:** ⚠️ No longer actively developed

* **Structure:** All-in-one single `.ino` file (~1000 lines)
* **Best for:** Quick prototyping, learning, simple projects
* **Features:** Basic WiFi, BLE, web dashboard, OTA updates
* **Pros:** Easy to understand, simple to modify
* **Cons:** Harder to maintain at scale, limited organization

**👉 [View Version 1 Documentation](./README_V1.md)**

---

### [Version 2 - Modular Multi-File](./README_V2.md)
**Status:** ✅ Actively developed and maintained

* **Structure:** Organized into 20+ separate modules
* **Best for:** Production deployments, complex projects, teams
* **Features:** Everything from V1 + enhanced debugging, NTP sync, persistent logging, flash-safe operations
* **Pros:** Clean architecture, easy to extend, production-ready
* **Cons:** More files to manage (but better organized)

**👉 [View Version 2 Documentation](./README_V2.md)**

---

## 🎯 Which Version Should You Use?

| Criteria | Version 1 | Version 2 |
|----------|-----------|-----------|
| **Learning/Tutorial** | ✅ Perfect | ⚠️ More complex |
| **Quick Prototype** | ✅ Ideal | ⚠️ Overkill |
| **Production Use** | ⚠️ Acceptable | ✅ Recommended |
| **Team Projects** | ❌ Hard to collaborate | ✅ Easy to split work |
| **Long-term Maintenance** | ⚠️ Gets messy | ✅ Clean & organized |
| **Feature Development** | ⚠️ Cramped | ✅ Spacious |

---

## 🚀 Core Features (Both Versions)

* **Multi-Chip Support:** ESP32, ESP32-S3, ESP32-C3, ESP32-C6, ESP32-S2
* **Web Dashboard:** Responsive dark-mode interface
* **BLE Configuration:** Set WiFi from any Bluetooth-enabled device
* **OTA Firmware Updates:** Upload new firmware over WiFi
* **Task Monitoring:** Real-time FreeRTOS task statistics
* **Robust Networking:** DHCP/Static IP with auto-reconnect
* **Zero-Malloc Queue:** Message pool for business logic
* **Watchdog Protection:** Automatic recovery from deadlocks

---

## 📱 Universal BLE WiFi Configurator PWA

Both versions include a **single-file Progressive Web App** that works on any modern browser (Android Chrome, iOS Safari 16.4+, Desktop Chrome/Edge).

**Features:**
* Configure WiFi via BLE without installing apps
* Works on phones, tablets, and computers
* Supports DHCP and Static IP configuration
* Can be "Added to Home Screen" for offline use

---

## 📚 Quick Start

### Version 1 (Single File)
```bash
# Arduino IDE or PlatformIO
1. Open the .ino file
2. Select your ESP32 board
3. Upload
4. Done!
```

### Version 2 (Modular)
```bash
# PlatformIO (recommended)
1. Copy all files to your src/ directory
2. Build with PlatformIO
3. Upload
4. Done!

# Arduino IDE
1. Copy all files to your sketch folder
2. Open the .ino file
3. Verify all files appear as tabs
4. Upload
```

---

## 🔧 Configuration

Both versions use similar configuration in `config.h`:

```cpp
#define DEBUG_MODE 1        // Enable detailed logging
#define ENABLE_OTA 1        // Enable firmware updates
#define WDT_TIMEOUT 20      // Watchdog timeout (seconds)
```

---

## 🌐 API Endpoints (Both Versions)

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/status` | System status (WiFi, heap, CPU, etc.) |
| GET | `/api/tasks` | FreeRTOS task statistics |
| POST | `/api/network` | Configure WiFi/network settings |
| POST | `/api/exec` | Send command to business logic task |
| POST | `/api/biz/start` | Start business logic processing |
| POST | `/api/biz/stop` | Stop business logic processing |
| GET | `/api/ota/status` | OTA update status |
| POST | `/api/ota/update` | Initiate firmware update |

---

## 📖 Documentation

* **[Version 1 Documentation](./README_V1.md)** - Single-file template guide
* **[Version 2 Documentation](./README_V2.md)** - Modular architecture guide

---

## 🤝 Contributing

Contributions are welcome! Please focus on **Version 2** for new features, as Version 1 is in maintenance mode.

---

## 📝 License

This template is provided as-is for educational and commercial use. Modify as needed for your projects.

---

## 🆘 Support

* **Issues:** Report bugs via GitHub Issues
* **Discussions:** Ask questions in GitHub Discussions
* **Documentation:** Check version-specific README files

---

## ⭐ Star This Repo

If this template helped you, please give it a star! It helps others discover the project.

---

**Ready to start?** Choose your version and dive into the full documentation:
* **[Version 1 (Simple) →](./README_V1.md)**
* **[Version 2 (Advanced) →](./README_V2.md)**
