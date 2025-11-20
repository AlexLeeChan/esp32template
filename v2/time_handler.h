/* ==============================================================================
   TIME_HANDLER.H - Time Management Interface
   
   Provides time-related functions:
   - NTP synchronization
   - Epoch timestamp retrieval
   - Time formatting
   
   Ensures accurate timekeeping for logging and scheduling.
   ============================================================================== */

/* Header guard to prevent multiple inclusion of time_handler.h */
#ifndef TIME_HANDLER_H
#define TIME_HANDLER_H

#include <Arduino.h>

void initNTP();

void syncNTP();

bool getTimeInitialized();

String getCurrentTimeString();

uint32_t getEpochTime();

bool shouldSyncNTP();

#endif