/* ==============================================================================
   HARDWARE.H - Hardware Abstraction Interface
   
   Provides hardware-specific functions:
   - Memory information (flash, PSRAM)
   - Temperature sensor reading
   - Hardware capability detection
   
   Abstracts hardware differences between ESP32 variants.
   ============================================================================== */

/* Header guard to prevent multiple inclusion of hardware.h */
#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>
#include "types.h"

float getInternalTemperatureC();

MemoryInfo getMemoryInfo();

#endif