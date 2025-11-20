/* ==============================================================================
   HARDWARE.CPP - Hardware Abstraction Implementation
   
   Implements hardware access functions:
   - Queries flash and PSRAM sizes
   - Reads internal temperature sensor (if available)
   - Detects hardware capabilities
   
   Handles differences between ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6.
   ============================================================================== */

#include "hardware.h"
#include "globals.h"
#include "debug_handler.h"

float getInternalTemperatureC() {
#if ESP32_HAS_TEMP
  if (s_temp_sensor == NULL) return NAN;
  float tsens_out;
  if (temperature_sensor_get_celsius(s_temp_sensor, &tsens_out) == ESP_OK) {
    return tsens_out;
  }
  #if DEBUG_MODE
  else {
    addErrorLog(F("Failed to read temperature"), millis() / 1000);
  }
  #endif
#endif
  return NAN;
}

MemoryInfo getMemoryInfo() {
  MemoryInfo info;
  info.flashSizeMB = ESP.getFlashChipSize() / (1024 * 1024);

#if CONFIG_SPIRAM_SUPPORT || CONFIG_ESP32_SPIRAM_SUPPORT
  info.hasPsram = psramFound();
  if (info.hasPsram) {
    info.psramSizeBytes = ESP.getPsramSize();
    info.psramFreeBytes = ESP.getFreePsram();
  } else {
    info.psramSizeBytes = 0;
    info.psramFreeBytes = 0;
  }
#else
  info.hasPsram = false;
  info.psramSizeBytes = 0;
  info.psramFreeBytes = 0;
#endif

  return info;
}