/* ==============================================================================
   TASKS.H - FreeRTOS Task Declarations
   
   Declares all FreeRTOS task functions that run concurrently:
   - webTask: HTTP server handling
   - bizTask: Main business logic
   - flashWriteTask: Background NVS writes (DEBUG_MODE only)
   
   Each task runs independently with its own stack and priority.
   ============================================================================== */

/* Header guard to prevent multiple inclusion of tasks.h */
#ifndef TASKS_H
#define TASKS_H

#include <Arduino.h>
#include "types.h"

void systemTask(void* param);

void webTask(void* param);

void bizTask(void* param);

void initMessagePool();

ExecMessage* allocMessage();

void freeMessage(ExecMessage* msg);

#endif