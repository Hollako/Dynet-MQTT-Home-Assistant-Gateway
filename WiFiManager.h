#pragma once
#include "Globals.h"

// Setup / loop
void wifiSetup();
void wifiLoop();

// Controls
void startAP();
void stopAP();
void beginSTAIfCreds();
void updateWiFiSM();
void installWiFiDebugHandlers();

// Utils
const char* reasonToStr(uint8_t r);
