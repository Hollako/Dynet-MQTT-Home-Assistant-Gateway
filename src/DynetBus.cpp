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
      LOGF("[DyNet RX] %02X %02X %02X %02X %02X %02X %02X %02X\n",
           _rxBuf[0],_rxBuf[1],_rxBuf[2],_rxBuf[3],_rxBuf[4],_rxBuf[5],_rxBuf[6],_rxBuf[7]);
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

// -------- Deferred level-request queue --------
void DynetBus::scheduleLevelReq(uint8_t area, uint8_t ch0, uint32_t afterMs) {
  uint32_t sendAt = millis() + afterMs;
  // Deduplicate: if same area+ch0 already queued, keep the earlier sendAt
  for (uint8_t i = 0; i < _lvlQCount; i++) {
    if (_lvlQ[i].area == area && _lvlQ[i].ch0 == ch0) {
      if ((int32_t)(sendAt - _lvlQ[i].sendAt) < 0) _lvlQ[i].sendAt = sendAt;
      return;
    }
  }
  if (_lvlQCount >= LVLQ_SIZE) return; // queue full — drop
  _lvlQ[_lvlQCount++] = {area, ch0, sendAt};
}

void DynetBus::scheduleAreaLevelReqs(uint8_t area, uint32_t baseAfterMs) {
  using namespace DynetEntities;
  uint32_t base = millis() + baseAfterMs;
  int slot = 0;
  for (int i = 0; i < em.channelsCount(); i++) {
    const auto& c = em.channelAt(i);
    if (c.present && c.area == area) {
      scheduleLevelReq(area, c.channel0, base + (uint32_t)(slot * 80) - millis());
      slot++;
    }
  }
  if (slot == 0) {
    // No known channels — probe first 8 to allow initial discovery
    uint8_t maxCh = cfg.dynet_max_channels ? cfg.dynet_max_channels : 8;
    if (maxCh > 8) maxCh = 8;
    for (uint8_t ch = 0; ch < maxCh; ch++) {
      scheduleLevelReq(area, ch, base + (uint32_t)(ch * 80) - millis());
    }
  }
  LOGF("[DyNet] scheduled %d level req(s) for A%u after %ums\n", (slot?slot:8), area, baseAfterMs);
}

// -------- public: periodic state polling (levels sweep) --------
void DynetBus::pollAreas() {
  // --- Manual sweep mode ---
  // Non-blocking: sends ONE request per call, then returns so DynetBus::loop()
  // can process UART responses before the next request.  This prevents both
  // bus collisions (we were transmitting before the device finished responding)
  // and SoftwareSerial buffer overflows (64-byte limit) that caused missed channels
  // when the old code fired all requests in a tight delay(2) loop.
  // Runs up to 3 automatic passes so transient misses are caught on the next round.

  // ── Process one deferred level-request queue item ──────────────────────
  if (_lvlQCount > 0) {
    uint32_t now = millis();
    uint8_t best = 0xFF;
    for (uint8_t i = 0; i < _lvlQCount; i++) {
      if ((int32_t)(now - _lvlQ[i].sendAt) >= 0) {
        if (best == 0xFF || (int32_t)(_lvlQ[i].sendAt - _lvlQ[best].sendAt) < 0)
          best = i;
      }
    }
    if (best != 0xFF) {
      sendRequestChannelLevel(_lvlQ[best].area, _lvlQ[best].ch0);
      _lvlQ[best] = _lvlQ[--_lvlQCount]; // compact
      return; // one TX per call to avoid bus collisions
    }
  }

  if (areasSweepActive) {
    if ((int32_t)(millis() - areasSweepNextAt) < 0) return;  // not time yet

    // Cap maxAreas the same way EntityManager::begin() does — prevents uint8_t
    // overflow when cfg.dynet_max_areas >= 255 (254+1 wraps to 0, resetting the
    // sweep to area 2 and creating an infinite loop).
    const uint8_t arCap  = (uint8_t)constrain(
        (int)(cfg.dynet_max_areas ? cfg.dynet_max_areas : DYNET_MAX_AREAS),
        1, (int)DYNET_MAX_AREAS);
    const uint8_t maxCh  = (uint8_t)constrain(
        (int)(cfg.dynet_max_channels ? cfg.dynet_max_channels : DYNET_MAX_CHANNELS),
        1, (int)DYNET_MAX_CHANNELS);

    // Early exit: all area slots are full — no point probing more area numbers
    if (DynetEntities::em.areasCount() >= (int)arCap) {
      areasSweepActive  = false;
      areasSweepArea    = 2;
      areasSweepChannel = 0;
      LOGF("[DyNet] sweep stopped: area capacity full (%d/%d)\n",
           DynetEntities::em.areasCount(), (int)arCap);
      return;
    }

    uint8_t a  = areasSweepArea;
    uint8_t ch = areasSweepChannel;
    if (a < 2) { a = 2; areasSweepArea = 2; }

    // End of this pass?
    if (a > arCap) {
      if (areasSweepPass > 0) {
        areasSweepPass--;
        areasSweepArea    = 2;
        areasSweepChannel = 0;
        areasSweepNextAt  = millis() + 500;   // brief pause between passes
        LOGF("[DyNet] sweep pass done, %u pass(es) remaining\n", (unsigned)areasSweepPass);
      } else {
        areasSweepActive  = false;
        areasSweepArea    = 2;
        areasSweepChannel = 0;
        LOGF("[DyNet] sweep complete\n");
      }
      return;
    }

    // First channel of this area: also send a preset request
    if (ch == 0) {
      requestPreset(a);
      LOGF("[DyNet] sweep pass=%u A%u ch 0..%u\n",
           (unsigned)areasSweepPass, (unsigned)a, (unsigned)(maxCh - 1));
    }

    sendRequestChannelLevel(a, ch);

    // Advance to next channel / area
    ch++;
    if (ch >= maxCh) {
      areasSweepArea    = a + 1;
      areasSweepChannel = 0;
    } else {
      areasSweepChannel = ch;
    }

    // 80 ms between requests:  ~8.3 ms TX  +  ~30 ms device processing  +  ~8.3 ms response TX
    // = ~47 ms minimum; 80 ms gives comfortable margin at 9600 baud.
    areasSweepNextAt = millis() + 80;
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
