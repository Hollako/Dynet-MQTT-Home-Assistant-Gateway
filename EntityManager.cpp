#include "EntityManager.h"
#include "Globals.h"
#include "ConfigStore.h"

using namespace DynetEntities;

//EntityManager em;  // global instance
namespace DynetEntities {
  EntityManager em;
}

extern void publishHADiscoveryForChannel(int idx);
extern void publishHADiscoveryForArea(uint8_t area);
extern void publishStateForChannel(int idx);
extern void publishSensorsForArea(uint8_t area);

static float q025_toC(int16_t steps) { return (float)steps * 0.25f; }
static float fp_toC(uint8_t hi, uint8_t lo) {
  int sign = (hi & 0x80) ? -1 : 1;
  float integ = (float)(hi & 0x7F);
  float frac  = (float)lo / 100.0f;
  return sign * (integ + frac);
}

void EntityManager::begin() {
  if (_channels) { delete[] _channels; _channels = nullptr; }
  if (_areas)    { delete[] _areas;    _areas    = nullptr; }

  int chCap = cfg.dynet_max_channels ? constrain((int)cfg.dynet_max_channels, 1, (int)DYNET_MAX_CHANNELS) : DYNET_MAX_CHANNELS;
  int arCap = cfg.dynet_max_areas    ? constrain((int)cfg.dynet_max_areas,    1, (int)DYNET_MAX_AREAS)    : DYNET_MAX_AREAS;

  _channels = new (std::nothrow) ChannelState[chCap];
  _areas    = new (std::nothrow) AreaState   [arCap];
  _chCount = 0; _arCount = 0;
  _chCap   = chCap; _arCap = arCap;       // NEW: remember capacities
}


int EntityManager::findArea(uint8_t area) const {
  for (int i = 0; i < _arCount; i++) if (_areas[i].area == area) return i;
  return -1;
}
int EntityManager::touchArea(uint8_t area) {
  int idx = findArea(area);
  if (idx >= 0) { _areas[idx].present = true; return idx; }
  if (_arCount >= _arCap) return -1;
  _areas[_arCount] = AreaState{};
  _areas[_arCount].area = area;
  _areas[_arCount].present = true;
  int created = _arCount++;
  // NEW: publish area-level entities (temp sensor, setpoint number, save button)
  publishHADiscoveryForArea(area);
  saveEntities();                       
  return created;
}

int EntityManager::findChannel(uint8_t area, uint8_t channel0) const {
  for (int i = 0; i < _chCount; i++)
    if (_channels[i].present && _channels[i].area == area && _channels[i].channel0 == channel0) return i;
  return -1;
}
int EntityManager::touchChannel(uint8_t area, uint8_t channel0) {
  int idx = findChannel(area, channel0);
  if (idx >= 0) { _channels[idx].present = true; return idx; }
  if (_chCount >= _chCap) return -1;
  _channels[_chCount] = ChannelState{};
  _channels[_chCount].present  = true;
  _channels[_chCount].area     = area;
  _channels[_chCount].channel0 = channel0;
  _channels[_chCount].type     = LIGHT_DIMMABLE; // default
  _channels[_chCount].isOn     = false;
  _channels[_chCount].levelPct = 0;
  touchArea(area);
  int created = _chCount++;
  // NEW: publish HA discovery for this channel now
  publishHADiscoveryForChannel(created);
  saveEntities();                       
  return created;
}

void EntityManager::setChannelType(uint8_t area, uint8_t channel0, EntityType t) {
  int i = touchChannel(area, channel0);
  if (i >= 0) {
    _channels[i].type = t;
    publishHADiscoveryForChannel(i);  // <-- must be here
    saveEntities();                   // persist the choice
  }
}

void EntityManager::setChannelLevel(uint8_t area, uint8_t channel0, uint8_t pct) {
  if (pct > 100) pct = 100;
  int i = touchChannel(area, channel0);
  if (i >= 0) {
    _channels[i].levelPct = pct;
    _channels[i].isOn = (pct > 0);
    publishStateForChannel(i);              // NEW
  }
}
void EntityManager::setChannelOnOff(uint8_t area, uint8_t channel0, bool on) {
  int i = touchChannel(area, channel0);
  if (i >= 0) {
    _channels[i].isOn = on;
    if (!on) _channels[i].levelPct = 0;
    else if (_channels[i].levelPct == 0) _channels[i].levelPct = 100;
    publishStateForChannel(i);              // NEW
  }
}

extern void publishPresetForArea(uint8_t area);  // add near other externs

void EntityManager::noteReportPreset(uint8_t area, uint8_t preset0) {
  int a = touchArea(area);
  uint8_t prev = 0xFF;

  if (a >= 0) {
    prev = _areas[a].preset0;
    _areas[a].preset0 = preset0;
  }

  publishPresetForArea(area);

  // Only act when preset actually changes (or was previously unknown)
  if (prev == preset0 && prev != 0xFF) return;

  // Debounce so we don't request twice (e.g. when both 0x64 and 0x62 arrive)
  uint32_t now = millis();
  if (a >= 0 && (now - _areas[a].lastLevelReqMs) < 300) return;
  if (a >= 0) _areas[a].lastLevelReqMs = now;

  // Request a full refresh of levels after preset change
  uint8_t maxCh = cfg.dynet_max_channels ? cfg.dynet_max_channels : (uint8_t)DYNET_MAX_CHANNELS;
  int sent = 0;
  for (uint8_t ch = 0; ch < maxCh; ch++) {
    dynet.sendRequestChannelLevel(area, ch);
    delay(2);
    sent++;
  }
  LOGF("[DyNet] A%u preset->P%u (was %u) => requested %d channel level(s)\n",
       area, preset0 + 1, (prev == 0xFF ? 0 : prev + 1), sent);

  saveEntities();
}

void EntityManager::noteActualTemp_q025(uint8_t area, int16_t steps) {
  int a = touchArea(area);
  if (a >= 0) {
    _areas[a].hasTemp = true; _areas[a].tempC = q025_toC(steps);
    publishSensorsForArea(area);            // NEW
    saveEntities(); 
  }
}
void EntityManager::noteSetpoint_q025(uint8_t area, int16_t steps) {
  int a = touchArea(area);
  if (a >= 0) {
    _areas[a].hasSetpt = true; _areas[a].setptC = q025_toC(steps);
    publishSensorsForArea(area);            // NEW
    saveEntities(); 
  }
}
void EntityManager::noteActualTemp_fp(uint8_t area, uint8_t hi, uint8_t lo) {
  int a = touchArea(area);
  if (a >= 0) {
    _areas[a].hasTemp = true; _areas[a].tempC = fp_toC(hi, lo);
    publishSensorsForArea(area);            // NEW
    saveEntities(); 
  }
}
void EntityManager::noteSetpoint_fp(uint8_t area, uint8_t hi, uint8_t lo) {
  int a = touchArea(area);
  if (a >= 0) {
    _areas[a].hasSetpt = true; _areas[a].setptC = fp_toC(hi, lo);
    publishSensorsForArea(area);            // NEW
    saveEntities(); 
  }
}

// Interpret logical frames (0x1C)
static inline uint8_t pctFromDyn(uint8_t v) {
  // DyNet: 0x01=100%, 0xFF=0%
  if (v == 0xFF) return 0;
  if (v <= 1)    return 100;
  // integer map 0x02..0xFE -> 99..1
  return (uint8_t)(100 - ((v - 1) * 99 / 0xFE));
}

int EntityManager::requestLevelsForArea(uint8_t area, uint8_t fallbackProbe) {
  int sent = 0;
  for (int i = 0; i < _chCount; ++i) {
    const ChannelState& c = _channels[i];
    if (c.present && c.area == area) {
      dynet.sendRequestChannelLevel(area, c.channel0);
      delay(2);
      sent++;
    }
  }
  if (sent == 0 && fallbackProbe > 0) {
    uint8_t lim = fallbackProbe;
    if (lim > DYNET_MAX_CHANNELS) lim = DYNET_MAX_CHANNELS;
    for (uint8_t ch = 0; ch < lim; ++ch) {
      dynet.sendRequestChannelLevel(area, ch);
      delay(2);
      sent++;
    }
  }
  return sent;
}

// Inverse of the send mapping for 0x64: Code->idx (0..7)
static inline int8_t idxFrom64Code(uint8_t code) {
  switch (code) {
    case 0x00: return 0; // P1
    case 0x01: return 1; // P2
    case 0x02: return 2; // P3
    case 0x03: return 3; // P4
    case 0x0A: return 4; // P5
    case 0x0B: return 5; // P6
    case 0x0C: return 6; // P7
    case 0x0D: return 7; // P8
    default:   return -1;
  }
}

bool EntityManager::handleLogicalFrame(const uint8_t b[8]) {
  if (b[0] != 0x1C) return false;
  const uint8_t area = b[1];

  // -------- Variant B: opcode in b[2] --------

  // Recall Preset with bank: [1C][Area][64][Code][Fade/??][Bank][Join][Chk]
  if (b[2] == 0x64) {
    const uint8_t code = b[3];   // 00,01,02,03,0A,0B,0C,0D
    const uint8_t bank = b[5];   // 0->P1..P8, 1->P9..P16, ...
    int8_t idx = idxFrom64Code(code);  // 0..7 or -1
    if (idx < 0) return false;

    uint8_t preset0 = (uint8_t)(bank * 8 + idx);  // 0-origin
    noteReportPreset(area, preset0);

    int sent = requestLevelsForArea(area, DYNET_MAX_CHANNELS);
    LOGF("[DyNet] A%u preset(0x64)->P%u; requested %d level(s)\n", area, preset0 + 1, sent);
    return true;
  }

  // Preset select linear: [1C][Area][65][Preset0][FadeLo][FadeHi][Join][Chk]
  if (b[2] == 0x65) {
    uint8_t preset0 = b[3];
    noteReportPreset(area, preset0);

    int sent = requestLevelsForArea(area, DYNET_MAX_CHANNELS);
    LOGF("[DyNet] A%u preset(0x65)->P%u; requested %d level(s)\n", area, preset0 + 1, sent);
    return true;
  }

  // Fade-to-preset: [1C][Area][6B][FF][Preset0][Fade][Join][Chk]
  if (b[2] == 0x6B) {
    uint8_t preset0 = b[4];
    noteReportPreset(area, preset0);

    int sent = requestLevelsForArea(area, DYNET_MAX_CHANNELS);
    LOGF("[DyNet] A%u preset(0x6B)->P%u; requested %d level(s)\n", area, preset0 + 1, sent);
    return true;
  }

  // -------- Variant A: opcode in b[3] ------------------------------
  switch (b[3]) {
    case 0x60: { // Report Channel Level: [1C][Area][Ch0][60][Target][Current]...
      uint8_t ch0 = b[2];
      uint8_t tgt = b[4];     // we use TARGET level
      setChannelLevel(area, ch0, pctFromDyn(tgt));
      return true;
    }
    case 0x62: { // Report Preset (Area): [1C][Area][Preset0][62]...
      uint8_t preset0 = b[2];
      noteReportPreset(area, preset0);
      return true;
    }
    case 0x4A: { // User Preference: [1C][Area][Sel][4A][D2][D3]...
      uint8_t pref = b[2];
      uint8_t d2   = b[4], d3 = b[5];
      if (pref == 0x06) { noteActualTemp_q025(area, (int16_t)((d2<<8)|d3)); return true; } // actual temp q0.25
      if (pref == 0x07) { noteSetpoint_q025   (area, (int16_t)((d2<<8)|d3)); return true; } // setpoint q0.25
      if (pref == 0x0C) { noteActualTemp_fp   (area, d2, d3); return true; }                // actual temp fp
      if (pref == 0x0D) { noteSetpoint_fp     (area, d2, d3); return true; }                // setpoint fp
      return false;
    }
    case 0x71: // Ramp channel to level
    case 0x72: // Fade channel to level
    case 0x73: // Fade (minutes)
    case 0x74: // Fade to off
    case 0x75: // Fade to on
    case 0x68: // Ramp to off
    case 0x69: // Ramp to on
    case 0x5F: // Ramp lit channels toward level
    {
      uint8_t ch0 = b[2];

      // Ensure discovery happens even if we never saw a 0x60 yet
      touchChannel(area, ch0);                 // ensure discovered
      delay(80);                               // small settle
      dynet.sendRequestChannelLevel(area, ch0); // 0x61 -> expect 0x60
      LOGF("[DyNet] A%u ch%u cmd(0x%02X) -> requested level\n", area, ch0, b[3]);
      return true;
    }
    default: break;
  }

  return false;
}

