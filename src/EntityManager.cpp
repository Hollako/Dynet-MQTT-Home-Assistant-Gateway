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
extern void removeHADiscoveryForChannel(uint8_t area, uint8_t ch0, DynetEntities::EntityType type);

static float q025_toC(int16_t steps) { return (float)steps * 0.25f; }
static float fp_toC(uint8_t hi, uint8_t lo) {
  int sign = (hi & 0x80) ? -1 : 1;
  float integ = (float)(hi & 0x7F);
  float frac  = (float)lo / 100.0f;
  return sign * (integ + frac);
}

void EntityManager::begin() {
  if (_channels) { delete[] _channels; _channels = nullptr; }
  if (_areas) {
    // Free per-area heap pointers before releasing the area slab
    for (int i = 0; i < _arCap; i++) {
      if (_areas[i].curtains) { delete[] _areas[i].curtains; _areas[i].curtains = nullptr; }
      if (_areas[i].hvac)     { delete   _areas[i].hvac;     _areas[i].hvac     = nullptr; }
    }
    delete[] _areas; _areas = nullptr;
  }

  int chCap = cfg.dynet_max_channels ? constrain((int)cfg.dynet_max_channels, 1, (int)DYNET_MAX_CHANNELS) : DYNET_MAX_CHANNELS;
  int arCap = cfg.dynet_max_areas    ? constrain((int)cfg.dynet_max_areas,    1, (int)DYNET_MAX_AREAS)    : DYNET_MAX_AREAS;

  _channels = new (std::nothrow) ChannelState[chCap];
  if (_channels) { for (int i = 0; i < chCap; i++) _channels[i] = ChannelState{}; }
  _areas    = new (std::nothrow) AreaState   [arCap];
  if (_areas)    { for (int i = 0; i < arCap; i++) _areas[i]    = AreaState{};    }
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
  // Suppress publish/save during bulk load
  if (!_loading) {
    publishHADiscoveryForArea(area);
    saveEntities();
  }
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
  // Suppress publish/save during bulk load
  if (!_loading) {
    publishHADiscoveryForChannel(created);
    publishStateForChannel(created);   // publish initial OFF state so HA shows entity immediately
    saveEntities();
  }
  return created;
}

void EntityManager::setChannelType(uint8_t area, uint8_t channel0, EntityType t) {
  int i = touchChannel(area, channel0);
  if (i < 0) return;

  EntityType oldType = _channels[i].type;

  // ── Leaving CURTAIN → free the slave channel ────────────────────────────
  if (oldType == CURTAIN && t != CURTAIN) {
    _channels[i].curtainMove    = CURTAIN_IDLE;
    _channels[i].curtainActionAt = 0;
    int si = findChannel(area, channel0 + 1);
    if (si >= 0 && _channels[si].isCurtainSlave) {
      removeHADiscoveryForChannel(area, channel0 + 1, CURTAIN);
      if (si != _chCount - 1) _channels[si] = _channels[_chCount - 1];
      _chCount--;
    }
  }

  // Remove the *old* HA entity for this channel before switching type.
  // removeHADiscoveryForChannel wipes light + switch + cover so it covers
  // every direction (light→cover, cover→light, etc.).
  if (oldType != t) {
    removeHADiscoveryForChannel(area, channel0, oldType);
  }

  _channels[i].type = t;

  // ── Entering CURTAIN → auto-create/claim slave channel ──────────────────
  if (t == CURTAIN && !_channels[i].isCurtainSlave) {
    uint8_t slaveCh = channel0 + 1;
    int si = findChannel(area, slaveCh);
    if (si >= 0) {
      // Channel already exists — retire its current HA entity then mark as slave
      removeHADiscoveryForChannel(area, slaveCh, _channels[si].type);
      _channels[si].type           = CURTAIN;
      _channels[si].isCurtainSlave = true;
    } else {
      // Create slave silently (no HA publish, no save during creation)
      bool prev = _loading; _loading = true;
      si = touchChannel(area, slaveCh);
      _loading = prev;
      if (si >= 0) {
        _channels[si].type           = CURTAIN;
        _channels[si].isCurtainSlave = true;
      }
    }
  }

  publishHADiscoveryForChannel(i);
  saveEntities();
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

extern void publishPresetForArea(uint8_t area);
extern void removeHADiscoveryForArea(uint8_t area);
extern void publishHADiscoveryForAreaCurtainEntry(uint8_t area, uint8_t idx);
extern void removeHADiscoveryForAreaCurtainEntry(uint8_t area, uint8_t idx);

void EntityManager::noteReportPreset(uint8_t area, uint8_t preset0) {
  int a = touchArea(area);
  uint8_t prev = 0xFF;

  if (a >= 0) {
    prev = _areas[a].preset0;
    _areas[a].preset0 = preset0;
  }

  // HVAC areas: map preset to mode/fan state, skip preset entity + level requests
  if (a >= 0 && _areas[a].areaType == AREA_HVAC && _areas[a].hvac) {
    auto& h = *_areas[a].hvac;
    for (uint8_t i = 0; i < h.modeCount; i++) {
      if (h.modes[i].used && h.modes[i].preset1 == preset0 + 1) {
        strncpy(h.currentMode, h.modes[i].name, sizeof(h.currentMode) - 1);
        h.currentMode[sizeof(h.currentMode) - 1] = '\0';
      }
    }
    for (uint8_t i = 0; i < h.fanCount; i++) {
      if (h.fanModes[i].used && h.fanModes[i].preset1 == preset0 + 1) {
        strncpy(h.currentFanMode, h.fanModes[i].name, sizeof(h.currentFanMode) - 1);
        h.currentFanMode[sizeof(h.currentFanMode) - 1] = '\0';
      }
    }
    publishSensorsForArea(area);  // publishes temp + setpoint + mode + fan state
    saveEntities();
    return;
  }

  publishPresetForArea(area);

  // Only act when preset actually changes (or was previously unknown)
  if (prev == preset0 && prev != 0xFF) return;

  // Debounce so we don't request twice (e.g. when both 0x64 and 0x62 arrive)
  uint32_t now = millis();
  if (a >= 0 && (now - _areas[a].lastLevelReqMs) < 300) return;
  if (a >= 0) _areas[a].lastLevelReqMs = now;

  // Schedule non-blocking level refresh after preset change (400ms settling time)
  dynet.scheduleAreaLevelReqs(area, 400);
  LOGF("[DyNet] A%u preset->P%u (was P%u) => scheduled level refresh\n",
       area, preset0 + 1, (prev == 0xFF ? 0 : prev + 1));

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

    dynet.scheduleAreaLevelReqs(area, 400);
    LOGF("[DyNet] A%u preset(0x64)->P%u; scheduled level refresh\n", area, preset0 + 1);
    return true;
  }

  // Preset select linear: [1C][Area][65][Preset0][FadeLo][FadeHi][Join][Chk]
  if (b[2] == 0x65) {
    uint8_t preset0 = b[3];
    noteReportPreset(area, preset0);

    dynet.scheduleAreaLevelReqs(area, 400);
    LOGF("[DyNet] A%u preset(0x65)->P%u; scheduled level refresh\n", area, preset0 + 1);
    return true;
  }

  // Fade-to-preset: [1C][Area][6B][FF][Preset0][Fade][Join][Chk]
  if (b[2] == 0x6B) {
    uint8_t preset0 = b[4];
    noteReportPreset(area, preset0);

    dynet.scheduleAreaLevelReqs(area, 400);
    LOGF("[DyNet] A%u preset(0x6B)->P%u; scheduled level refresh\n", area, preset0 + 1);
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
      dynet.scheduleLevelReq(area, ch0, 300);  // non-blocking: request level after 300ms
      LOGF("[DyNet] A%u ch%u cmd(0x%02X) -> scheduled level req\n", area, ch0, b[3]);
      return true;
    }
    default: break;
  }

  return false;
}

// ---- Manual delete ----------------------------------------------

bool EntityManager::deleteChannel(uint8_t area, uint8_t channel0) {
  int idx = -1;
  for (int i = 0; i < _chCount; i++) {
    if (_channels[i].present && _channels[i].area == area && _channels[i].channel0 == channel0) {
      idx = i; break;
    }
  }
  if (idx < 0) return false;

  EntityType type = _channels[idx].type;

  // Remove from HA
  removeHADiscoveryForChannel(area, channel0, type);

  // Compact: swap with last element then shrink
  if (idx != _chCount - 1) _channels[idx] = _channels[_chCount - 1];
  _chCount--;

  // If area now has no channels, remove it too
  bool hasMore = false;
  for (int i = 0; i < _chCount; i++) {
    if (_channels[i].present && _channels[i].area == area) { hasMore = true; break; }
  }
  if (!hasMore) deleteArea(area);

  saveEntities();
  LOGF("[EM] deleted A%u C%u\n", area, channel0);
  return true;
}

void EntityManager::setChannelName(uint8_t area, uint8_t channel0, const char* name) {
  int idx = findChannel(area, channel0);
  if (idx < 0) return;
  strncpy(_channels[idx].name, name ? name : "", sizeof(_channels[idx].name) - 1);
  _channels[idx].name[sizeof(_channels[idx].name) - 1] = '\0';
  publishHADiscoveryForChannel(idx);  // update HA entity name
  saveEntities();
}

void EntityManager::setAreaName(uint8_t area, const char* name) {
  int idx = findArea(area);
  if (idx < 0) return;
  strncpy(_areas[idx].name, name ? name : "", sizeof(_areas[idx].name) - 1);
  _areas[idx].name[sizeof(_areas[idx].name) - 1] = '\0';
  publishHADiscoveryForArea(area);    // update HA entity names
  saveEntities();
}

bool EntityManager::deleteArea(uint8_t area) {
  // Delete all channels belonging to this area
  for (int i = _chCount - 1; i >= 0; i--) {
    if (_channels[i].present && _channels[i].area == area) {
      removeHADiscoveryForChannel(area, _channels[i].channel0, _channels[i].type);
      if (i != _chCount - 1) _channels[i] = _channels[_chCount - 1];
      _chCount--;
    }
  }

  // Remove the area entry
  int ai = findArea(area);
  if (ai >= 0) {
    removeHADiscoveryForArea(area);
    // Free this area's heap pointers before overwriting the slot
    if (_areas[ai].curtains) { delete[] _areas[ai].curtains; _areas[ai].curtains = nullptr; }
    if (_areas[ai].hvac)     { delete   _areas[ai].hvac;     _areas[ai].hvac     = nullptr; }
    if (ai != _arCount - 1) {
      _areas[ai] = _areas[_arCount - 1];
      _areas[_arCount - 1].curtains = nullptr; // prevent double-free of moved pointers
      _areas[_arCount - 1].hvac     = nullptr;
    }
    _arCount--;
  }

  saveEntities();
  LOGF("[EM] deleted Area %u\n", area);
  return (ai >= 0);
}

// ---- Curtain control -----------------------------------------------

void EntityManager::commandCurtain(uint8_t area, uint8_t ch0, const char* cmd) {
  int mi = findChannel(area, ch0);
  if (mi < 0 || _channels[mi].type != CURTAIN || _channels[mi].isCurtainSlave) return;

  ChannelState& m = _channels[mi];
  uint8_t upCh   = ch0;
  uint8_t downCh = ch0 + 1;

  if (strcmp(cmd, "OPEN") == 0) {
    // Interlock: stop DOWN relay immediately, then after 500ms start UP relay
    dynet.sendFadeToLevel_1s(area, downCh, 0, 0x02);
    m.curtainMove     = CURTAIN_DELAY_OPEN;
    m.curtainActionAt = millis() + 500;
    LOGF("[Curtain] A%u Ch%u OPEN: DOWN off, delay 500ms\n", area, upCh + 1);

  } else if (strcmp(cmd, "CLOSE") == 0) {
    // Interlock: stop UP relay immediately, then after 500ms start DOWN relay
    dynet.sendFadeToLevel_1s(area, upCh, 0, 0x02);
    m.curtainMove     = CURTAIN_DELAY_CLOSE;
    m.curtainActionAt = millis() + 500;
    LOGF("[Curtain] A%u Ch%u CLOSE: UP off, delay 500ms\n", area, upCh + 1);

  } else if (strcmp(cmd, "STOP") == 0) {
    dynet.sendFadeToLevel_1s(area, upCh,   0, 0x02);
    dynet.sendFadeToLevel_1s(area, downCh, 0, 0x02);
    m.curtainMove     = CURTAIN_IDLE;
    m.curtainActionAt = 0;
    LOGF("[Curtain] A%u Ch%u STOP: both off\n", area, upCh + 1);
  }
}

void EntityManager::setCurtainTime(uint8_t area, uint8_t ch0, uint8_t seconds) {
  int mi = findChannel(area, ch0);
  if (mi < 0 || _channels[mi].type != CURTAIN || _channels[mi].isCurtainSlave) return;
  _channels[mi].curtainTimeSec = seconds;
  saveEntities();
}

void EntityManager::pollCurtains() {
  uint32_t now = millis();
  for (int i = 0; i < _chCount; i++) {
    ChannelState& c = _channels[i];
    if (c.type != CURTAIN || c.isCurtainSlave) continue;
    if (c.curtainMove == CURTAIN_IDLE) continue;
    if ((int32_t)(now - c.curtainActionAt) < 0) continue; // not yet

    uint8_t upCh   = c.channel0;
    uint8_t downCh = c.channel0 + 1;

    switch (c.curtainMove) {
      case CURTAIN_DELAY_OPEN:
        dynet.sendFadeToLevel_1s(c.area, upCh, 100, 0x02);
        if (c.curtainTimeSec > 0) {
          c.curtainMove     = CURTAIN_OPENING;
          c.curtainActionAt = now + (uint32_t)c.curtainTimeSec * 1000UL;
        } else {
          c.curtainMove     = CURTAIN_IDLE;
          c.curtainActionAt = 0;
        }
        LOGF("[Curtain] A%u Ch%u: UP relay ON (travel %us)\n", c.area, upCh+1, c.curtainTimeSec);
        break;

      case CURTAIN_DELAY_CLOSE:
        dynet.sendFadeToLevel_1s(c.area, downCh, 100, 0x02);
        if (c.curtainTimeSec > 0) {
          c.curtainMove     = CURTAIN_CLOSING;
          c.curtainActionAt = now + (uint32_t)c.curtainTimeSec * 1000UL;
        } else {
          c.curtainMove     = CURTAIN_IDLE;
          c.curtainActionAt = 0;
        }
        LOGF("[Curtain] A%u Ch%u: DOWN relay ON (travel %us)\n", c.area, downCh+1, c.curtainTimeSec);
        break;

      case CURTAIN_OPENING:
      case CURTAIN_CLOSING:
        // Travel time expired — stop both relays
        dynet.sendFadeToLevel_1s(c.area, upCh,   0, 0x02);
        dynet.sendFadeToLevel_1s(c.area, downCh, 0, 0x02);
        c.curtainMove     = CURTAIN_IDLE;
        c.curtainActionAt = 0;
        LOGF("[Curtain] A%u Ch%u: travel time expired, both off\n", c.area, upCh+1);
        break;

      default: break;
    }
  }
}

// ---- Area Curtain (virtual — preset-based) --------------------------

void EntityManager::setAreaType(uint8_t area, AreaType t) {
  int ai = touchArea(area);
  if (ai < 0) return;
  if (_areas[ai].areaType == t) return;

  // Leaving CURTAIN: free curtain array
  if (_areas[ai].areaType == AREA_CURTAIN && t != AREA_CURTAIN) {
    if (_areas[ai].curtains) { delete[] _areas[ai].curtains; _areas[ai].curtains = nullptr; }
  }
  // Leaving HVAC: free hvac config
  if (_areas[ai].areaType == AREA_HVAC && t != AREA_HVAC) {
    if (_areas[ai].hvac) { delete _areas[ai].hvac; _areas[ai].hvac = nullptr; }
  }

  _areas[ai].areaType = t;

  // Entering CURTAIN: allocate curtain array + remove channel HA entities
  if (t == AREA_CURTAIN) {
    if (!_areas[ai].curtains) {
      _areas[ai].curtains = new (std::nothrow) AreaCurtainEntry[MAX_CURTAINS_PER_AREA];
      if (_areas[ai].curtains) {
        for (int i = 0; i < MAX_CURTAINS_PER_AREA; i++) _areas[ai].curtains[i] = AreaCurtainEntry{};
      }
    }
    for (int i = 0; i < _chCount; i++) {
      if (_channels[i].area == area && _channels[i].present && !_channels[i].isCurtainSlave) {
        removeHADiscoveryForChannel(area, _channels[i].channel0, _channels[i].type);
      }
    }
  }
  // Entering HVAC: allocate hvac config
  if (t == AREA_HVAC) {
    if (!_areas[ai].hvac) {
      _areas[ai].hvac = new (std::nothrow) HvacConfig{};
    }
    // Hide all channel entities — HVAC areas are controlled via climate entity
    for (int i = 0; i < _chCount; i++) {
      if (_channels[i].area == area && _channels[i].present && !_channels[i].isCurtainSlave) {
        removeHADiscoveryForChannel(area, _channels[i].channel0, _channels[i].type);
      }
    }
  }
  // Wipe all stale HA entities for this area then publish the correct new ones
  removeHADiscoveryForArea(area);
  publishHADiscoveryForArea(area);
  saveEntities();
}

int EntityManager::addAreaCurtain(uint8_t area) {
  int ai = findArea(area);
  if (ai < 0) return -1;
  // Allocate on demand (in case setAreaType was bypassed, e.g. during load)
  if (!_areas[ai].curtains) {
    _areas[ai].curtains = new (std::nothrow) AreaCurtainEntry[MAX_CURTAINS_PER_AREA];
    if (!_areas[ai].curtains) return -1;
    for (int i = 0; i < MAX_CURTAINS_PER_AREA; i++) _areas[ai].curtains[i] = AreaCurtainEntry{};
  }
  for (int i = 0; i < MAX_CURTAINS_PER_AREA; i++) {
    if (!_areas[ai].curtains[i].used) {
      _areas[ai].curtains[i] = AreaCurtainEntry{};
      _areas[ai].curtains[i].used = true;
      snprintf(_areas[ai].curtains[i].name, sizeof(_areas[ai].curtains[i].name),
               "Curtain %d", i + 1);
      publishHADiscoveryForAreaCurtainEntry(area, i);
      saveEntities();
      return i;
    }
  }
  return -1; // all slots full
}

void EntityManager::deleteAreaCurtain(uint8_t area, uint8_t idx) {
  int ai = findArea(area);
  if (ai < 0 || idx >= MAX_CURTAINS_PER_AREA || !_areas[ai].curtains) return;
  if (!_areas[ai].curtains[idx].used) return;
  removeHADiscoveryForAreaCurtainEntry(area, idx);
  _areas[ai].curtains[idx] = AreaCurtainEntry{};  // clear slot
  saveEntities();
}

void EntityManager::setAreaCurtainEntry(uint8_t area, uint8_t idx,
                                        const char* name,
                                        uint8_t openP, uint8_t closeP, uint8_t stopP) {
  int ai = findArea(area);
  if (ai < 0 || idx >= MAX_CURTAINS_PER_AREA || !_areas[ai].curtains || !_areas[ai].curtains[idx].used) return;
  auto& e = _areas[ai].curtains[idx];
  if (name && name[0]) {
    strncpy(e.name, name, sizeof(e.name) - 1);
    e.name[sizeof(e.name) - 1] = '\0';
  }
  e.openPreset  = openP;
  e.closePreset = closeP;
  e.stopPreset  = stopP;
  publishHADiscoveryForAreaCurtainEntry(area, idx);
  saveEntities();
}

void EntityManager::commandAreaCurtain(uint8_t area, uint8_t curtainIdx, const char* cmd) {
  int ai = findArea(area);
  if (ai < 0) return;
  const AreaState& a = _areas[ai];
  if (a.areaType != AREA_CURTAIN) return;
  if (!a.curtains || curtainIdx >= MAX_CURTAINS_PER_AREA || !a.curtains[curtainIdx].used) return;
  const AreaCurtainEntry& e = a.curtains[curtainIdx];
  uint8_t preset = 0;
  if      (strcmp(cmd, "OPEN")  == 0) preset = e.openPreset;
  else if (strcmp(cmd, "CLOSE") == 0) preset = e.closePreset;
  else if (strcmp(cmd, "STOP")  == 0) preset = e.stopPreset;
  if (preset == 0) return;
  dynet.sendAreaPreset(area, preset, 0);
  LOGF("[AreaCurtain] A%u ct%u %s -> Preset %u\n", area, curtainIdx, cmd, preset);
}

// ---- HVAC area management ------------------------------------------

int EntityManager::addHvacMode(uint8_t area, const char* name, uint8_t preset1) {
  int ai = findArea(area);
  if (ai < 0 || _areas[ai].areaType != AREA_HVAC) return -1;
  if (!_areas[ai].hvac) return -1;
  auto& h = *_areas[ai].hvac;
  if (h.modeCount >= MAX_HVAC_MODES) return -1;
  for (uint8_t i = 0; i < MAX_HVAC_MODES; i++) {
    if (!h.modes[i].used) {
      h.modes[i].used = true;
      h.modes[i].preset1 = preset1;
      strncpy(h.modes[i].name, name ? name : "", sizeof(h.modes[i].name) - 1);
      h.modes[i].name[sizeof(h.modes[i].name) - 1] = '\0';
      h.modeCount++;
      publishHADiscoveryForArea(area);
      saveEntities();
      return i;
    }
  }
  return -1;
}

int EntityManager::addHvacFanMode(uint8_t area, const char* name, uint8_t preset1) {
  int ai = findArea(area);
  if (ai < 0 || _areas[ai].areaType != AREA_HVAC) return -1;
  if (!_areas[ai].hvac) return -1;
  auto& h = *_areas[ai].hvac;
  if (h.fanCount >= MAX_HVAC_FANMODES) return -1;
  for (uint8_t i = 0; i < MAX_HVAC_FANMODES; i++) {
    if (!h.fanModes[i].used) {
      h.fanModes[i].used = true;
      h.fanModes[i].preset1 = preset1;
      strncpy(h.fanModes[i].name, name ? name : "", sizeof(h.fanModes[i].name) - 1);
      h.fanModes[i].name[sizeof(h.fanModes[i].name) - 1] = '\0';
      h.fanCount++;
      publishHADiscoveryForArea(area);
      saveEntities();
      return i;
    }
  }
  return -1;
}

void EntityManager::deleteHvacMode(uint8_t area, uint8_t idx) {
  int ai = findArea(area);
  if (ai < 0 || !_areas[ai].hvac || idx >= MAX_HVAC_MODES) return;
  auto& h = *_areas[ai].hvac;
  if (!h.modes[idx].used) return;
  h.modes[idx] = HvacModeEntry{};
  if (h.modeCount > 0) h.modeCount--;
  publishHADiscoveryForArea(area);
  saveEntities();
}

void EntityManager::deleteHvacFanMode(uint8_t area, uint8_t idx) {
  int ai = findArea(area);
  if (ai < 0 || !_areas[ai].hvac || idx >= MAX_HVAC_FANMODES) return;
  auto& h = *_areas[ai].hvac;
  if (!h.fanModes[idx].used) return;
  h.fanModes[idx] = HvacModeEntry{};
  if (h.fanCount > 0) h.fanCount--;
  publishHADiscoveryForArea(area);
  saveEntities();
}

void EntityManager::commandHvacMode(uint8_t area, const char* modeName) {
  int ai = findArea(area);
  if (ai < 0 || !_areas[ai].hvac) return;
  auto& h = *_areas[ai].hvac;
  for (uint8_t i = 0; i < MAX_HVAC_MODES; i++) {
    if (h.modes[i].used && strcmp(h.modes[i].name, modeName) == 0) {
      dynet.sendAreaPreset(area, h.modes[i].preset1, 0);
      strncpy(h.currentMode, modeName, sizeof(h.currentMode) - 1);
      h.currentMode[sizeof(h.currentMode) - 1] = '\0';
      publishSensorsForArea(area);
      LOGF("[HVAC] A%u mode '%s' -> Preset %u\n", area, modeName, h.modes[i].preset1);
      return;
    }
  }
  LOGF("[HVAC] A%u mode '%s' not found\n", area, modeName);
}

void EntityManager::commandHvacFanMode(uint8_t area, const char* fanName) {
  int ai = findArea(area);
  if (ai < 0 || !_areas[ai].hvac) return;
  auto& h = *_areas[ai].hvac;
  for (uint8_t i = 0; i < MAX_HVAC_FANMODES; i++) {
    if (h.fanModes[i].used && strcmp(h.fanModes[i].name, fanName) == 0) {
      dynet.sendAreaPreset(area, h.fanModes[i].preset1, 0);
      strncpy(h.currentFanMode, fanName, sizeof(h.currentFanMode) - 1);
      h.currentFanMode[sizeof(h.currentFanMode) - 1] = '\0';
      publishSensorsForArea(area);
      LOGF("[HVAC] A%u fan '%s' -> Preset %u\n", area, fanName, h.fanModes[i].preset1);
      return;
    }
  }
  LOGF("[HVAC] A%u fan '%s' not found\n", area, fanName);
}

