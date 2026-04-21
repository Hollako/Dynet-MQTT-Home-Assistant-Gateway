#pragma once
#include <Arduino.h>
#include "DynetBus.h"

// Compact manager for discovered Areas/Channels and their HA entity types.
namespace DynetEntities {

enum EntityType : uint8_t {
  LIGHT_DIMMABLE = 0,   // default HA Light (brightness)
  LIGHT_ONOFF    = 1,   // HA Light on/off only
  SWITCH_ONOFF   = 2    // HA Switch on/off
};

struct ChannelState {
  uint8_t  area      = 0;    // 1..255
  uint8_t  channel0  = 0;    // 0-origin (as per DyNet)
  EntityType type    = LIGHT_DIMMABLE;
  bool     present   = false;
  bool     isOn      = false;
  uint8_t  levelPct  = 0;     // 0..100 derived from reports
  char     name[25]  = {};    // user label; empty = default "Area N Ch M"
};

struct AreaState {
  uint8_t  area     = 0;   // 1..255
  bool     present  = false;
  uint8_t  preset0  = 0xFF; // unknown
  bool     hasTemp  = false;
  float    tempC    = NAN;
  bool     hasSetpt = false;
  float    setptC   = NAN;
  uint32_t lastLevelReqMs = 0;   // debounce refresh requests
  char     name[25] = {};   // user label; empty = default "Area N"
};

#ifndef DYNET_MAX_CHANNELS
#define DYNET_MAX_CHANNELS 32  // was 128
#endif
#ifndef DYNET_MAX_AREAS
#define DYNET_MAX_AREAS 32
#endif

class EntityManager {
public:
  void begin();

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
  bool          _loading  = false;
  //ChannelState _channels[DYNET_MAX_CHANNELS];
  //AreaState    _areas[DYNET_MAX_AREAS];
  ChannelState* _channels = nullptr;
  AreaState*    _areas    = nullptr;
  int           _chCount = 0, _arCount = 0;
  int           _chCap   = 0, _arCap   = 0;    // NEW

// (public:)
inline int maxChannels() const { return _chCap; }
inline int maxAreas()    const { return _arCap; }
};

extern EntityManager em; // global instance (defined in .cpp)

} // namespace DynetEntities
