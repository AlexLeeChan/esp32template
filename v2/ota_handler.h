/* ==============================================================================
   OTA_HANDLER.H - Over-The-Air Update Interface
   
   Provides functions for performing firmware updates over WiFi:
   - OTA status checking and reporting
   - OTA update initiation and progress tracking
   - Web API endpoints for OTA management
   - Partition information display
   
   OTA updates allow remote firmware upgrades without physical access.
   ============================================================================== */

/* Header guard to prevent multiple inclusion of ota_handler.h */
#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include "config.h"

#if ENABLE_OTA

#include <Arduino.h>

void registerOtaRoutes();

void handleOTAStatus();

void handleOTAUpdate();

void handleOTAReset();

void handleOTAInfo();

void deleteNonEssentialTasks();

void deleteWebTask();

void recreateTasks();

void dumpPartitionInfo(String& logOutput);

#endif

#endif