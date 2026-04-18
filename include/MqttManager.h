#pragma once
#include "Globals.h"

// === Public API ===
void mqttSetup();
void mqttLoop();
void mqttEnsureConnected();

// Cross-module helpers
String availabilityTopic();

// Base topics
String areaBaseTopic(uint8_t area);
String channelBaseTopic(uint8_t area, uint8_t channel0);

// HA discovery + state publishing
void publishAvailability(const char* payload);

// Publish HA discovery for one entity/area (call when entities change)
void publishHADiscoveryForChannel(int idx);          // idx is EntityManager channel index
void publishHADiscoveryForArea(uint8_t area);        // temp/setpoint button, etc.

// Publish current states (you can call after you parse a DyNet report)
void publishStateForChannel(int idx);                // emits JSON for HA light/switch
void publishSensorsForArea(uint8_t area);            // temperature, setpoint

// MQTT callback (installed internally)
void mqttCallback(char* topic, byte* payload, unsigned int length);

// Publish current preset for an Area (1-based string or "unknown")
void publishPresetForArea(uint8_t area);