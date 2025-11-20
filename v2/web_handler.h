/* ==============================================================================
   WEB_HANDLER.H - Web Server Interface
   
   Defines web server functionality:
   - REST API endpoints for system control and monitoring
   - File upload handling
   - CORS support for web applications
   - Web-based configuration interface
   
   Web server provides remote access to system functions via HTTP.
   ============================================================================== */

/* Header guard to prevent multiple inclusion of web_handler.h */
#ifndef WEB_HANDLER_H
#define WEB_HANDLER_H

#include <Arduino.h>
#include "config.h"

void registerRoutes();

void sendIndex();

void handleApiStatus();

void handleApiBizStart();

void handleApiBizStop();

void handleApiExec();

void handleApiNetwork();

#if DEBUG_MODE

void handleApiTasks();

void handleApiDebugLogs();

void handleApiDebugClear();
#endif

#if ENABLE_OTA

bool isOtaActive();

void sendBusyJson(const char* msg = "OTA in progress");
#else
inline bool isOtaActive() { return false; }
inline void sendBusyJson(const char* msg = "OTA in progress") { (void)msg; } 
#endif

#endif