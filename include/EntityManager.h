#pragma once
#include <Arduino.h>
#include "DynetBus.h"

// Compact manager for discovered Areas/Channels and their HA entity types.
namespace DynetEntities {

enum EntityType : uint8_t {
  LIGHT_DIMMABLE = 0,   // default HA Light (brightness)
  LIGHT_ONOFF    = 1,   // HA Light on/off only
  SWITCH_ONOFF   = 2,   // HA Switch on/off
  CURTAIN        = 3    // HA Cover — paired: ch0=UP relay, ch0+1=DOWN relay
};

enum CurtainMove : uint8_t {
  CURTAIN_IDLE        = 0,
  CURTAIN_DELAY_OPEN,   // 500 ms interlocking pause before engaging UP relay
  CURTAIN_OPENING,      // UP relay ON, travel timer running
  CURTAIN_DELAY_CLOSE,  // 500 ms interlocking pause before engaging DOWN relay
  CURTAIN_CLOSING,      // DOWN relay ON, travel timer running
};

// Area-level type
enum AreaType : uint8_t {
  AREA_LIGHTS  = 0,   // normal lighting — channels, preset select in HA
  AREA_CURTAIN = 1,   // virtual curtain — sends presets for OPEN/CLOSE/STOP
  AREA_HVAC    = 2,   // climate/thermostat — modes+fan mapped to presets, temp/setpoint via opcodes
};

// One HVAC operating mode or fan speed mapped to a Dynalite preset
struct HvacModeEntry {
  bool    used    = false;
  char    name[24] = {};  // HA mode name e.g. "off", "cool", "heat", "fan_only"
  uint8_t preset1 = 1;   // 1-based Dynalite preset
};
static constexpr uint8_t MAX_HVAC_MODES    = 8;
static constexpr uint8_t MAX_HVAC_FANMODES = 6;
static constexpr uint8_t MAX_LIGHT_PRESETS = 16;  // max presets for AREA_LIGHTS

// Preset name storage for AREA_LIGHTS (heap-allocated on demand)
struct AreaPresetNames {
  char n[MAX_LIGHT_PRESETS][16] = {};  // 16 × 16 = 256 bytes, allocated only when a name is set
};

struct HvacConfig {
  HvacModeEntry modes[MAX_HVAC_MODES];
  HvacModeEntry fanModes[MAX_HVAC_FANMODES];
  uint8_t       modeCount = 0;
  uint8_t       fanCount  = 0;
  char          currentMode[24]    = {};  // last known mode name; "" = unknown
  char          currentFanMode[24] = {};  // last known fan mode;  "" = unknown
  float         setptStep = 0.5f;         // temperature setpoint step: 0.5 or 1.0 °C
};

struct ChannelState {
  uint8_t    area            = 0;    // 1..255
  uint8_t    channel0        = 0;    // 0-origin (as per DyNet)
  EntityType type            = LIGHT_DIMMABLE;
  bool       present         = false;
  bool       isOn            = false;
  uint8_t    levelPct        = 0;    // 0..100 derived from reports
  char       name[24]        = {};   // user label; empty = default "Area N Ch M"
  // Curtain support
  bool       isCurtainSlave  = false;  // true = DOWN relay; hidden from UI & HA
  uint8_t    curtainTimeSec  = 30;     // travel time in seconds (master only, persisted)
  CurtainMove curtainMove    = CURTAIN_IDLE;   // state-machine state (runtime, not persisted)
  uint32_t   curtainActionAt = 0;              // millis() of next transition  (runtime)
};

// One named curtain within an AREA_CURTAIN-type area
struct AreaCurtainEntry {
  bool    used        = false;
  char    name[24]    = {};    // user label
  uint8_t openPreset  = 1;    // 1-origin preset sent for OPEN
  uint8_t closePreset = 2;
  uint8_t stopPreset  = 3;
};
static constexpr uint8_t MAX_CURTAINS_PER_AREA = 8;

struct AreaState {
  uint8_t  area     = 0;   // 1..255
  bool     present  = false;
  uint8_t  preset0  = 0xFF; // unknown
  bool     hasTemp  = false;
  float    tempC    = NAN;
  bool     hasSetpt = false;
  float    setptC   = NAN;
  uint32_t lastLevelReqMs = 0;   // debounce refresh requests
  char     name[24] = {};   // user label; empty = default "Area N"
  uint8_t  presetCount = 4; // 1..128, number of presets exposed to HA for this area
  // Area-type / virtual curtains
  AreaType         areaType = AREA_LIGHTS;
  // Allocated on demand when areaType is set to AREA_CURTAIN (nullptr for light areas).
  // Saves ~1440 bytes per area slot vs. embedding the array statically.
  AreaCurtainEntry* curtains  = nullptr; // heap-allocated [MAX_CURTAINS_PER_AREA] or nullptr
  HvacConfig*       hvac      = nullptr; // heap-allocated for HVAC areas, nullptr otherwise
  AreaPresetNames*  presets   = nullptr; // heap-allocated preset names for AREA_LIGHTS, nullptr otherwise
};

// Hard limits — config values are clamped to these at runtime.
#ifndef DYNET_MAX_CHANNELS
#  if defined(ESP8266)
#    define DYNET_MAX_CHANNELS 16   // ESP8266: up to 16 channels per area
#  else
#    define DYNET_MAX_CHANNELS 64   // ESP32: generous cap, no heap concern
#  endif
#endif
#ifndef DYNET_MAX_AREAS
#  if defined(ESP8266)
#    define DYNET_MAX_AREAS 32    // hard cap for ESP8266 (limited RAM)
#  else
#    define DYNET_MAX_AREAS 255   // ESP32 — no practical limit
#  endif
#endif

class EntityManager {
public:
  // arCapOverride / chCapOverride: if > 0, use these instead of cfg values.
  // Pass DYNET_MAX_AREAS / DYNET_MAX_CHANNELS when loading from storage so
  // entities are never dropped just because the user lowered the config cap.
  void begin(int arCapOverride = 0, int chCapOverride = 0);

  // Ensure entries exist; returns index or -1 if full
  int  touchChannel(uint8_t area, uint8_t channel0);
  int  touchArea(uint8_t area);
  int requestLevelsForArea(uint8_t area, uint8_t fallbackProbe = 0);

  // Lookups (return index or -1)
  int  findChannel(uint8_t area, uint8_t channel0) const;
  int  findArea(uint8_t area) const;

  // Mutations
  void setChannelType(uint8_t area, uint8_t channel0, EntityType t);
  void setChannelLevel(uint8_t area, uint8_t channel0, uint8_t pct);
  void setChannelOnOff(uint8_t area, uint8_t channel0, bool on);
  void noteReportPreset(uint8_t area, uint8_t preset0);
  void noteActualTemp_q025(uint8_t area, int16_t steps);
  void noteSetpoint_q025   (uint8_t area, int16_t steps);
  void noteActualTemp_fp   (uint8_t area, uint8_t hi, uint8_t lo);
  void noteSetpoint_fp     (uint8_t area, uint8_t hi, uint8_t lo);

  // Manual add / delete
  bool deleteChannel(uint8_t area, uint8_t channel0);  // returns false if not found
  bool deleteArea(uint8_t area);                        // deletes area + all its channels

  // Name setters (persists via saveEntities + republishes HA discovery)
  void setChannelName(uint8_t area, uint8_t channel0, const char* name);
  void setAreaName(uint8_t area, const char* name);
  void setPresetName(uint8_t area, uint8_t preset1, const char* name); // 1-based preset, AREA_LIGHTS only

  // Returns the stored custom name for a 1-based preset, or nullptr if not set
  const char* getPresetName(uint8_t area, uint8_t preset1) const {
    int ai = findArea(area);
    if (ai < 0) return nullptr;
    const auto& as = _areas[ai];
    if (!as.presets || preset1 < 1 || preset1 > MAX_LIGHT_PRESETS) return nullptr;
    return as.presets->n[preset1 - 1][0] ? as.presets->n[preset1 - 1] : nullptr;
  }

  // Always returns a valid display name — custom if set, else Dynalite defaults or "Preset N"
  String getPresetDisplayName(uint8_t area, uint8_t preset1) const {
    const char* custom = getPresetName(area, preset1);
    if (custom && custom[0]) return String(custom);
    switch (preset1) {
      case 1: return F("High");
      case 2: return F("Medium");
      case 3: return F("Low");
      case 4: return F("Off");
      default: return String(F("Preset ")) + String(preset1);
    }
  }

  // Channel curtain control (relay-based, paired channels)
  void commandCurtain(uint8_t area, uint8_t ch0, const char* cmd); // "OPEN"/"CLOSE"/"STOP"
  void setCurtainTime(uint8_t area, uint8_t ch0, uint8_t seconds); // set travel time
  void pollCurtains();   // process state-machine; call every loop()

  // Area curtain control (virtual — sends preset opcodes, no physical channels)
  void setAreaType(uint8_t area, AreaType t);
  int  addAreaCurtain(uint8_t area);                           // returns index 0..7 or -1 if full
  void deleteAreaCurtain(uint8_t area, uint8_t idx);
  void setAreaCurtainEntry(uint8_t area, uint8_t idx,
                           const char* name,
                           uint8_t openP, uint8_t closeP, uint8_t stopP);
  void commandAreaCurtain(uint8_t area, uint8_t curtainIdx, const char* cmd); // "OPEN"/"CLOSE"/"STOP"

  // HVAC area management
  int  addHvacMode   (uint8_t area, const char* name, uint8_t preset1); // returns idx or -1
  int  addHvacFanMode(uint8_t area, const char* name, uint8_t preset1);
  void deleteHvacMode   (uint8_t area, uint8_t idx);
  void deleteHvacFanMode(uint8_t area, uint8_t idx);
  void commandHvacMode   (uint8_t area, const char* modeName); // from HA: send preset + publish state
  void commandHvacFanMode(uint8_t area, const char* fanName);

  // Access
  inline int channelsCount() const { return _chCount; }
  inline int areasCount() const    { return _arCount; }
  const ChannelState& channelAt(int idx) const { return _channels[idx]; }
  const AreaState&    areaAt(int idx)    const { return _areas[idx]; }
  // Mutable access (only for loading from persistent storage — no publish/save side effects)
  ChannelState& channelAtMut(int idx) { return _channels[idx]; }
  AreaState&    areaAtMut(int idx)    { return _areas[idx]; }

  // Parse a DyNet logical frame (b[0]=0x1C). Returns true if consumed.
  bool handleLogicalFrame(const uint8_t b[8]);

  // Suppress publish/save side effects during bulk load (set before loadEntities, clear after)
  void setLoading(bool v) { _loading = v; }

private:
  bool          _loading   = false;
  ChannelState* _channels  = nullptr;
  AreaState*    _areas     = nullptr;
  int           _chCount   = 0, _arCount   = 0;  // used slots
  int           _chCap     = 0, _arCap     = 0;  // user limit (from config)
  int           _chAlloced = 0, _arAlloced = 0;  // actually allocated (lazy, grows on demand)

  bool growAreaArray();     // allocate more AreaState slots (up to _arCap)
  bool growChannelArray();  // allocate more ChannelState slots (up to _chCap)

// (public:)
inline int maxChannels() const { return _chCap; }
inline int maxAreas()    const { return _arCap; }
};

extern EntityManager em; // global instance (defined in .cpp)

} // namespace DynetEntities
