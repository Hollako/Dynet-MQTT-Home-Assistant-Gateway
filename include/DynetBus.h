#pragma once
#include <Arduino.h>
#include "Globals.h"
#include "EntityManager.h"  // for DynetEntities::em.handleLogicalFrame()

// ---------- DyNet opcodes (PLACEHOLDERS – replace with spec values) ----------
enum : uint8_t {
  OP_REPORT_LOGICAL   = 0x1C, // logical frame header (if used in your spec)
  OP_CMD_FADE_LEVEL   = 0x31, // write channel level with fade (example)
  OP_REQ_CH_LEVEL     = 0x61, // request channel level (example)
  OP_REQ_ALL_CH       = 0x63, // request all channel levels for area (example)
  OP_REPORT_LEVEL     = 0x60, // report channel level (example)
  OP_REQ_PRESET       = 0x62, // request active preset for area (example)
  OP_SAVE_PRESET      = 0x50, // program/store current preset (example)
  OP_SET_SETPOINT_Q25 = 0x4B, // set HVAC setpoint in q0.25°C units (example)
};

// ---------- 8-byte physical frame ----------
struct DynetPhysFrame {
  uint8_t b[8];  // b[7] is checksum
};

class DynetBus {
public:
  void begin(int rxPin, int txPin, int dePin = -1, uint32_t baud = 9600);
  void begin();       // init UART + DE/RE
  void loop();        // read bytes, assemble frames, dispatch to EntityManager
  void pollAreas();   // lightweight periodic polling (optional)
  void requestPreset(uint8_t area);            // ask controller to report current preset (0x63)
  void requestChannelLevel(uint8_t area, uint8_t ch0); // ask level for channel (0-origin) (0x61)
  void poll();  

  // ---- TX helpers (called from MQTT/WebUI) ----
  // Fade to level (pct 0..100, rampCode is an app-specific byte you used in your WebUI)
  void sendFadeToLevel_1s(uint8_t area, uint8_t ch0, uint8_t pct, uint8_t rampCode);
  void sendRequestChannelLevel(uint8_t area, uint8_t ch0);
  void sendRequestPreset(uint8_t area);
  void sendProgramCurrentPreset(uint8_t area);
  void sendSetTempSetpoint_q025(uint8_t area, float tempC);
  void sendAreaPreset(uint8_t area, uint8_t preset);                       // instant
  void sendAreaPreset(uint8_t area, uint8_t preset, uint16_t fadeMs);      // with fade
  void sendFadeToPreset_linear(uint8_t area, uint8_t preset0, uint8_t fade20ms);
  void sendSelectPreset_linear(uint8_t area, uint8_t preset0, uint16_t fade20ms16); // 2.0s   

  // Non-blocking deferred level-request queue — processed one-per-call in pollAreas()
  void scheduleLevelReq(uint8_t area, uint8_t ch0, uint32_t afterMs);
  void scheduleAreaLevelReqs(uint8_t area, uint32_t baseAfterMs = 400);

private:
  void write8(const uint8_t f[8]);
  static uint8_t checksum(const uint8_t f[8]); // sum of first 7 bytes (placeholder)

  // RX state
  uint8_t _rxBuf[8] = {0};
  uint8_t _rxPos    = 0;

  // Deferred level-request queue
  struct LvlReq { uint8_t area; uint8_t ch0; uint32_t sendAt; };
  static const uint8_t LVLQ_SIZE = 64;
  LvlReq   _lvlQ[64] = {};
  uint8_t  _lvlQCount = 0;

};

// Global bus singleton
extern DynetBus dynet;
extern void onDynetReportChannelLevel(uint8_t area, uint8_t ch, uint8_t pct);
extern void onDynetReportPreset(uint8_t area, uint8_t preset0);

// Return the number of active channels in this area (0 = unknown)
// If not provided, poll() will fall back to DYNET_MAX_CHANNELS or 48.
extern uint8_t dynetActiveChannelCount(uint8_t area);

// Return whether a channel is active/known in this area.
// If not provided, poll() will assume all channels are active.
extern bool dynetIsChannelActive(uint8_t area, uint8_t ch);

// C-style wrappers used by your .ino
void dynetSetup();
void dynetLoop();
void dynetPollAreas();
