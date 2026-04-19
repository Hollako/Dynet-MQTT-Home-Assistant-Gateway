#include "Globals.h"
#include "MqttManager.h"

// Include your new modules
#include "DynetBus.h"
#include "EntityManager.h"

// If the Somfy project had BASE_TOPIC_PREFIX defined, override here safely.
#ifndef HA_DISCOVERY_PREFIX
#define HA_DISCOVERY_PREFIX "homeassistant"
#endif
#ifndef BASE_TOPIC_PREFIX
#define BASE_TOPIC_PREFIX "dynet"
#endif

extern DynetBus dynet;
namespace DynetEntities { extern EntityManager em; }

// pacing
static unsigned long lastMqttAttempt = 0;
static const unsigned long MQTT_RETRY_MS = 5000;

// ---- Utilities ---------------------------------------------------
// (kept for reference; no longer used for per-area devices)
static void addHadeviceBlockForArea(JsonDocument& doc, uint8_t area) {
  JsonObject dev = doc.createNestedObject("device");
  String sid = mqttSafeId(deviceId);

  // Unique device id per area
  JsonArray ids = dev.createNestedArray("identifiers");
  ids.add(sid + "_area" + String(area));

  // IMPORTANT: Do NOT add "connections" here, or all areas will share the same MAC and merge.

  dev["manufacturer"] = HA_MANUFACTURER;
  dev["model"]        = String(HA_MODEL) + " (Area)";
  dev["name"]         = String("DyNet Area ") + area;
  dev["sw_version"]   = HA_SW_VERSION;

  IPAddress ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP() : WiFi.softAPIP();
  dev["configuration_url"] = String("http://") + ip.toString() + "/";
}

String availabilityTopic() {
  String sid = mqttSafeId(deviceId);
  return String(BASE_TOPIC_PREFIX) + "/" + sid + "/availability";
}
String areaBaseTopic(uint8_t area) {
  String sid = mqttSafeId(deviceId);
  return String(BASE_TOPIC_PREFIX) + "/" + sid + "/area/" + String(area);
}
String channelBaseTopic(uint8_t area, uint8_t channel0) {
  String sid = mqttSafeId(deviceId);
  return String(BASE_TOPIC_PREFIX) + "/" + sid + "/area/" + String(area) + "/ch/" + String(channel0);
}

void publishAvailability(const char* payload) {
  mqtt.publish(availabilityTopic().c_str(), payload, true);
}

// Remove any stale discovery for the other component class
static void retireOppositeDiscovery(uint8_t area, uint8_t ch0, DynetEntities::EntityType newType) {
  if (!mqtt.connected()) return;             // don't fail the main publish if we're offline

  const char* opposite =
      (newType == DynetEntities::SWITCH_ONOFF) ? "light" : "switch";   // lights (both types) vs switch

  String sid   = mqttSafeId(deviceId);
  String objId = String("a") + String(area) + "_c" + String(ch0);
  String t     = String(HA_DISCOVERY_PREFIX) + "/" + opposite + "/" + sid + "/" + objId + "/config";

  // Use a single empty retained publish to delete. Avoid sending a second “null” payload.
  mqtt.publish(t.c_str(), "", true);
}

// ---- HA Discovery ------------------------------------------------
// Light (JSON schema) or Switch; we choose by entity.type
static void publishHADiscovery_LightOrSwitch(const DynetEntities::ChannelState& chs) {
  using namespace DynetEntities;
  const bool isLight = (chs.type == LIGHT_DIMMABLE || chs.type == LIGHT_ONOFF);
  const char* comp   = isLight ? "light" : "switch";

  String sid = mqttSafeId(deviceId);
  String objId = String("a") + String(chs.area) + "_c" + String(chs.channel0);
  String discTopic = String(HA_DISCOVERY_PREFIX) + "/" + comp + "/" + sid + "/" + objId + "/config";

  DynamicJsonDocument doc(768);
  doc["name"]               = String("Area ") + chs.area + " Ch " + (chs.channel0 + 1);
  doc["unique_id"]          = sid + "_" + objId;
  doc["availability_topic"] = availabilityTopic();

  String base = channelBaseTopic(chs.area, chs.channel0);

  if (isLight) {
    // Use JSON schema to handle brightness in one topic
    doc["schema"]        = "json";
    doc["command_topic"] = base + "/set";
    doc["state_topic"]   = base + "/state";
    doc["brightness"]    = (chs.type == LIGHT_DIMMABLE);
  } else {
    // switch
    doc["command_topic"]   = base + "/set";
    doc["state_topic"]     = base + "/state";
    doc["payload_on"]      = "ON";
    doc["payload_off"]     = "OFF";
    //doc["optimistic"]      = true;
  }

  // <-- per-area device block
  addHadeviceBlockForArea(doc, chs.area);

  String payload; serializeJson(doc, payload);
  mqtt.publish(discTopic.c_str(), payload.c_str(), true);
  // prime state as unknown
  mqtt.publish((base + "/state").c_str(), "{\"state\":\"unknown\"}", true);
}

// Temperature sensor
static void publishHADiscovery_TempSensor(uint8_t area) {
  String sid = mqttSafeId(deviceId);
  String objId = String("a") + String(area) + "_temp";
  String discTopic = String(HA_DISCOVERY_PREFIX) + "/sensor/" + sid + "/" + objId + "/config";
  DynamicJsonDocument doc(512);
  doc["name"]               = String("Area ") + area + " Temperature";
  doc["unique_id"]          = sid + "_" + objId;
  doc["state_topic"]        = areaBaseTopic(area) + "/temperature";
  doc["unit_of_measurement"] = "°C";
  doc["device_class"]       = "temperature";
  doc["availability_topic"] = availabilityTopic();
  addHadeviceBlockForArea(doc, area);
  String payload; serializeJson(doc, payload);
  mqtt.publish(discTopic.c_str(), payload.c_str(), true);
}

// Setpoint as a Number entity (simple + reliable)
static void publishHADiscovery_SetpointNumber(uint8_t area) {
  String sid = mqttSafeId(deviceId);
  String objId = String("a") + String(area) + "_setpoint";
  String discTopic = String(HA_DISCOVERY_PREFIX) + "/number/" + sid + "/" + objId + "/config";
  DynamicJsonDocument doc(512);
  doc["name"]               = String("Area ") + area + " Setpoint";
  doc["unique_id"]          = sid + "_" + objId;
  doc["command_topic"]      = areaBaseTopic(area) + "/setpoint/set";
  doc["state_topic"]        = areaBaseTopic(area) + "/setpoint";
  doc["min"] = 10.0; doc["max"] = 35.0; doc["step"] = 0.5;
  doc["unit_of_measurement"] = "°C";
  doc["availability_topic"] = availabilityTopic();
  addHadeviceBlockForArea(doc, area);
  String payload; serializeJson(doc, payload);
  mqtt.publish(discTopic.c_str(), payload.c_str(), true);
}

// --- Preset "select" entity (HA MQTT Select) ---
static void publishHADiscovery_PresetSelect(uint8_t area) {
  String sid = mqttSafeId(deviceId);
  String objId = String("a") + String(area) + "_preset";
  String discTopic = String(HA_DISCOVERY_PREFIX) + "/select/" + sid + "/" + objId + "/config";

  DynamicJsonDocument doc(768);
  doc["name"]               = String("Area ") + area + " Preset";
  doc["unique_id"]          = sid + "_" + objId;
  doc["command_topic"]      = areaBaseTopic(area) + "/preset/set";
  doc["state_topic"]        = areaBaseTopic(area) + "/preset";
  doc["availability_topic"] = availabilityTopic();

  JsonArray opts = doc.createNestedArray("options");
  for (int p = 1; p <= 16; ++p) opts.add(String(p));

  addHadeviceBlockForArea(doc, area);
  String payload; serializeJson(doc, payload);
  mqtt.publish(discTopic.c_str(), payload.c_str(), true);
}

void publishHADiscoveryForChannel(int idx) {
  
  using namespace DynetEntities;
  if (idx < 0 || idx >= em.channelsCount()) return;
  const ChannelState& chs = em.channelAt(idx);

  // --- build topics/JSON for current type ---
  String sid   = mqttSafeId(deviceId);
  String objId = String("a") + String(chs.area) + "_c" + String(chs.channel0);
  String comp  = (chs.type == SWITCH_ONOFF) ? "switch" : "light";
  String cfgTopic = String(HA_DISCOVERY_PREFIX) + "/" + comp + "/" + sid + "/" + objId + "/config";
  LOGF("[HA] preparing discovery for A%u C%u as %s\n", chs.area, chs.channel0, (chs.type==SWITCH_ONOFF?"switch":"light"));
  DynamicJsonDocument doc(512);
  addHadeviceBlockForArea(doc, chs.area);          // your existing helper
  doc["unique_id"] = sid + "_" + objId;
  doc["name"]      = String("Area ") + chs.area + " Ch " + (chs.channel0 + 1);
  String base      = channelBaseTopic(chs.area, chs.channel0);
  doc["state_topic"]   = base + "/state";
  doc["availability_topic"] = availabilityTopic();       // if you have a helper; else set availability_topic + payloads

  if (chs.type == SWITCH_ONOFF) {
    // SWITCH -> plain payloads
    doc["command_topic"] = base + "/set";
    doc["payload_on"]    = "ON";
    doc["payload_off"]   = "OFF";
    // Do NOT set optimistic: true (keeps UI as single toggle)
  } else {
    // LIGHT (both dimmable and on/off) -> JSON light schema
    doc["schema"]        = "json";
    doc["command_topic"] = base + "/set";
    if (chs.type == LIGHT_DIMMABLE) doc["brightness"] = true;
  }

  String payload; serializeJson(doc, payload);

  // 1) Publish the *new* discovery first
  mqtt.publish(cfgTopic.c_str(), payload.c_str(), true);
  LOGF("[HA] discovery published: %s\n", cfgTopic.c_str());

  // 2) Then remove the opposite component (if any)
  retireOppositeDiscovery(chs.area, chs.channel0, chs.type);
}

void publishHADiscoveryForArea(uint8_t area) {
  // Publish temp sensor + setpoint number + save preset button + preset select
  publishHADiscovery_TempSensor(area);
  publishHADiscovery_SetpointNumber(area);
  // Save preset button
  {
    String sid = mqttSafeId(deviceId);
    String objId = String("a") + String(area) + "_save_preset";
    String discTopic = String(HA_DISCOVERY_PREFIX) + "/button/" + sid + "/" + objId + "/config";
    DynamicJsonDocument doc(512);
    doc["name"]               = String("Area ") + area + " Save Preset";
    doc["unique_id"]          = sid + "_" + objId;
    doc["command_topic"]      = areaBaseTopic(area) + "/save_preset";
    doc["payload_press"]      = "PRESS";
    doc["availability_topic"] = availabilityTopic();
    addHadeviceBlockForArea(doc, area);
    String payload; serializeJson(doc, payload);
    mqtt.publish(discTopic.c_str(), payload.c_str(), true);
  }
  publishHADiscovery_PresetSelect(area);
}

// ---- State Publishers -------------------------------------------
void publishStateForChannel(int idx) {
  using namespace DynetEntities;
  if (idx < 0 || idx >= em.channelsCount()) return;
  const ChannelState& chs = em.channelAt(idx);
  String base = channelBaseTopic(chs.area, chs.channel0);

  if (chs.type == SWITCH_ONOFF) {
    const char* st = chs.isOn ? "ON" : "OFF";
    mqtt.publish((base + "/state").c_str(), st, false);
  } else {
    DynamicJsonDocument doc(128);
    doc["state"] = (chs.isOn ? "ON" : "OFF");
    if (chs.type == LIGHT_DIMMABLE) {
      int bri = (int)((chs.levelPct * 255 + 50) / 100);
      doc["brightness"] = bri;
    }
    String payload; serializeJson(doc, payload);
    mqtt.publish((base + "/state").c_str(), payload.c_str(), false);
  }
}

void publishPresetForArea(uint8_t area) {
  using namespace DynetEntities;
  int ai = em.findArea(area);
  if (ai < 0) return;
  const auto& as = em.areaAt(ai);

  String topic = areaBaseTopic(area) + "/preset";
  if (as.preset0 != 0xFF) {
    String s = String((int)as.preset0 + 1); // 1-based
    mqtt.publish(topic.c_str(), s.c_str(), true);
  } else {
    mqtt.publish(topic.c_str(), "unknown", true);
  }
}

void publishSensorsForArea(uint8_t area) {
  using namespace DynetEntities;
  int ai = em.findArea(area);
  if (ai < 0) return;
  const AreaState& as = em.areaAt(ai);

  String aBase = areaBaseTopic(area);

  if (as.hasTemp && !isnan(as.tempC)) {
    String t = String(as.tempC, 2);
    mqtt.publish((aBase + "/temperature").c_str(), t.c_str(), true);
  }
  if (as.hasSetpt && !isnan(as.setptC)) {
    String s = String(as.setptC, 2);
    mqtt.publish((aBase + "/setpoint").c_str(), s.c_str(), true);
  }
}

// ---- MQTT Command Handling --------------------------------------
static bool startsWith(const String& s, const String& p) { return s.startsWith(p); }

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg; msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();

  String top = String(topic);
  String sid = mqttSafeId(deviceId);
  String base = String(BASE_TOPIC_PREFIX) + "/" + sid + "/";

  if (!startsWith(top, base)) return;

  // Patterns:
  // dynet/<sid>/area/<A>/ch/<C>/set
  // dynet/<sid>/area/<A>/ch/<C>/brightness/set
  // dynet/<sid>/area/<A>/ch/<C>/level/set
  // dynet/<sid>/area/<A>/ch/<C>/request
  // dynet/<sid>/area/<A>/save_preset
  // dynet/<sid>/area/<A>/preset/set
  // dynet/<sid>/area/<A>/reqpreset
  // dynet/<sid>/area/<A>/reqlevels
  // dynet/<sid>/area/<A>/setpoint/set

  String rest = top.substring(base.length()); // e.g., "area/1/ch/2/set"
  int p0 = rest.indexOf('/');
  if (p0 < 0) return;
  String pA = rest.substring(0, p0);
  if (pA != "area") return;

  String rest2 = rest.substring(p0 + 1);       // "1/ch/2/set" or "1/save_preset"
  int p1 = rest2.indexOf('/');
  if (p1 < 0) return;

  int area = rest2.substring(0, p1).toInt();
  if (area <= 0 || area > 255) return;

  String afterArea = rest2.substring(p1 + 1);  // e.g. "ch/2/set" or "save_preset"

  // --- Area-level commands (no channel) ---

  // Save current preset (program)
  if (afterArea == "save_preset") {
    if (msg == "PRESS") dynet.sendProgramCurrentPreset((uint8_t)area);
    return;
  }

  // Area setpoint (HVAC)
  if (afterArea == "setpoint/set") {
    float tempC = msg.toFloat();
    LOGF("[MQTT] Setpoint cmd: area=%d -> %.2f°C\n", area, tempC);
    dynet.sendSetTempSetpoint_q025((uint8_t)area, tempC);

    // optional optimistic update so HA mirrors immediately
    {
      using namespace DynetEntities;
      int16_t q = (int16_t)roundf(tempC * 4.0f);
      em.noteSetpoint_q025((uint8_t)area, q);
    }
    return;
  }

  // Select preset for Area (HA sends 1..16; DyNet uses 0-origin internally on wire)
  if (afterArea == "preset/set") {
    int preset = msg.toInt();
    if (preset >= 1 && preset <= 16) {
      dynet.sendAreaPreset((uint8_t)area, (uint8_t)preset, 0);
      using namespace DynetEntities;
      // Request actual preset + levels
      delay(120);
      dynet.sendRequestPreset((uint8_t)area);              // 0x63
      delay(120);
      em.requestLevelsForArea(area, DYNET_MAX_CHANNELS);  // 0x61 per channel

      // Optional optimistic UI update (can keep it)
      em.noteReportPreset((uint8_t)area, (uint8_t)(preset - 1));
      publishPresetForArea((uint8_t)area);
    }
    return;
  }

  // Request current preset (0x63 -> expect 0x62 reply)
  if (afterArea == "reqpreset") {
    dynet.sendRequestPreset((uint8_t)area);
    return;
  }

  // Request ALL channel levels in the area
  if (afterArea == "reqlevels") {
    const uint8_t COUNT =
    #ifdef DYNET_MAX_CHANNELS
      DYNET_MAX_CHANNELS
    #else
      48
    #endif
    ;
    for (uint8_t ch = 0; ch < COUNT; ++ch) {
      dynet.sendRequestChannelLevel((uint8_t)area, ch);
      delay(2);
    }
    return;
  }

  // --- Channel-level commands ---
  // Expect "ch/<C>/<something>"
  int p2 = afterArea.indexOf('/');
  if (p2 < 0) return;
  String kw = afterArea.substring(0, p2);
  String afterCh = afterArea.substring(p2 + 1);
  if (kw != "ch") return;

  int p3 = afterCh.indexOf('/');
  if (p3 < 0) return;

  int ch = afterCh.substring(0, p3).toInt();
  if (ch < 0 || ch > 255) return;

  String action = afterCh.substring(p3 + 1);   // "set", "brightness/set", "level/set", "request"

  using namespace DynetEntities;
  int idx = em.touchChannel((uint8_t)area, (uint8_t)ch);
  if (idx < 0) return;
  ChannelState cs = em.channelAt(idx); // copy (for type)

  // 1) Main "set" action
  if (action == "set") {
    if (cs.type == SWITCH_ONOFF || cs.type == LIGHT_ONOFF) {
      bool on = false;
      bool parsed = false;

      if (msg.length() && msg[0] == '{') {
        DynamicJsonDocument d(192);
        if (deserializeJson(d, msg) == DeserializationError::Ok) {
          const char* st = d["state"] | "";
          if (*st) { on = (strcasecmp(st, "ON") == 0); parsed = true; }
        }
      }
      if (!parsed) {
        on = (msg.equalsIgnoreCase("ON") || msg == "1" || msg.equalsIgnoreCase("true"));
      }

      uint8_t pct = on ? 100 : 0;
      dynet.sendFadeToLevel_1s((uint8_t)area, (uint8_t)ch, pct, 0x02);
      em.setChannelLevel((uint8_t)area, (uint8_t)ch, pct);
      publishStateForChannel(idx);
      delay(180);
      dynet.sendRequestChannelLevel((uint8_t)area, (uint8_t)ch);
      return;
    } else {
      // Dimmable Light: JSON schema or simple forms
      DynamicJsonDocument d(256);
      DeserializationError err = deserializeJson(d, msg);
      if (err) {
        // Simple: "ON"/"OFF"/"50"
        if (msg.equalsIgnoreCase("ON"))  {
          dynet.sendFadeToLevel_1s((uint8_t)area, (uint8_t)ch, 100, 0x02);
          em.setChannelLevel((uint8_t)area, (uint8_t)ch, 100);
          publishStateForChannel(idx);
          delay(180);
          dynet.sendRequestChannelLevel((uint8_t)area, (uint8_t)ch);
          return;
        }
        if (msg.equalsIgnoreCase("OFF")) {
          dynet.sendFadeToLevel_1s((uint8_t)area, (uint8_t)ch,   0, 0x02);
          em.setChannelLevel((uint8_t)area, (uint8_t)ch, 0);
          publishStateForChannel(idx);
          delay(180);
          dynet.sendRequestChannelLevel((uint8_t)area, (uint8_t)ch);
          return;
        }
        int pct = constrain(msg.toInt(), 0, 100);
        dynet.sendFadeToLevel_1s((uint8_t)area, (uint8_t)ch, pct, 0x02);
        em.setChannelLevel((uint8_t)area, (uint8_t)ch, pct);
        publishStateForChannel(idx);
        delay(180);
        dynet.sendRequestChannelLevel((uint8_t)area, (uint8_t)ch);
        return;
      }

      const char* st = d["state"] | "";
      bool haveBri = d.containsKey("brightness");
      int bri = haveBri ? (int)d["brightness"] : -1;

      if (st && *st) {
        if (!strcasecmp(st, "OFF")) {
          dynet.sendFadeToLevel_1s((uint8_t)area, (uint8_t)ch, 0, 0x02);
          em.setChannelLevel((uint8_t)area, (uint8_t)ch, 0);
          publishStateForChannel(idx);
          delay(180);
          dynet.sendRequestChannelLevel((uint8_t)area, (uint8_t)ch);
          return;
        } else if (!strcasecmp(st, "ON")) {
          if (haveBri && bri >= 0) {
            int pct = constrain((bri * 100 + 127) / 255, 0, 100);
            dynet.sendFadeToLevel_1s((uint8_t)area, (uint8_t)ch, pct, 0x02);
            em.setChannelLevel((uint8_t)area, (uint8_t)ch, pct);
            publishStateForChannel(idx);
            delay(180);
            dynet.sendRequestChannelLevel((uint8_t)area, (uint8_t)ch);
            return;
          } else {
            dynet.sendFadeToLevel_1s((uint8_t)area, (uint8_t)ch, 100, 0x02);
            em.setChannelLevel((uint8_t)area, (uint8_t)ch, 100);
            publishStateForChannel(idx);
            delay(180);
            dynet.sendRequestChannelLevel((uint8_t)area, (uint8_t)ch);
            return;
          }
        }
      }

      if (haveBri && bri >= 0) {
        int pct = constrain((bri * 100 + 127) / 255, 0, 100);
        dynet.sendFadeToLevel_1s((uint8_t)area, (uint8_t)ch, pct, 0x02);
        em.setChannelLevel((uint8_t)area, (uint8_t)ch, pct);
        publishStateForChannel(idx);
        return;
      }
    }
  }

  // 2) explicit brightness topic (0..255 or 0..100)
  if (action == "brightness/set") {
    int val = msg.toInt();
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    int pct = (val > 100) ? ((val * 100 + 127) / 255) : val;
    pct = constrain(pct, 0, 100);
    dynet.sendFadeToLevel_1s((uint8_t)area, (uint8_t)ch, pct, 0x02);

    em.setChannelLevel((uint8_t)area, (uint8_t)ch, pct);
    publishStateForChannel(idx);
    delay(180);
    dynet.sendRequestChannelLevel((uint8_t)area, (uint8_t)ch);
    return;
  }

  // 3) explicit level topic (0..100)
  if (action == "level/set") {
    int pct = constrain(msg.toInt(), 0, 100);
    dynet.sendFadeToLevel_1s((uint8_t)area, (uint8_t)ch, pct, 0x02);

    em.setChannelLevel((uint8_t)area, (uint8_t)ch, pct);
    publishStateForChannel(idx);
    delay(180);
    dynet.sendRequestChannelLevel((uint8_t)area, (uint8_t)ch);
    return;
  }

  // 4) request one channel’s level (0x61) — no optimistic publish here
  if (action == "request") {
    dynet.sendRequestChannelLevel((uint8_t)area, (uint8_t)ch);
    return;
  }

  // (future: toggle/step_up/step_down)
}


// ---- Connection / Loop ------------------------------------------
static void subscribeAll() {
  String sid = mqttSafeId(deviceId);
  String base = String(BASE_TOPIC_PREFIX) + "/" + sid + "/";

  mqtt.subscribe((base + "area/+/ch/+/set").c_str());
  mqtt.subscribe((base + "area/+/ch/+/brightness/set").c_str());
  mqtt.subscribe((base + "area/+/ch/+/level/set").c_str());
  mqtt.subscribe((base + "area/+/ch/+/request").c_str());
  mqtt.subscribe((base + "area/+/save_preset").c_str());
  mqtt.subscribe((base + "area/+/preset/set").c_str());
  mqtt.subscribe((base + "area/+/reqpreset").c_str());
  mqtt.subscribe((base + "area/+/reqlevels").c_str());
  mqtt.subscribe((base + "area/+/setpoint/set").c_str());
}

void mqttEnsureConnected() {
  if (String(cfg.mqtt_server).length() == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) return;

  mqtt.setServer(cfg.mqtt_server, cfg.mqtt_port);
  mqtt.setBufferSize(768);
  mqtt.setCallback(mqttCallback);

  String willTopic = availabilityTopic();
  String cid = "DyNetESP-" + mqttSafeId(deviceId) + "-" + CHIP_ID_STR;

  bool ok;
  if (strlen(cfg.mqtt_user) > 0)
    ok = mqtt.connect(cid.c_str(), cfg.mqtt_user, cfg.mqtt_pass, willTopic.c_str(), 0, true, HA_OFFLINE);
  else
    ok = mqtt.connect(cid.c_str(), willTopic.c_str(), 0, true, HA_OFFLINE);

  if (ok) {
    publishAvailability(HA_ONLINE);
    subscribeAll();
    // trigger a paced HA discovery sweep
    if (cfg.ha_discovery) {
      rediscoveryScheduled = true;
      rediscoveryPtr = 0;                       // channel index
      nextRediscoveryAt = millis() + 300;
    } else {
      rediscoveryScheduled = false;
    }
  }
}

void mqttSetup() { /* nothing extra */ }

void mqttLoop() {
  if (String(cfg.mqtt_server).length() > 0) {
    if (!mqtt.connected()) {
      if (WiFi.status() == WL_CONNECTED && millis() - lastMqttAttempt >= MQTT_RETRY_MS) {
        lastMqttAttempt = millis();
        mqttEnsureConnected();
      }
    } else {
      mqtt.loop();

      // paced HA discovery over channels + areas
      if (cfg.ha_discovery && rediscoveryScheduled) {
        if (millis() >= nextRediscoveryAt) {
          using namespace DynetEntities;

          Serial.printf("[HA] rediscovery: ch=%u/%u areas=%u\n",
              rediscoveryPtr,
              DynetEntities::em.channelsCount(),
              DynetEntities::em.areasCount());

          // publish one channel per tick
          if (rediscoveryPtr < (uint16_t)em.channelsCount()) {
            publishHADiscoveryForChannel(rediscoveryPtr);
            rediscoveryPtr++;
            nextRediscoveryAt = millis() + 120;
          } else if (rediscoveryPtr == (uint16_t)em.channelsCount()) {
            // Publish areas once, then advance ptr past this gate so we don't repeat.
            for (int i = 0; i < em.areasCount(); i++) {
              publishHADiscoveryForArea(em.areaAt(i).area);
            }
            rediscoveryPtr++;           // move past the "areas" gate
            nextRediscoveryAt = millis() + 200;
          } else {
            rediscoveryScheduled = false;
          }
          delay(0); // yield
        }
      }
    }
  }
}
