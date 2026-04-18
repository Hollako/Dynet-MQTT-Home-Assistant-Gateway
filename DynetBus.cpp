#include "DynetBus.h"
#include "EntityManager.h"   // DynetEntities::em.handleLogicalFrame()

// ================== UART / IO ==================
#if defined(ESP8266)
  #include <SoftwareSerial.h>
  static SoftwareSerial* sSS = nullptr;   // RX, TX, invert=false
  static Stream*         io  = nullptr;
#else
  // ESP32: use UART1 by default
  static HardwareSerial* sHS = nullptr;
  static Stream*         io  = nullptr;
#endif

// Small helper for DE/RE direction
static inline void setTX(bool on) {
  if (dePin >= 0) digitalWrite(dePin, on ? HIGH : LOW);
}

// ---- Auto-sync window (poll preset briefly after bus activity) ----
static uint32_t g_syncUntilMs = 0;
static uint8_t  g_syncAreaHint = 0;     // 0 = round-robin all areas, otherwise poll this area only
static uint8_t  g_rrArea = 2;
static uint32_t g_nextPresetPollAt = 0;

static inline void dynetRequestSync(uint8_t areaHint, uint32_t durationMs) {
  g_syncAreaHint = areaHint;                 // 0 or area number
  g_syncUntilMs  = millis() + durationMs;    // enable polling window
  if (g_nextPresetPollAt < millis()) g_nextPresetPollAt = millis();
}

// ================== DynetBus ==================
DynetBus dynet;

// -------- checksum: two’s complement of sum(bytes 0..6) --------
uint8_t DynetBus::checksum(const uint8_t f[8]) {
  uint16_t s = 0;
  for (int i = 0; i < 7; ++i) s += f[i];
  return (uint8_t)(0 - (s & 0xFF));
}

// -------- low-level 8-byte writer with DE/RE handling ----------
void DynetBus::write8(const uint8_t f[8]) {
  // Log TX so you can “see yourself” on RS485 (you don’t hear your own TX)
  LOGF("[DyNet TX] %02X %02X %02X %02X %02X %02X %02X %02X\n",
                f[0],f[1],f[2],f[3],f[4],f[5],f[6],f[7]);

  if (!io) return;
  if (dePin >= 0) { digitalWrite(dePin, HIGH); delayMicroseconds(12); }
  io->write(f, 8);
  io->flush();
  if (dePin >= 0) { delayMicroseconds(12); digitalWrite(dePin, LOW); }
}

// -------- public: begin (bring up UART and DE/RE) --------------
void DynetBus::begin() {
  if (dePin >= 0) { pinMode(dePin, OUTPUT); setTX(false); }

#if defined(ESP8266)
  // If user selected the hardware UART0 pins (RX=3, TX=1) use Serial
  if (rxPin == 3 && txPin == 1) {
    Serial.setDebugOutput(false);    // avoid SDK logs on the bus
    Serial.flush();
    Serial.end();
    delay(5);
    Serial.begin(9600);              // set your DyNet baud as needed
    io = &Serial;
  } else {
    if (sSS) { delete sSS; sSS = nullptr; }
    sSS = new SoftwareSerial(rxPin, txPin, false);  // (rx, tx, invert=false)
    sSS->enableIntTx(false);
    sSS->begin(9600);
    io = sSS;
  }
#else
  if (!sHS) sHS = new HardwareSerial(1);
  sHS->begin(9600, SERIAL_8N1, rxPin, txPin);       // change baud if needed
  io = sHS;
#endif

  _rxPos = 0;
}

// -------- public: loop (streaming 8-byte frame builder) --------
void DynetBus::loop() {
  if (!io) return;

  while (io->available()) {
    uint8_t c = (uint8_t)io->read();

    // Hard resync: 0x1C is start-of-logical-frame
    if (c == 0x1C) {
      _rxPos = 0;
      _rxBuf[_rxPos++] = c;
      continue;
    }

    // If we haven't seen a start byte yet, ignore
    if (_rxPos == 0) continue;

    _rxBuf[_rxPos++] = c;
    if (_rxPos < 8) continue;

    // We have 8 bytes starting with 0x1C
    const uint8_t want = checksum(_rxBuf);

    if (want == _rxBuf[7]) {
    // Start a short sync window for this area whenever we see valid bus traffic.
    // This ensures Dynalite-origin changes get discovered even if we miss the exact command opcode.
    const uint8_t area = _rxBuf[1];
    dynetRequestSync(area, 12000);   // 12 seconds of preset polling after activity

    DynetEntities::em.handleLogicalFrame(_rxBuf);

    } else {
      // Keep a useful log for diagnosing checksum/framing
      LOGF("[DyNet RX BADCHK] %02X %02X %02X %02X %02X %02X %02X %02X | want=%02X\n",
          _rxBuf[0],_rxBuf[1],_rxBuf[2],_rxBuf[3],_rxBuf[4],_rxBuf[5],_rxBuf[6],_rxBuf[7],
          want);
    }
    _rxPos = 0; // wait for next 0x1C
  }
}

// -------- public: periodic state polling (levels sweep) --------
void DynetBus::pollAreas() {
  // --- Manual sweep mode (existing behavior) ---
  if (areasSweepActive) {
    if (millis() < areasSweepNextAt) return;

    const uint8_t maxAreas = (cfg.dynet_max_areas    ? cfg.dynet_max_areas    : (uint8_t)DYNET_MAX_AREAS);
    const uint8_t maxCh    = (cfg.dynet_max_channels ? cfg.dynet_max_channels : (uint8_t)DYNET_MAX_CHANNELS);

    uint8_t a = areasSweepArea;
    if (a < 2) a = 2;

    if (a > maxAreas) {
      areasSweepActive = false;
      areasSweepArea   = 2;
      return;
    }

    requestPreset(a);
    for (uint8_t ch = 0; ch < maxCh; ++ch) {
      sendRequestChannelLevel(a, ch);
      delay(2);
    }
    LOGF("[DyNet] sweep area=%u requested=%u channels\n", a, maxCh);

    areasSweepArea = (uint8_t)(a + 1);
    if (areasSweepArea > maxAreas) {
      areasSweepActive = false;
      areasSweepArea   = 2;
    }
    areasSweepNextAt = millis() + 1000;
    return;
  }

  if ((int32_t)(millis() - g_syncUntilMs) < 0) {

      if (millis() < g_nextPresetPollAt) return;

      const uint8_t maxAreas = (cfg.dynet_max_areas ? cfg.dynet_max_areas : (uint8_t)DYNET_MAX_AREAS);

      uint8_t a = 0;
      if (g_syncAreaHint >= 2 && g_syncAreaHint <= maxAreas) {
        a = g_syncAreaHint;   // poll only the active area
      } else {
        // round-robin all areas
        if (g_rrArea < 2) g_rrArea = 2;
        if (g_rrArea > maxAreas) g_rrArea = 2;
        a = g_rrArea++;
      }

      requestPreset(a);                    // 0x63 -> expect 0x62
      g_nextPresetPollAt = millis() + 1200; // throttle (1.2s). Tune 700–2000ms.

      return;
    }
}

// ================== TX HELPERS (opcode in b[2]) ==================
//
// All frames use: [1C][Area][Opcode][D1][D2][D3][Join][Chk]
// Join defaults to 0xFF unless you have banked/joined logic.
//

// Recall/select preset with optional fade (ms)
// map 1..8 within bank to DyNet "code" (00,01,02,03,0A,0B,0C,0D)
static inline uint8_t dy64_code_for_preset(uint8_t preset1) {
  uint8_t idx = (uint8_t)((preset1 - 1) % 8);
  switch (idx) {
    case 0: return 0x00; case 1: return 0x01; case 2: return 0x02; case 3: return 0x03;
    case 4: return 0x0A; case 5: return 0x0B; case 6: return 0x0C; default: return 0x0D;
  }
}

void DynetBus::requestSync(uint8_t area, uint32_t durationMs) {
  _syncAreaHint = area;                 // 0 means "round robin all"
  _syncUntilMs = millis() + durationMs; // enable polling window
}

void DynetBus::sendAreaPreset(uint8_t area, uint8_t preset) {
  sendAreaPreset(area, preset, 0);
}

void DynetBus::sendAreaPreset(uint8_t area, uint8_t preset, uint16_t fadeMs) {
  if (area == 0)   area   = 1;
  if (preset == 0) preset = 1;

  uint8_t bank = (uint8_t)((preset - 1) / 8);           // BYTE[5]
  uint8_t code = dy64_code_for_preset(preset);          // BYTE[3]
  uint8_t fade = (uint8_t)((fadeMs / 20) & 0xFF);       // BYTE[4] only (Lo)

  // [1C][Area][64][Code][FadeLo][Bank][FF][Chk]
  uint8_t f[8] = { 0x1C, area, 0x64, code, fade, bank, 0xFF, 0x00 };
  f[7] = checksum(f);
  write8(f);
}


// Request current preset for an area
void DynetBus::requestPreset(uint8_t area) {
  uint8_t f[8] = { 0x1C, area, 0x00, 0x63, 0xFF, 0x00, 0xFF, 0x00 };
  f[7] = checksum(f);
  write8(f);
}

// Fade a single channel to a percentage using a ramp code (100ms units)
void DynetBus::sendFadeToLevel_1s(uint8_t area, uint8_t ch0, uint8_t pct, uint8_t rampCode) {
  if (pct > 100) pct = 100;

  // DyNet level mapping: 0x01 = 100%, 0xFF = 0%
  uint8_t level = (pct == 0)   ? 0xFF :
                  (pct == 100) ? 0x01 :
                  (uint8_t)(0xFF - ((pct * 254) / 100));

  uint8_t f[8] = { 0x1C, area, ch0, 0x71, level, rampCode, 0xFF, 0x00 };
  f[7] = checksum(f);
  write8(f);
}

// Ask level for one channel
void DynetBus::sendRequestChannelLevel(uint8_t area, uint8_t ch0) {
  uint8_t f[8] = { 0x1C, area, ch0, 0x61, 0x00, 0x00, 0xFF, 0x00 };
  f[7] = checksum(f);
  write8(f);
}

// (Alias kept for compatibility with older code)
void DynetBus::sendRequestPreset(uint8_t area) { requestPreset(area); }

// Fade entire area to a preset with linear time (fade in 20ms steps)
void DynetBus::sendFadeToPreset_linear(uint8_t area, uint8_t preset0, uint8_t fade20ms) {
  uint8_t f[8] = { 0x1C, area, 0x6B, 0xFF, preset0, fade20ms, 0xFF, 0x00 };
  f[7] = checksum(f);
  write8(f);
}

// Select preset with a 16-bit fade (20ms units) – convenient alternative
void DynetBus::sendSelectPreset_linear(uint8_t area, uint8_t preset0, uint16_t fade20ms16) {
  uint8_t f[8] = { 0x1C, area, 0x65, preset0,
                   (uint8_t)(fade20ms16 & 0xFF), (uint8_t)(fade20ms16 >> 8),
                   0xFF, 0x00 };
  f[7] = checksum(f);
  write8(f);
}

// Program (save) the current preset for the area
void DynetBus::sendProgramCurrentPreset(uint8_t area) {
  uint8_t f[8] = { 0x1C, area, 0x00, 0x08, 0x00, 0x00, 0xFF, 0x00 };
  f[7] = checksum(f);
  write8(f);
}

// Send HVAC setpoint in q0.25°C (e.g., 22.5°C => 90)
void DynetBus::sendSetTempSetpoint_q025(uint8_t area, float tempC) {
  int16_t q = (int16_t)roundf(tempC * 4.0f);
  uint8_t f[8] = { 0x1C, area, 0x07, 0x48, (uint8_t)(q >> 8), (uint8_t)(q & 0xFF), 0xFF, 0x00 };
  f[7] = checksum(f);
  write8(f);
}

// ================== C-style wrappers for .ino ==================
void dynetSetup()     { dynet.begin(); }
void dynetLoop()      { dynet.loop();  }
void dynetPollAreas() { dynet.pollAreas(); }
