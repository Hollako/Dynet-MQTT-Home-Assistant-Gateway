#pragma once
#include "Globals.h"

void registerWebRoutes();
void handleRestoreBackupUpload();
void handleRestoreBackupPost();

// explicit endpoints (optional to call directly)
void handleRootGet();
void handleConfigGet();
void handleConfigPost();

void sendRebootingPage(const char* title, const char* msg, int seconds, int delayMs);