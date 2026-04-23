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
  // Use the user-set area name if available, otherwise fall back to "Area N"
  {
    using namespace DynetEntities;
    int ai = em.findArea(area);
    dev["name"] = (ai >= 0 && em.areaAt(ai).name[0])
                  ? String(em.areaAt(ai).name)
                  : (String("Area ") + area);
  }
  dev["sw_version"]   = HA_SW_VERSION;
  dev["hw_version"]   = String("Dynalite Area ") + area;   // permanent area# reference
  dev["via_device"]   = sid + "_gateway";   // links to the ESP gateway device

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
String gatewayBaseTopic() {
  String sid = mqttSafeId(deviceId);
  return String(BASE_TOPIC_PREFIX) + "/" + sid + "/gateway";
}

// HA device block for the gateway ESP itself (shared by all gateway entities)
static void addHaDeviceBlock_Gateway(JsonDocument& doc) {
  String sid = mqttSafeId(deviceId);
  JsonObject dev = doc.createNestedObject("device");
  JsonArray ids = dev.createNestedArray("identifiers");
  ids.add(sid + "_gateway");
  dev["name"]         = deviceId.length() ? deviceId : "DyNet Gateway";
  dev["manufacturer"] = HA_MANUFACTURER;
  dev["model"]        = HA_MODEL;
  dev["sw_version"]   = HA_SW_VERSION;
  IPAddress ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP() : WiFi.softAPIP();
  dev["configuration_url"] = String("http://") + ip.toString() + "/";
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

// Returns user-defined area name if set, otherwise "Area N"
static String areaDisplayName(uint8_t area) {
  using namespace DynetEntities;
  int ai = em.findArea(area);
  if (ai >= 0 && em.areaAt(ai).name[0]) return String(em.areaAt(ai).name);
  return String("Area ") + String(area);
}

// Temperature sensor
static void publishHADiscovery_TempSensor(uint8_t area) {
  String sid = mqttSafeId(deviceId);
  String objId = String("a") + String(area) + "_temp";
  String discTopic = String(HA_DISCOVERY_PREFIX) + "/sensor/" + sid + "/" + objId + "/config";
  DynamicJsonDocument doc(512);
  doc["name"]               = areaDisplayName(area) + " Temperature";
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
  doc["name"]               = areaDisplayName(area) + " Setpoint";
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

// --- HVAC climate entity ---
static void publishHADiscovery_Climate(uint8_t area) {
  using namespace DynetEntities;
  int ai = em.findArea(area);
  if (ai < 0 || !em.areaAt(ai).hvac) return;
  const HvacConfig& h = *em.areaAt(ai).hvac;

  String sid    = mqttSafeId(deviceId);
  String objId  = String("a") + area + "_climate";
  String discTopic = String(HA_DISCOVERY_PREFIX) + "/climate/" + sid + "/" + objId + "/config";
  String aBase  = areaBaseTopic(area);

  DynamicJsonDocument doc(1536);
  addHadeviceBlockForArea(doc, area);
  doc["unique_id"]          = sid + "_" + objId;
  doc["name"]               = areaDisplayName(area);
  doc["availability_topic"] = availabilityTopic();

  // Temperature
  doc["current_temperature_topic"] = aBase + "/temperature";
  doc["temperature_state_topic"]   = aBase + "/setpoint";
  doc["temperature_command_topic"] = aBase + "/setpoint/set";
  doc["temperature_unit"]          = "C";
  doc["min_temp"] = 10;
  doc["max_temp"] = 35;
  doc["temp_step"] = 0.5;

  // Modes
  doc["mode_state_topic"]   = aBase + "/hvac/mode";
  doc["mode_command_topic"] = aBase + "/hvac/mode/set";
  JsonArray modes = doc.createNestedArray("modes");
  for (uint8_t i = 0; i < MAX_HVAC_MODES; i++) {
    if (h.modes[i].used && h.modes[i].name[0]) modes.add(String(h.modes[i].name));
  }
  if (modes.size() == 0) { modes.add("off"); modes.add("cool"); }  // safe defaults

  // Fan modes (only if configured)
  if (h.fanCount > 0) {
    doc["fan_mode_state_topic"]   = aBase + "/hvac/fan";
    doc["fan_mode_command_topic"] = aBase + "/hvac/fan/set";
    JsonArray fans = doc.createNestedArray("fan_modes");
    for (uint8_t i = 0; i < MAX_HVAC_FANMODES; i++) {
      if (h.fanModes[i].used && h.fanModes[i].name[0]) fans.add(String(h.fanModes[i].name));
    }
  }

  String payload; serializeJson(doc, payload);
  mqtt.publish(discTopic.c_str(), payload.c_str(), true);
  LOGF("[HA] climate discovery published: A%u\n", area);
}

// --- Preset "select" entity (HA MQTT Select) ---
static void publishHADiscovery_PresetSelect(uint8_t area) {
  String sid = mqttSafeId(deviceId);
  String objId = String("a") + String(area) + "_preset";
  String discTopic = String(HA_DISCOVERY_PREFIX) + "/select/" + sid + "/" + objId + "/config";

  DynamicJsonDocument doc(1536);  // 128 options × ~5 bytes + doc overhead
  doc["name"]               = areaDisplayName(area) + " Preset";
  doc["unique_id"]          = sid + "_" + objId;
  doc["command_topic"]      = areaBaseTopic(area) + "/preset/set";
  doc["state_topic"]        = areaBaseTopic(area) + "/preset";
  doc["availability_topic"] = availabilityTopic();

  JsonArray opts = doc.createNestedArray("options");
  // Use per-area preset count; fall back to global cfg if not set
  int ai_ps = DynetEntities::em.findArea(area);
  uint8_t pCount = (ai_ps >= 0 && DynetEntities::em.areaAt(ai_ps).presetCount)
                   ? DynetEntities::em.areaAt(ai_ps).presetCount
                   : (cfg.ha_preset_count ? cfg.ha_preset_count : 4);
  if (pCount > 128) pCount = 128;
  for (int p = 1; p <= pCount; ++p) opts.add(String(p));

  addHadeviceBlockForArea(doc, area);
  String payload; serializeJson(doc, payload);
  mqtt.publish(discTopic.c_str(), payload.c_str(), true);
}

static void publishHADiscovery_Cover(uint8_t area, uint8_t ch0); // forward declaration

void publishHADiscoveryForChannel(int idx) {

  using namespace DynetEntities;
  if (idx < 0 || idx >= em.channelsCount()) return;
  const ChannelState& chs = em.channelAt(idx);

  // Slave channels have no independent HA entity
  if (chs.isCurtainSlave) return;

  // Curtain master → cover entity
  if (chs.type == CURTAIN) {
    publishHADiscovery_Cover(chs.area, chs.channel0);
    return;
  }

  // --- build topics/JSON for current type ---
  String sid   = mqttSafeId(deviceId);
  String objId = String("a") + String(chs.area) + "_c" + String(chs.channel0);
  String comp  = (chs.type == SWITCH_ONOFF) ? "switch" : "light";
  String cfgTopic = String(HA_DISCOVERY_PREFIX) + "/" + comp + "/" + sid + "/" + objId + "/config";
  LOGF("[HA] preparing discovery for A%u C%u as %s\n", chs.area, chs.channel0, (chs.type==SWITCH_ONOFF?"switch":"light"));
  DynamicJsonDocument doc(768);                    // 768 to avoid silent truncation of device block
  addHadeviceBlockForArea(doc, chs.area);          // your existing helper
  doc["unique_id"] = sid + "_" + objId;
  doc["name"]      = (chs.name[0])
                   ? String(chs.name)
                   : (String("Area ") + chs.area + " Ch " + (chs.channel0 + 1));
  String base      = channelBaseTopic(chs.area, chs.channel0);
  doc["state_topic"]   = base + "/state";
  doc["availability_topic"] = availabilityTopic();

  if (chs.type == SWITCH_ONOFF) {
    // SWITCH -> plain payloads
    doc["command_topic"] = base + "/set";
    doc["payload_on"]    = "ON";
    doc["payload_off"]   = "OFF";
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

  // 2) Publish a retained initial state so HA immediately shows the entity
  //    (without this, light+JSON-schema entities stay invisible until first real state arrives)
  if (chs.type == SWITCH_ONOFF) {
    mqtt.publish((base + "/state").c_str(), chs.isOn ? "ON" : "OFF", true);
  } else {
    int bri = (int)((chs.levelPct * 255 + 50) / 100);
    String st = String("{\"state\":\"") + (chs.isOn ? "ON" : "OFF") +
                "\",\"brightness\":" + bri + "}";
    mqtt.publish((base + "/state").c_str(), st.c_str(), true);
  }

  // 3) Then remove the opposite component (if any)
  retireOppositeDiscovery(chs.area, chs.channel0, chs.type);
}

// ---- HA Cover for a single named curtain entry inside an AREA_CURTAIN area ---
void publishHADiscoveryForAreaCurtainEntry(uint8_t area, uint8_t idx) {
  using namespace DynetEntities;
  int ai = em.findArea(area);
  if (ai < 0) return;
  if (!em.areaAt(ai).curtains) return;
  const AreaCurtainEntry& e = em.areaAt(ai).curtains[idx];
  if (!e.used) return;

  String sid    = mqttSafeId(deviceId);
  String objId  = String("a") + area + "_ct" + idx;
  String discTopic = String(HA_DISCOVERY_PREFIX) + "/cover/" + sid + "/" + objId + "/config";
  String cmdTopic  = areaBaseTopic(area) + "/curtain/" + idx + "/cover/set";

  String entryName = (e.name[0]) ? String(e.name)
                                  : (areaDisplayName(area) + " Curtain " + (idx + 1));

  DynamicJsonDocument doc(640);
  addHadeviceBlockForArea(doc, area);
  doc["name"]               = entryName;
  doc["unique_id"]          = sid + "_" + objId;
  doc["command_topic"]      = cmdTopic;
  doc["payload_open"]       = "OPEN";
  doc["payload_close"]      = "CLOSE";
  doc["payload_stop"]       = "STOP";
  doc["optimistic"]         = true;
  doc["device_class"]       = "curtain";
  doc["availability_topic"] = availabilityTopic();

  String payload; serializeJson(doc, payload);
  mqtt.publish(discTopic.c_str(), payload.c_str(), true);
  LOGF("[HA] area curtain entry published: A%u ct%u\n", area, idx);
}

void removeHADiscoveryForAreaCurtainEntry(uint8_t area, uint8_t idx) {
  if (!mqtt.connected()) return;
  String sid   = mqttSafeId(deviceId);
  String objId = String("a") + area + "_ct" + idx;
  String t     = String(HA_DISCOVERY_PREFIX) + "/cover/" + sid + "/" + objId + "/config";
  mqtt.publish(t.c_str(), "", true);
  LOGF("[HA] removed area curtain entry A%u ct%u\n", area, idx);
}

void publishHADiscoveryForArea(uint8_t area) {
  using namespace DynetEntities;
  int ai = em.findArea(area);

  // Area Curtain type: publish one cover entity per curtain entry
  if (ai >= 0 && em.areaAt(ai).areaType == AREA_CURTAIN) {
    if (em.areaAt(ai).curtains) {
      for (uint8_t i = 0; i < MAX_CURTAINS_PER_AREA; i++) {
        if (em.areaAt(ai).curtains[i].used) {
          publishHADiscoveryForAreaCurtainEntry(area, i);
        }
      }
    }
    return;
  }

  // HVAC area: climate entity only (temp/setpoint handled inside climate)
  if (ai >= 0 && em.areaAt(ai).areaType == AREA_HVAC) {
    publishHADiscovery_Climate(area);
    return;
  }

  // Default Lights area: save preset button + preset select
  // (temp sensor is published on-demand when first temp opcode arrives)
  {
    String sid = mqttSafeId(deviceId);
    String objId = String("a") + String(area) + "_save_preset";
    String discTopic = String(HA_DISCOVERY_PREFIX) + "/button/" + sid + "/" + objId + "/config";
    DynamicJsonDocument doc(512);
    doc["name"]               = areaDisplayName(area) + " Save Preset";
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

// ---- HA Cover (curtain) discovery -----------------------------------
static void publishHADiscovery_Cover(uint8_t area, uint8_t ch0) {
  using namespace DynetEntities;
  String sid    = mqttSafeId(deviceId);
  String objId  = String("a") + String(area) + "_c" + String(ch0);
  String discTopic = String(HA_DISCOVERY_PREFIX) + "/cover/" + sid + "/" + objId + "/config";
  String base   = channelBaseTopic(area, ch0);

  // Get display name from channel
  String name;
  int ci = em.findChannel(area, ch0);
  if (ci >= 0 && em.channelAt(ci).name[0])
    name = String(em.channelAt(ci).name);
  else
    name = String("Area ") + area + " Ch " + (ch0 + 1);

  DynamicJsonDocument doc(640);
  addHadeviceBlockForArea(doc, area);
  doc["name"]               = name;
  doc["unique_id"]          = sid + "_" + objId;
  doc["command_topic"]      = base + "/cover/set";
  doc["payload_open"]       = "OPEN";
  doc["payload_close"]      = "CLOSE";
  doc["payload_stop"]       = "STOP";
  doc["optimistic"]         = true;
  doc["device_class"]       = "curtain";
  doc["availability_topic"] = availabilityTopic();

  String payload; serializeJson(doc, payload);
  mqtt.publish(discTopic.c_str(), payload.c_str(), true);
  LOGF("[HA] cover discovery published: A%u Ch%u\n", area, ch0);
}

// ---- HA Discovery removal ---------------------------------------
// Publish empty retained payloads to the discovery topics to remove
// entities from Home Assistant when a channel or area is manually deleted.

void removeHADiscoveryForChannel(uint8_t area, uint8_t ch0, DynetEntities::EntityType type) {
  if (!mqtt.connected()) return;
  String sid   = mqttSafeId(deviceId);
  String objId = String("a") + String(area) + "_c" + String(ch0);
  // Always wipe all three possible component types so nothing is left stale
  for (const char* comp : {"light", "switch", "cover"}) {
    String t = String(HA_DISCOVERY_PREFIX) + "/" + comp + "/" + sid + "/" + objId + "/config";
    mqtt.publish(t.c_str(), "", true);
  }
  LOGF("[HA] removed discovery for A%u C%u\n", area, ch0);
}

void removeHADiscoveryForArea(uint8_t area) {
  if (!mqtt.connected()) return;
  String sid = mqttSafeId(deviceId);
  // sensor (temperature)
  mqtt.publish((String(HA_DISCOVERY_PREFIX) + "/sensor/"  + sid + "/a" + area + "_temp/config").c_str(),        "", true);
  // number (setpoint)
  mqtt.publish((String(HA_DISCOVERY_PREFIX) + "/number/"  + sid + "/a" + area + "_setpoint/config").c_str(),    "", true);
  // button (save preset)
  mqtt.publish((String(HA_DISCOVERY_PREFIX) + "/button/"  + sid + "/a" + area + "_save_preset/config").c_str(), "", true);
  // select (preset)
  mqtt.publish((String(HA_DISCOVERY_PREFIX) + "/select/"  + sid + "/a" + area + "_preset/config").c_str(),      "", true);
  // cover entries (area curtain — wipe all per-curtain slots)
  for (uint8_t i = 0; i < DynetEntities::MAX_CURTAINS_PER_AREA; i++) {
    String objId = String("a") + area + "_ct" + i;
    mqtt.publish((String(HA_DISCOVERY_PREFIX) + "/cover/" + sid + "/" + objId + "/config").c_str(), "", true);
  }
  // also wipe the legacy area-level cover entity (a<N>_cover) so stale retained
  // messages from older firmware versions are cleared from the broker
  mqtt.publish((String(HA_DISCOVERY_PREFIX) + "/cover/" + sid + "/a" + area + "_cover/config").c_str(), "", true);
  // climate entity (HVAC)
  mqtt.publish((String(HA_DISCOVERY_PREFIX) + "/climate/" + sid + "/a" + area + "_climate/config").c_str(), "", true);
  LOGF("[HA] removed discovery for Area %u\n", area);
}

// ---- Gateway Device HA Discovery --------------------------------
void publishHADiscovery_Gateway() {
  if (!mqtt.connected()) return;
  String sid  = mqttSafeId(deviceId);
  String base = gatewayBaseTopic();
  String avail = availabilityTopic();

  // Helper: publish one entity config
  auto pub = [&](const char* comp, const char* objSuffix, JsonDocument& doc) {
    String topic = String(HA_DISCOVERY_PREFIX) + "/" + comp + "/" + sid + "/" + objSuffix + "/config";
    String payload; serializeJson(doc, payload);
    mqtt.publish(topic.c_str(), payload.c_str(), true);
  };

  // 1. IP address sensor
  {
    DynamicJsonDocument doc(512);
    addHaDeviceBlock_Gateway(doc);
    doc["name"]               = "Gateway IP";
    doc["unique_id"]          = sid + "_gw_ip";
    doc["state_topic"]        = base + "/state";
    doc["value_template"]     = "{{ value_json.ip }}";
    doc["icon"]               = "mdi:ip-network";
    doc["entity_category"]    = "diagnostic";
    doc["availability_topic"] = avail;
    pub("sensor", "gw_ip", doc);
  }
  // 2. RSSI sensor
  {
    DynamicJsonDocument doc(512);
    addHaDeviceBlock_Gateway(doc);
    doc["name"]               = "Gateway RSSI";
    doc["unique_id"]          = sid + "_gw_rssi";
    doc["state_topic"]        = base + "/state";
    doc["value_template"]     = "{{ value_json.rssi }}";
    doc["unit_of_measurement"] = "dBm";
    doc["device_class"]       = "signal_strength";
    doc["entity_category"]    = "diagnostic";
    doc["availability_topic"] = avail;
    pub("sensor", "gw_rssi", doc);
  }
  // 3. Free heap sensor
  {
    DynamicJsonDocument doc(512);
    addHaDeviceBlock_Gateway(doc);
    doc["name"]               = "Gateway Free Heap";
    doc["unique_id"]          = sid + "_gw_heap";
    doc["state_topic"]        = base + "/state";
    doc["value_template"]     = "{{ value_json.heap }}";
    doc["unit_of_measurement"] = "B";
    doc["icon"]               = "mdi:memory";
    doc["entity_category"]    = "diagnostic";
    doc["availability_topic"] = avail;
    pub("sensor", "gw_heap", doc);
  }
  // 4. Uptime sensor
  {
    DynamicJsonDocument doc(512);
    addHaDeviceBlock_Gateway(doc);
    doc["name"]               = "Gateway Uptime";
    doc["unique_id"]          = sid + "_gw_uptime";
    doc["state_topic"]        = base + "/state";
    doc["value_template"]     = "{{ value_json.uptime }}";
    doc["unit_of_measurement"] = "s";
    doc["icon"]               = "mdi:clock-outline";
    doc["entity_category"]    = "diagnostic";
    doc["availability_topic"] = avail;
    pub("sensor", "gw_uptime", doc);
  }
  // 5. Reboot button
  {
    DynamicJsonDocument doc(512);
    addHaDeviceBlock_Gateway(doc);
    doc["name"]               = "Gateway Reboot";
    doc["unique_id"]          = sid + "_gw_reboot";
    doc["command_topic"]      = base + "/reboot";
    doc["payload_press"]      = "PRESS";
    doc["icon"]               = "mdi:restart";
    doc["entity_category"]    = "config";
    doc["availability_topic"] = avail;
    pub("button", "gw_reboot", doc);
  }
  // 6. Scan Areas button
  {
    DynamicJsonDocument doc(512);
    addHaDeviceBlock_Gateway(doc);
    doc["name"]               = "Gateway Scan Areas";
    doc["unique_id"]          = sid + "_gw_scan";
    doc["command_topic"]      = base + "/scan";
    doc["payload_press"]      = "PRESS";
    doc["icon"]               = "mdi:magnify-scan";
    doc["entity_category"]    = "config";
    doc["availability_topic"] = avail;
    pub("button", "gw_scan", doc);
  }
  // 7. Re-publish HA Discovery button
  {
    DynamicJsonDocument doc(512);
    addHaDeviceBlock_Gateway(doc);
    doc["name"]               = "Gateway Re-publish Discovery";
    doc["unique_id"]          = sid + "_gw_rediscover";
    doc["command_topic"]      = base + "/rediscover";
    doc["payload_press"]      = "PRESS";
    doc["icon"]               = "mdi:home-search";
    doc["entity_category"]    = "config";
    doc["availability_topic"] = avail;
    pub("button", "gw_rediscover", doc);
  }

  LOGF("[HA] gateway device discovery published\n");
}

// Publish live diagnostic state for the gateway device (called periodically)
void publishGatewayState() {
  if (!mqtt.connected()) return;
  DynamicJsonDocument doc(256);
  IPAddress ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP() : WiFi.softAPIP();
  doc["ip"]     = ip.toString();
  doc["rssi"]   = (WiFi.status() == WL_CONNECTED) ? (int)WiFi.RSSI() : 0;
  doc["heap"]   = (uint32_t)ESP.getFreeHeap();
  doc["uptime"] = (uint32_t)(millis() / 1000UL);
  String payload; serializeJson(doc, payload);
  mqtt.publish((gatewayBaseTopic() + "/state").c_str(), payload.c_str(), false);
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
    // For non-HVAC areas auto-publish temp sensor discovery on first receipt
    if (as.areaType != AREA_HVAC) {
      publishHADiscovery_TempSensor(area);
    }
  }
  if (as.hasSetpt && !isnan(as.setptC)) {
    String s = String(as.setptC, 2);
    mqtt.publish((aBase + "/setpoint").c_str(), s.c_str(), true);
  }
  // HVAC: publish current mode and fan state so climate entity stays in sync
  if (as.areaType == AREA_HVAC && as.hvac) {
    const HvacConfig& h = *as.hvac;
    if (h.currentMode[0])
      mqtt.publish((aBase + "/hvac/mode").c_str(), h.currentMode, true);
    if (h.currentFanMode[0])
      mqtt.publish((aBase + "/hvac/fan").c_str(), h.currentFanMode, true);
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
  // dynet/<sid>/gateway/reboot|scan|rediscover
  // dynet/<sid>/area/<A>/ch/<C>/set  ...etc

  String rest = top.substring(base.length()); // e.g., "area/1/ch/2/set" or "gateway/reboot"
  int p0 = rest.indexOf('/');
  if (p0 < 0) return;
  String pA = rest.substring(0, p0);

  // --- Gateway device commands ---
  if (pA == "gateway") {
    String gwCmd = rest.substring(p0 + 1);
    if (gwCmd == "reboot") {
      LOGF("[MQTT] gateway reboot requested\n");
      publishAvailability(HA_OFFLINE);
      delay(200);
      ESP.restart();
    } else if (gwCmd == "scan") {
      LOGF("[MQTT] gateway scan requested\n");
      areasSweepActive  = true;
      areasSweepArea    = 2;
      areasSweepChannel = 0;
      areasSweepPass    = 2;
      areasSweepNextAt  = millis() + 50;
    } else if (gwCmd == "rediscover") {
      LOGF("[MQTT] gateway rediscover requested\n");
      rediscoveryScheduled = true;
      rediscoveryPtr       = 0;
      nextRediscoveryAt    = millis() + 200;
    }
    return;
  }

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
      dynet.sendRequestPreset((uint8_t)area);            // immediate — single write, no blocking
      dynet.scheduleAreaLevelReqs((uint8_t)area, 500);  // non-blocking level refresh
      // Optimistic UI update
      using namespace DynetEntities;
      em.noteReportPreset((uint8_t)area, (uint8_t)(preset - 1));
      publishPresetForArea((uint8_t)area);
    }
    return;
  }

  // HVAC mode command: area/<N>/hvac/mode/set
  if (afterArea == "hvac/mode/set") {
    using namespace DynetEntities;
    em.commandHvacMode((uint8_t)area, msg.c_str());
    return;
  }

  // HVAC fan mode command: area/<N>/hvac/fan/set
  if (afterArea == "hvac/fan/set") {
    using namespace DynetEntities;
    em.commandHvacFanMode((uint8_t)area, msg.c_str());
    return;
  }

  // Area curtain cover command: area/<N>/curtain/<idx>/cover/set
  if (afterArea.startsWith("curtain/")) {
    String curtainRest = afterArea.substring(8);  // "<idx>/cover/set"
    int p = curtainRest.indexOf('/');
    if (p >= 0 && curtainRest.substring(p + 1) == "cover/set") {
      uint8_t curtainIdx = (uint8_t)curtainRest.substring(0, p).toInt();
      using namespace DynetEntities;
      em.commandAreaCurtain((uint8_t)area, curtainIdx, msg.c_str());
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
      dynet.scheduleLevelReq((uint8_t)area, ch, 200 + (uint32_t)ch * 80);
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
      dynet.scheduleLevelReq((uint8_t)area, (uint8_t)ch, 400);
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
          dynet.scheduleLevelReq((uint8_t)area, (uint8_t)ch, 400);
          return;
        }
        if (msg.equalsIgnoreCase("OFF")) {
          dynet.sendFadeToLevel_1s((uint8_t)area, (uint8_t)ch,   0, 0x02);
          em.setChannelLevel((uint8_t)area, (uint8_t)ch, 0);
          publishStateForChannel(idx);
          dynet.scheduleLevelReq((uint8_t)area, (uint8_t)ch, 400);
          return;
        }
        int pct = constrain(msg.toInt(), 0, 100);
        dynet.sendFadeToLevel_1s((uint8_t)area, (uint8_t)ch, pct, 0x02);
        em.setChannelLevel((uint8_t)area, (uint8_t)ch, pct);
        publishStateForChannel(idx);
        dynet.scheduleLevelReq((uint8_t)area, (uint8_t)ch, 400);
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
          dynet.scheduleLevelReq((uint8_t)area, (uint8_t)ch, 400);
          return;
        } else if (!strcasecmp(st, "ON")) {
          if (haveBri && bri >= 0) {
            int pct = constrain((bri * 100 + 127) / 255, 0, 100);
            dynet.sendFadeToLevel_1s((uint8_t)area, (uint8_t)ch, pct, 0x02);
            em.setChannelLevel((uint8_t)area, (uint8_t)ch, pct);
            publishStateForChannel(idx);
            dynet.scheduleLevelReq((uint8_t)area, (uint8_t)ch, 400);
            return;
          } else {
            dynet.sendFadeToLevel_1s((uint8_t)area, (uint8_t)ch, 100, 0x02);
            em.setChannelLevel((uint8_t)area, (uint8_t)ch, 100);
            publishStateForChannel(idx);
            dynet.scheduleLevelReq((uint8_t)area, (uint8_t)ch, 400);
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
    dynet.scheduleLevelReq((uint8_t)area, (uint8_t)ch, 250);
    return;
  }

  // 3) explicit level topic (0..100)
  if (action == "level/set") {
    int pct = constrain(msg.toInt(), 0, 100);
    dynet.sendFadeToLevel_1s((uint8_t)area, (uint8_t)ch, pct, 0x02);

    em.setChannelLevel((uint8_t)area, (uint8_t)ch, pct);
    publishStateForChannel(idx);
    dynet.scheduleLevelReq((uint8_t)area, (uint8_t)ch, 250);
    return;
  }

  // 4) request one channel’s level (0x61) — no optimistic publish here
  if (action == "request") {
    dynet.sendRequestChannelLevel((uint8_t)area, (uint8_t)ch);
    return;
  }

  // 5) curtain cover command: OPEN / CLOSE / STOP
  if (action == "cover/set") {
    using namespace DynetEntities;
    em.commandCurtain((uint8_t)area, (uint8_t)ch, msg.c_str());
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
  mqtt.subscribe((base + "area/+/hvac/mode/set").c_str());
  mqtt.subscribe((base + "area/+/hvac/fan/set").c_str());
  mqtt.subscribe((base + "area/+/curtain/+/cover/set").c_str());
  mqtt.subscribe((base + "area/+/ch/+/cover/set").c_str());
  mqtt.subscribe((base + "gateway/+").c_str());
}

void mqttEnsureConnected() {
  if (String(cfg.mqtt_server).length() == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) return;

  mqtt.setServer(cfg.mqtt_server, cfg.mqtt_port);
  mqtt.setBufferSize(1536);  // climate entity discovery can exceed 768 bytes
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
    // Publish gateway device discovery + initial state
    if (cfg.ha_discovery) {
      publishHADiscovery_Gateway();
      publishGatewayState();
    }
    // trigger a paced HA discovery sweep for areas/channels
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

static uint32_t lastGwStateMs = 0;

void mqttLoop() {
  if (String(cfg.mqtt_server).length() > 0) {
    if (!mqtt.connected()) {
      if (WiFi.status() == WL_CONNECTED && millis() - lastMqttAttempt >= MQTT_RETRY_MS) {
        lastMqttAttempt = millis();
        mqttEnsureConnected();
      }
    } else {
      mqtt.loop();

      // Gateway state update every 30 s
      if (millis() - lastGwStateMs >= 30000UL) {
        lastGwStateMs = millis();
        publishGatewayState();
      }

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
            publishHADiscovery_Gateway();  // gateway device
            publishGatewayState();
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
