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