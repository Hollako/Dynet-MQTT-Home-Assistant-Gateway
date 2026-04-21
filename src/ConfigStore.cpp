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
  if (!LittleFS.exists(CONFIG_FILE)) return false;
  File f = LittleFS.open(CONFIG_FILE, "r"); if (!f) return false;

  DynamicJsonDocument doc(3072);
  DeserializationError err = deserializeJson(doc, f);
  f.close(); if (err) return false;

  if (doc.containsKey("ap_ssid"))      apSsid = String((const char*)doc["ap_ssid"]);
  if (doc.containsKey("ap_pass"))      apPass = String((const char*)doc["ap_pass"]);
  if (doc.containsKey("wifi_ssid"))    setStr(cfg.wifi_ssid, sizeof(cfg.wifi_ssid), String((const char*)doc["wifi_ssid"]));
  if (doc.containsKey("wifi_pass"))    setStr(cfg.wifi_pass, sizeof(cfg.wifi_pass), String((const char*)doc["wifi_pass"]));
  if (doc.containsKey("mqtt_server"))  setStr(cfg.mqtt_server, sizeof(cfg.mqtt_server), String((const char*)doc["mqtt_server"]));
  if (doc.containsKey("mqtt_port"))    cfg.mqtt_port = (int)doc["mqtt_port"];
  if (doc.containsKey("mqtt_user"))    setStr(cfg.mqtt_user, sizeof(cfg.mqtt_user), String((const char*)doc["mqtt_user"]));
  if (doc.containsKey("mqtt_pass"))    setStr(cfg.mqtt_pass, sizeof(cfg.mqtt_pass), String((const char*)doc["mqtt_pass"]));
  if (doc.containsKey("ha_discovery")) cfg.ha_discovery = (bool)doc["ha_discovery"];

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
  doc["mqtt_server"]  = cfg.mqtt_server;
  doc["mqtt_port"]    = cfg.mqtt_port;
  doc["mqtt_user"]    = cfg.mqtt_user;
  doc["mqtt_pass"]    = cfg.mqtt_pass;
  doc["ha_discovery"] = cfg.ha_discovery;

  doc["tx_pin"] = txPin;
  doc["rx_pin"] = rxPin;
  doc["de_pin"] = dePin;
  doc["led_pin"]      = ledPin;
  doc["led_invert"]   = ledActiveLow;
  doc["btn_pin"]      = buttonPin;
  doc["btn_invert"]   = buttonActiveLow;
  doc["dynet_max_channels"] = cfg.dynet_max_channels;
  doc["dynet_max_areas"]    = cfg.dynet_max_areas;

  File f = LittleFS.open(CONFIG_FILE, "w"); if (!f) return false;
  size_t n = serializeJson(doc, f); f.flush(); f.close();
  return (n > 0);
}

// ---------- entities.json ----------
bool loadEntities() {
  using namespace DynetEntities;
  if (!LittleFS.exists(ENTITIES_FILE)) return false;
  File f = LittleFS.open(ENTITIES_FILE, "r"); if (!f) return false;

  DynamicJsonDocument doc(6144);
  DeserializationError err = deserializeJson(doc, f, DeserializationOption::NestingLimit(6));
  f.close(); if (err) return false;

  em.begin();
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
          em.setChannelType(area, ch0, (DynetEntities::EntityType)type);
          if (v.containsKey("n") && v["n"].is<const char*>()) {
            auto& ch = em.channelAtMut(i);
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
      }
      delay(0);
    }
  }
  em.setLoading(false);  // re-enable publish/save
  LOGF("[persist] loaded %d channels, %d areas from %s\n", em.channelsCount(), em.areasCount(), ENTITIES_FILE);
  return true;
}

bool saveEntities() {
  using namespace DynetEntities;
  File f = LittleFS.open(ENTITIES_FILE, "w"); if (!f) return false;

  DynamicJsonDocument doc(6144);

  // channels
  JsonArray chs = doc.createNestedArray("channels");
  for (int i = 0; i < em.channelsCount(); i++) {
    const auto& c = em.channelAt(i);
    if (!c.present) continue;
    JsonObject o = chs.createNestedObject();
    o["a"] = c.area;
    o["c"] = c.channel0;
    o["t"] = (uint8_t)c.type;
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
  }

  String out; serializeJson(doc, out);
  size_t n = f.print(out); f.flush(); f.close();
  return (n > 0);
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
