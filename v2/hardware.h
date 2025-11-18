#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>
#include "types.h"

// ============================================================================
// HARDWARE FUNCTIONS
// ============================================================================

/**
 * @brief Get internal temperature in Celsius
 * @return Temperature or NAN if not available
 */
float getInternalTemperatureC();

/**
 * @brief Get memory information
 * @return MemoryInfo struct
 */
MemoryInfo getMemoryInfo();

#endif // HARDWARE_H