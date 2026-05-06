#include "Globals.h"
#include "ConfigStore.h"
#include "DynetBus.h"
#include "EntityManager.h"

#ifndef CONFIG_FILE
#define CONFIG_FILE "/config.json"
#endif
#ifndef ENTITIES_FILE
#define ENTITIES_FILE "/entities.json"
#endif

extern DynetBus dynet;
namespace DynetEntities { extern EntityManager em; }

bool loadConfig() {
  // Safe defaults before parsing — ensures correct behaviour on first boot
  // or when a key is absent from an older config.json.
  cfg.ha_discovery  = true;
  cfg.log_web       = false;
  cfg.net_mode      = NET_WIFI;
  cfg.eth_phy_type  = 0;           // LAN8720
  cfg.eth_phy_addr  = 0;
  cfg.eth_power_pin = -1;          // not used
  cfg.eth_mdc_pin   = 23;          // LAN8720 typical
  cfg.eth_mdio_pin  = 18;          // LAN8720 typical

  if (!LittleFS.exists(CONFIG_FILE)) return false;
  File f = LittleFS.open(CONFIG_FILE, "r"); if (!f) return false;

  DynamicJsonDocument doc(3072);
  DeserializationError err = deserializeJson(doc, f);
  f.close(); if (err) return false;

  if (doc.containsKey("ap_ssid"))      apSsid = String((const char*)doc["ap_ssid"]);
  if (doc.containsKey("ap_pass"))      apPass = String((const char*)doc["ap_pass"]);
  if (doc.containsKey("wifi_ssid"))    setStr(cfg.wifi_ssid,  sizeof(cfg.wifi_ssid),  String((const char*)doc["wifi_ssid"]));
  if (doc.containsKey("wifi_pass"))    setStr(cfg.wifi_pass,  sizeof(cfg.wifi_pass),  String((const char*)doc["wifi_pass"]));
  if (doc.containsKey("wifi_ssid2"))      setStr(cfg.wifi_ssid2,      sizeof(cfg.wifi_ssid2),      String((const char*)doc["wifi_ssid2"]));
  if (doc.containsKey("wifi_pass2"))      setStr(cfg.wifi_pass2,      sizeof(cfg.wifi_pass2),      String((const char*)doc["wifi_pass2"]));
  if (doc.containsKey("last_ssid_name"))  setStr(cfg.last_ssid_name,  sizeof(cfg.last_ssid_name),  String((const char*)doc["last_ssid_name"]));
  else cfg.last_ssid_name[0] = '\0';
  if (doc.containsKey("mqtt_server"))  setStr(cfg.mqtt_server, sizeof(cfg.mqtt_server), String((const char*)doc["mqtt_server"]));
  if (doc.containsKey("mqtt_port"))    cfg.mqtt_port = (int)doc["mqtt_port"];
  if (doc.containsKey("mqtt_user"))    setStr(cfg.mqtt_user, sizeof(cfg.mqtt_user), String((const char*)doc["mqtt_user"]));
  if (doc.containsKey("mqtt_pass"))    setStr(cfg.mqtt_pass, sizeof(cfg.mqtt_pass), String((const char*)doc["mqtt_pass"]));
  if (doc.containsKey("ha_discovery")) cfg.ha_discovery = (bool)doc["ha_discovery"];
  if (doc.containsKey("log_web"))     cfg.log_web      = (bool)doc["log_web"];
  else cfg.log_web = false; // default OFF — enable in Config page when needed

  // Network mode + Ethernet PHY config (ESP32 only; harmless on ESP8266)
  if (doc.containsKey("net_mode"))       cfg.net_mode      = (uint8_t)(int)doc["net_mode"];
  if (doc.containsKey("eth_phy_type"))   cfg.eth_phy_type  = (uint8_t)(int)doc["eth_phy_type"];
  if (doc.containsKey("eth_phy_addr"))   cfg.eth_phy_addr  = (uint8_t)(int)doc["eth_phy_addr"];
  if (doc.containsKey("eth_power_pin"))  cfg.eth_power_pin = (int8_t) (int)doc["eth_power_pin"];
  if (doc.containsKey("eth_mdc_pin"))    cfg.eth_mdc_pin   = (int8_t) (int)doc["eth_mdc_pin"];
  if (doc.containsKey("eth_mdio_pin"))   cfg.eth_mdio_pin  = (int8_t) (int)doc["eth_mdio_pin"];

  // RS485 pins & mode (optional; -1 means not used)
  if (doc.containsKey("tx_pin")) txPin = sanitizeGpio((int)doc["tx_pin"]);
  if (doc.containsKey("rx_pin")) rxPin = sanitizeGpio((int)doc["rx_pin"]);
  if (doc.containsKey("de_pin")) dePin = sanitizeGpio((int)doc["de_pin"]);
  if (doc.containsKey("led_pin"))   ledPin = sanitizeLedGpio((int)doc["led_pin"]);
  if (doc.containsKey("led_invert"))   ledActiveLow = (bool)doc["led_invert"];
  if (doc.containsKey("btn_pin"))      buttonPin = sanitizeButtonGpio((int)doc["btn_pin"]);
  if (doc.containsKey("btn_invert"))   buttonActiveLow = (bool)doc["btn_invert"];
  if (doc.containsKey("dynet_max_channels")) cfg.dynet_max_channels = (uint8_t)doc["dynet_max_channels"];
  if (doc.containsKey("dynet_max_areas"))    cfg.dynet_max_areas    = (uint8_t)doc["dynet_max_areas"];
  if (doc.containsKey("ha_preset_count"))    cfg.ha_preset_count    = (uint8_t)doc["ha_preset_count"];
  return true;
}

bool saveConfig() {
  if (deviceId.length() == 0) deviceId = CHIP_ID_STR;

  DynamicJsonDocument doc(2048);
  doc["device_id"]    = deviceId;
  doc["ap_ssid"]      = apSsid;
  doc["ap_pass"]      = apPass;
  doc["wifi_ssid"]    = cfg.wifi_ssid;
  doc["wifi_pass"]    = cfg.wifi_pass;
  doc["wifi_ssid2"]      = cfg.wifi_ssid2;
  doc["wifi_pass2"]      = cfg.wifi_pass2;
  doc["last_ssid_name"]  = cfg.last_ssid_name;
  doc["mqtt_server"]  = cfg.mqtt_server;
  doc["mqtt_port"]    = cfg.mqtt_port;
  doc["mqtt_user"]    = cfg.mqtt_user;
  doc["mqtt_pass"]    = cfg.mqtt_pass;
  doc["ha_discovery"] = cfg.ha_discovery;
  doc["log_web"]      = cfg.log_web;

  // Network mode + Ethernet PHY
  doc["net_mode"]      = cfg.net_mode;
  doc["eth_phy_type"]  = cfg.eth_phy_type;
  doc["eth_phy_addr"]  = cfg.eth_phy_addr;
  doc["eth_power_pin"] = cfg.eth_power_pin;
  doc["eth_mdc_pin"]   = cfg.eth_mdc_pin;
  doc["eth_mdio_pin"]  = cfg.eth_mdio_pin;

  doc["tx_pin"] = txPin;
  doc["rx_pin"] = rxPin;
  doc["de_pin"] = dePin;
  doc["led_pin"]      = ledPin;
  doc["led_invert"]   = ledActiveLow;
  doc["btn_pin"]      = buttonPin;
  doc["btn_invert"]   = buttonActiveLow;
  doc["dynet_max_channels"] = cfg.dynet_max_channels;
  doc["dynet_max_areas"]    = cfg.dynet_max_areas;
  doc["ha_preset_count"]    = cfg.ha_preset_count;

  File f = LittleFS.open(CONFIG_FILE, "w"); if (!f) return false;
  size_t n = serializeJson(doc, f); f.flush(); f.close();
  return (n > 0);
}

// ---------- entities.json ----------
bool loadEntities() {
  using namespace DynetEntities;
  if (!LittleFS.exists(ENTITIES_FILE)) return false;
  File f = LittleFS.open(ENTITIES_FILE, "r"); if (!f) return false;

  // ESP32 can have 255 areas — needs a large pool.
  // ESP8266: max 32 areas + 200 channels; ArduinoJson pool for that is ~8-10 KB.
  // 10 KB was proven to work; going higher causes malloc failure on the
  // fragmented post-WiFi/MQTT heap and silently breaks persistence.
#if defined(ESP32)
  DynamicJsonDocument doc(65536);
#else
  DynamicJsonDocument doc(10240);
#endif
  DeserializationError err = deserializeJson(doc, f, DeserializationOption::NestingLimit(8));
  f.close();
  if (err) {
    LOGF("[persist] loadEntities failed: %s\n", err.c_str());
    return false;
  }

  // Always begin with compile-time maximum caps so that no saved entities are
  // silently dropped because the user lowered cfg.dynet_max_areas/channels.
  // The user cap only controls sweep/discovery, not what we can hold in RAM.
  em.begin(DYNET_MAX_AREAS, DYNET_MAX_CHANNELS);
  em.setLoading(true);   // suppress publish/save inside touchArea/touchChannel

  // channels
  if (doc.containsKey("channels") && doc["channels"].is<JsonArray>()) {
    JsonArray arr = doc["channels"].as<JsonArray>();
    for (JsonVariant v : arr) {
      // accept both shapes: a/c/t (compact) and area/ch/type (webui)
      uint8_t area = (uint8_t)(v["a"]    | v["area"] | 0);
      uint8_t ch0  = (uint8_t)(v["c"]    | v["ch"]   | 0);
      uint8_t type = (uint8_t)(v["t"]    | v["type"] | DynetEntities::LIGHT_DIMMABLE);
      if (area > 0) {
        int i = em.touchChannel(area, ch0);
        if (i >= 0) {
          auto& ch = em.channelAtMut(i);
          ch.type = (DynetEntities::EntityType)type;
          if (v.containsKey("slave") && (bool)v["slave"]) ch.isCurtainSlave = true;
          if (v.containsKey("ctime")) ch.curtainTimeSec = (uint8_t)v["ctime"];
          if (v.containsKey("n") && v["n"].is<const char*>()) {
            strncpy(ch.name, (const char*)v["n"], sizeof(ch.name) - 1);
            ch.name[sizeof(ch.name) - 1] = '\0';
          }
        }
      }
      delay(0);
    }
    //return true;
  }

  // areas (lightweight bits: preset + hvac presence/values if you saved them)
    if (doc.containsKey("areas") && doc["areas"].is<JsonArray>()) {
    for (JsonObject v : doc["areas"].as<JsonArray>()) {
      uint8_t area   = v["a"] | 0;
      uint8_t preset = v["p"] | 0xFF;
      if (area > 0) {
        em.touchArea(area);
        if (preset != 0xFF) em.noteReportPreset(area, preset);
        if (v.containsKey("tempC"))  em.noteActualTemp_fp(area, (uint8_t)floor(fabs((double)v["tempC"])), (uint8_t)((fabs((double)v["tempC"]) - floor(fabs((double)v["tempC"]))) * 100.0));
        if (v.containsKey("setptC")) em.noteSetpoint_fp  (area, (uint8_t)floor(fabs((double)v["setptC"])), (uint8_t)((fabs((double)v["setptC"]) - floor(fabs((double)v["setptC"]))) * 100.0));
        if (v.containsKey("n") && v["n"].is<const char*>()) {
          int ai2 = em.findArea(area);
          if (ai2 >= 0) {
            auto& ar = em.areaAtMut(ai2);
            strncpy(ar.name, (const char*)v["n"], sizeof(ar.name) - 1);
            ar.name[sizeof(ar.name) - 1] = '\0';
          }
        }
        if (v.containsKey("pc")) {
          int ai2 = em.findArea(area);
          if (ai2 >= 0) {
            uint8_t pc = (uint8_t)constrain((int)v["pc"], 1, 128);
            em.areaAtMut(ai2).presetCount = pc;
          }
        }
        // Fade time (0.1 s units; LIGHTS default = 20 = 2.0 s; CURTAIN/HVAC = 0)
        if (v.containsKey("fade")) {
          int ai2 = em.findArea(area);
          if (ai2 >= 0) {
            em.areaAtMut(ai2).fadeTenths = (uint16_t)constrain((int)v["fade"], 0, 600);
          }
        }
        // Preset names for AREA_LIGHTS
        if (v.containsKey("pnames") && v["pnames"].is<JsonArray>()) {
          int ai2 = em.findArea(area);
          if (ai2 >= 0) {
            auto& ar = em.areaAtMut(ai2);
            if (!ar.presets) ar.presets = new (std::nothrow) DynetEntities::AreaPresetNames{};
            if (ar.presets) {
              uint8_t pi = 0;
              for (JsonVariant pv : v["pnames"].as<JsonArray>()) {
                if (pi >= DynetEntities::MAX_LIGHT_PRESETS) break;
                if (pv.is<const char*>() && pv.as<const char*>()[0]) {
                  strncpy(ar.presets->n[pi], pv.as<const char*>(), sizeof(ar.presets->n[pi]) - 1);
                  ar.presets->n[pi][sizeof(ar.presets->n[pi]) - 1] = '\0';
                }
                pi++;
              }
            }
          }
        }
        // Area type + per-curtain/hvac entries
        if (v.containsKey("at")) {
          int ai2 = em.findArea(area);
          if (ai2 >= 0) {
            auto& ar = em.areaAtMut(ai2);
            ar.areaType = (DynetEntities::AreaType)(uint8_t)v["at"];

            // Allocate curtain array on demand
            if (ar.areaType == DynetEntities::AREA_CURTAIN && !ar.curtains) {
              ar.curtains = new (std::nothrow) DynetEntities::AreaCurtainEntry[DynetEntities::MAX_CURTAINS_PER_AREA];
              if (ar.curtains) {
                for (int ci = 0; ci < DynetEntities::MAX_CURTAINS_PER_AREA; ci++)
                  ar.curtains[ci] = DynetEntities::AreaCurtainEntry{};
              }
            }
            if (ar.curtains && v.containsKey("curtains") && v["curtains"].is<JsonArray>()) {
              uint8_t ci = 0;
              for (JsonObject ce : v["curtains"].as<JsonArray>()) {
                if (ci >= DynetEntities::MAX_CURTAINS_PER_AREA) break;
                ar.curtains[ci].used        = true;
                ar.curtains[ci].openPreset  = ce["op"] | 1;
                ar.curtains[ci].closePreset = ce["cl"] | 2;
                ar.curtains[ci].stopPreset  = ce["st"] | 3;
                if (ce.containsKey("n") && ce["n"].is<const char*>()) {
                  strncpy(ar.curtains[ci].name, (const char*)ce["n"], sizeof(ar.curtains[ci].name) - 1);
                  ar.curtains[ci].name[sizeof(ar.curtains[ci].name) - 1] = '\0';
                }
                ci++;
                delay(0);
              }
            }

            // Allocate PIR config on demand (PIR is an overlay, not tied to area type)
            if (v.containsKey("pir") && !ar.pir) {
              ar.pir = new (std::nothrow) DynetEntities::PirConfig{};
            }
            if (ar.pir && v.containsKey("pir")) {
              JsonObject pv = v["pir"].as<JsonObject>();
              ar.pir->occEnabled = pv["en"] | true;  // default enabled
            }

            // Allocate HVAC config on demand
            if (ar.areaType == DynetEntities::AREA_HVAC && !ar.hvac) {
              ar.hvac = new (std::nothrow) DynetEntities::HvacConfig{};
            }
            if (ar.hvac && v.containsKey("hvac")) {
              JsonObject hv = v["hvac"].as<JsonObject>();
              // Restore setpoint step (0.5 or 1.0)
              if (hv.containsKey("step")) {
                float s = (float)(double)hv["step"];
                ar.hvac->setptStep = (s >= 1.0f) ? 1.0f : 0.5f;
              }
              // Restore last-known mode and fan mode
              if (hv.containsKey("curMode") && hv["curMode"].is<const char*>()) {
                strncpy(ar.hvac->currentMode, (const char*)hv["curMode"], sizeof(ar.hvac->currentMode) - 1);
                ar.hvac->currentMode[sizeof(ar.hvac->currentMode) - 1] = '\0';
              }
              if (hv.containsKey("curFan") && hv["curFan"].is<const char*>()) {
                strncpy(ar.hvac->currentFanMode, (const char*)hv["curFan"], sizeof(ar.hvac->currentFanMode) - 1);
                ar.hvac->currentFanMode[sizeof(ar.hvac->currentFanMode) - 1] = '\0';
              }
              // Mode/Fan control type fields (added later — safe to be absent in old files)
              if (hv.containsKey("mCtrl")) ar.hvac->modeCtrlType  = (uint8_t)(int)hv["mCtrl"];
              if (hv.containsKey("mSrcA")) ar.hvac->modeArea      = (uint8_t)(int)hv["mSrcA"];
              if (hv.containsKey("mCh"))   ar.hvac->modeChannel0  = (uint8_t)(int)hv["mCh"];
              if (hv.containsKey("fCtrl")) ar.hvac->fanCtrlType   = (uint8_t)(int)hv["fCtrl"];
              if (hv.containsKey("fSrcA")) ar.hvac->fanArea       = (uint8_t)(int)hv["fSrcA"];
              if (hv.containsKey("fCh"))   ar.hvac->fanChannel0   = (uint8_t)(int)hv["fCh"];
              if (hv.containsKey("modes") && hv["modes"].is<JsonArray>()) {
                uint8_t mi = 0;
                for (JsonObject me : hv["modes"].as<JsonArray>()) {
                  if (mi >= DynetEntities::MAX_HVAC_MODES) break;
                  ar.hvac->modes[mi].used    = true;
                  ar.hvac->modes[mi].preset1 = me["p"]  | 1;
                  ar.hvac->modes[mi].level   = me["lv"] | 0;
                  if (me.containsKey("n") && me["n"].is<const char*>()) {
                    strncpy(ar.hvac->modes[mi].name, (const char*)me["n"], sizeof(ar.hvac->modes[mi].name) - 1);
                    ar.hvac->modes[mi].name[sizeof(ar.hvac->modes[mi].name) - 1] = '\0';
                  }
                  ar.hvac->modeCount++;
                  mi++;
                  delay(0);
                }
              }
              if (hv.containsKey("fans") && hv["fans"].is<JsonArray>()) {
                uint8_t fi = 0;
                for (JsonObject fe : hv["fans"].as<JsonArray>()) {
                  if (fi >= DynetEntities::MAX_HVAC_FANMODES) break;
                  ar.hvac->fanModes[fi].used    = true;
                  ar.hvac->fanModes[fi].preset1 = fe["p"]  | 1;
                  ar.hvac->fanModes[fi].level   = fe["lv"] | 0;
                  if (fe.containsKey("n") && fe["n"].is<const char*>()) {
                    strncpy(ar.hvac->fanModes[fi].name, (const char*)fe["n"], sizeof(ar.hvac->fanModes[fi].name) - 1);
                    ar.hvac->fanModes[fi].name[sizeof(ar.hvac->fanModes[fi].name) - 1] = '\0';
                  }
                  ar.hvac->fanCount++;
                  fi++;
                  delay(0);
                }
              }
            }
          }
        }
      }
      delay(0);
    }
  }
  em.setLoading(false);  // re-enable publish/save
  if (doc.overflowed()) {
    LOGF("[persist] WARNING: JSON doc overflowed during load — some entities may be missing! Increase capacity.\n");
  }
  LOGF("[persist] loaded %d channels, %d areas from %s\n", em.channelsCount(), em.areasCount(), ENTITIES_FILE);
  return true;
}

// ── Dirty flag shared by saveEntities() / saveEntitiesNow() / serviceEntitiesSave() ──
static bool          _entDirty   = false;
static unsigned long _entDirtyAt = 0;
static const unsigned long SAVE_DEBOUNCE_MS = 8000UL;  // coalesce bursts into one write

// ── Internal: do the actual JSON write (no debounce logic here) ──────────────
static bool doWriteEntities() {
  using namespace DynetEntities;

  // ── Build JSON doc BEFORE opening the file ─────────────────────────────
  // CRITICAL: LittleFS.open("w") truncates immediately. Build + validate
  // first so a failed malloc never wipes the existing file.
#if defined(ESP32)
  static const size_t DOC_SIZE = 65536;
#else
  // ~1.5 KB output; ArduinoJson needs ~3-4× for its node pool. 6 KB fits the
  // fragmented post-WiFi/MQTT heap much better than 10 KB.
  static const size_t DOC_SIZE = 6144;
#endif

#if defined(ESP8266)
  if (ESP.getFreeHeap() < (DOC_SIZE + 4096)) {
    LOGF("[persist] saveEntities: heap low (%u B free) — will retry\n",
         (unsigned)ESP.getFreeHeap());
    return false;   // caller re-schedules
  }
#endif

  DynamicJsonDocument doc(DOC_SIZE);
  if (doc.capacity() == 0) {
    LOGF("[persist] saveEntities: OOM — doc malloc failed, keeping old file\n");
    return false;
  }

  // channels
  JsonArray chs = doc.createNestedArray("channels");
  for (int i = 0; i < em.channelsCount(); i++) {
    const auto& c = em.channelAt(i);
    if (!c.present) continue;
    JsonObject o = chs.createNestedObject();
    o["a"] = c.area;
    o["c"] = c.channel0;
    o["t"] = (uint8_t)c.type;
    if (c.isCurtainSlave)            o["slave"] = true;
    if (c.type == DynetEntities::CURTAIN && !c.isCurtainSlave) o["ctime"] = c.curtainTimeSec;
    if (c.name[0]) o["n"] = c.name;
    // Not saving level/isOn to avoid heavy write cycles during fades.
  }

  // areas
  JsonArray ars = doc.createNestedArray("areas");
  for (int i = 0; i < em.areasCount(); i++) {
    const auto& a = em.areaAt(i);
    if (!a.present) continue;
    JsonObject o = ars.createNestedObject();
    o["a"] = a.area;
    o["p"] = a.preset0;               // last-known preset (0xFF if unknown)
    if (a.hasTemp)  o["tempC"]  = a.tempC;
    if (a.hasSetpt) o["setptC"] = a.setptC;
    if (a.name[0])  o["n"]      = a.name;
    o["pc"] = a.presetCount ? a.presetCount : 4;
    if (a.fadeTenths != 20) o["fade"] = a.fadeTenths;  // omit when default (2.0 s) to keep JSON compact
    if (a.pir) {  // PIR overlay — saved independently of area type
      JsonObject pv = o.createNestedObject("pir");
      if (!a.pir->occEnabled) pv["en"] = false;  // omit when default (true) to keep JSON compact
    }
    if (a.areaType == DynetEntities::AREA_CURTAIN && a.curtains) {
      o["at"] = (uint8_t)a.areaType;
      JsonArray cArr = o.createNestedArray("curtains");
      for (uint8_t ci = 0; ci < DynetEntities::MAX_CURTAINS_PER_AREA; ci++) {
        const auto& ce = a.curtains[ci];
        if (!ce.used) continue;
        JsonObject co = cArr.createNestedObject();
        if (ce.name[0]) co["n"] = ce.name;
        co["op"] = ce.openPreset;
        co["cl"] = ce.closePreset;
        co["st"] = ce.stopPreset;
      }
    }
    // Preset names (AREA_LIGHTS only)
    if (a.areaType == DynetEntities::AREA_LIGHTS && a.presets) {
      bool anyName = false;
      for (uint8_t p = 0; p < DynetEntities::MAX_LIGHT_PRESETS; p++)
        if (a.presets->n[p][0]) { anyName = true; break; }
      if (anyName) {
        JsonArray pArr = o.createNestedArray("pnames");
        for (uint8_t p = 0; p < DynetEntities::MAX_LIGHT_PRESETS; p++)
          pArr.add(a.presets->n[p][0] ? a.presets->n[p] : "");
      }
    }
    if (a.areaType == DynetEntities::AREA_HVAC && a.hvac) {
      o["at"] = (uint8_t)a.areaType;
      JsonObject hv = o.createNestedObject("hvac");
      hv["step"] = a.hvac->setptStep;
      if (a.hvac->currentMode[0])    hv["curMode"] = a.hvac->currentMode;
      if (a.hvac->currentFanMode[0]) hv["curFan"]  = a.hvac->currentFanMode;
      // Mode control type / source area / channel (omit defaults to keep JSON compact)
      if (a.hvac->modeCtrlType)  hv["mCtrl"] = a.hvac->modeCtrlType;
      if (a.hvac->modeArea)      hv["mSrcA"] = a.hvac->modeArea;
      if (a.hvac->modeCtrlType == DynetEntities::HVAC_CTRL_LEVEL || a.hvac->modeArea)
                                 hv["mCh"]   = a.hvac->modeChannel0;
      if (a.hvac->fanCtrlType)   hv["fCtrl"] = a.hvac->fanCtrlType;
      if (a.hvac->fanArea)       hv["fSrcA"] = a.hvac->fanArea;
      if (a.hvac->fanCtrlType == DynetEntities::HVAC_CTRL_LEVEL || a.hvac->fanArea)
                                 hv["fCh"]   = a.hvac->fanChannel0;
      JsonArray mArr = hv.createNestedArray("modes");
      for (uint8_t mi = 0; mi < DynetEntities::MAX_HVAC_MODES; mi++) {
        const auto& me = a.hvac->modes[mi];
        if (!me.used) continue;
        JsonObject mo = mArr.createNestedObject();
        mo["n"]  = me.name;
        mo["p"]  = me.preset1;
        if (me.level > 0) mo["lv"] = me.level;  // omit 0 to stay compact
      }
      JsonArray fArr = hv.createNestedArray("fans");
      for (uint8_t fi = 0; fi < DynetEntities::MAX_HVAC_FANMODES; fi++) {
        const auto& fe = a.hvac->fanModes[fi];
        if (!fe.used) continue;
        JsonObject fo = fArr.createNestedObject();
        fo["n"]  = fe.name;
        fo["p"]  = fe.preset1;
        if (fe.level > 0) fo["lv"] = fe.level;
      }
    }
  }

  // Guard: doc overflowed — old file is still intact, do not overwrite it
  if (doc.overflowed()) {
    LOGF("[persist] saveEntities: JSON doc overflowed — keeping old file safe\n");
    return false;
  }

  // ── Only now open (and truncate) the file ──────────────────────────────
  File f = LittleFS.open(ENTITIES_FILE, "w");
  if (!f) { LOGF("[persist] saveEntities: cannot open file for write\n"); return false; }
  size_t n = serializeJson(doc, f);
  f.flush(); f.close();

  LOGF("[persist] saveEntities: wrote %u bytes, ch=%d areas=%d\n",
       (unsigned)n, em.channelsCount(), em.areasCount());
  return (n > 0);
}

// ── Public API ────────────────────────────────────────────────────────────────

// Mark dirty — actual write is deferred.  Safe to call from any event handler.
bool saveEntities() {
  if (!_entDirty) {
    _entDirty   = true;
    _entDirtyAt = millis();
  }
  return true;
}

// Write immediately, bypass the debounce window.
// Use after user-triggered actions: import, restore, explicit area/channel edits.
bool saveEntitiesNow() {
  bool ok = doWriteEntities();
  if (ok) _entDirty = false;   // cancel any pending deferred write
  else  { _entDirty = true; _entDirtyAt = millis(); }  // reschedule on failure
  return ok;
}

// Call every loop().  Fires the deferred write once the debounce window elapses.
void serviceEntitiesSave() {
  if (!_entDirty) return;
  if ((unsigned long)(millis() - _entDirtyAt) < SAVE_DEBOUNCE_MS) return;
  bool ok = doWriteEntities();
  if (ok) _entDirty = false;
  else    _entDirtyAt = millis();  // retry after another full window on failure
}

// same helper as your Somfy project
bool extractTopObject(const String& src, const char* key, String& out) {
  out = "";
  String pat = "\"" + String(key) + "\"";
  int k = src.indexOf(pat);
  if (k < 0) return false;
  int colon = src.indexOf(':', k + pat.length());
  if (colon < 0) return false;
  int i = colon + 1;
  while (i < (int)src.length() && (src[i]==' '||src[i]=='\n'||src[i]=='\r'||src[i]=='\t')) i++;
  if (i >= (int)src.length() || src[i] != '{') return false;

  int start = i, depth = 0;
  for (; i < (int)src.length(); i++) {
    char c = src[i];
    if (c == '{') depth++;
    else if (c == '}') { depth--; if (depth == 0) { out = src.substring(start, i + 1); return true; } }
  }
  return false;
}
