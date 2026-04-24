#include <Arduino.h>

#include "Globals.h"
#include "WiFiManager.h"
#include "MqttManager.h"
#include "ConfigStore.h"
#include "WebUI.h"
#include "DynetBus.h"
#include "EntityManager.h"

unsigned long lastDynetPoll = 0;
const unsigned long DYNET_POLL_INTERVAL = 5000; // ms, to request feedback

void setup() {
  Serial.begin(115200);  // 9600 was too slow: progress callback output blocked yield(), starving LwIP
  delay(200);
  
  Serial1.begin(115200);
  //Serial1.println();
  //Serial1.println(F("Logger on Serial1 (GPIO2)"));

  // Web Console Init //
  logs_init(4096);            // <-- allocate buffer (4KB ok for ESP8266)
  logs_serial_ready();  // IMPORTANT: after Serial1.begin()

  LOGF("Boot: logs on Serial1, DyNet on Serial0\n");
  
  // ----- Identity / storage -----
  EEPROM.begin(EEPROM_SIZE);
  String eid;
  if (eepromLoadDeviceId(eid)) deviceId = eid;
  else { deviceId = CHIP_ID_STR; eepromSaveDeviceId(deviceId); }

  if (!LittleFS.begin()) { LittleFS.format(); LittleFS.begin(); }

  // Load config from FS
  loadConfig();
  // IMPORTANT: initialize EntityManager + load persisted entities
  if (!loadEntities()) {
    DynetEntities::em.begin();   // ensure capacities are allocated even on first boot
    LOGF("[persist] no entities.json yet; initialized empty entity manager\n");
  } else {
    LOGF("[persist] entities.json loaded: ch=%d areas=%d\n",
        DynetEntities::em.channelsCount(),
        DynetEntities::em.areasCount());
  }

  // Apply GPIO selections from config (LED/button), and TX if you have a wrapper
  applyLedPin();
  applyButtonPin();

  // Setup WiFi (brings up AP immediately and tries STA)
  wifiSetup();

  // Start MQTT (non-blocking) and try a first connection
  mqttSetup();
  mqttEnsureConnected();   // quick initial attempt

  ArduinoOTA.setHostname(deviceId.c_str()); // sets mDNS hostname used by MDNS.begin() inside ArduinoOTA.begin()
  ArduinoOTA.begin();
  ArduinoOTA.handle();

  //otaSetup();

  // Setup Web server
  registerWebRoutes();

  // Setup OTA/mDNS (optional)
  //otaSetup(); // see helper below

  // Setup Dynet Bus
  dynetSetup();
  // Setup entity manager (HA discovery)
  //DynetEntities::em.begin();

  LOGLN("=== Boot completed ===");
}

void loop() {
  // WiFi state machine (keeps AP/STA logic running)
  wifiLoop();

  // MQTT client (connect/retry + HA discovery pacing)
  mqttLoop();

  // Web server
  server.handleClient();

  // Deferred OTA download (runs after the HTTP handler has returned and
  // released its buffers, giving BearSSL a larger contiguous heap block).
  webOtaLoop();

  // Dynet bus handler
  dynetLoop();

  // Button long-press → factory reset
  pollButtonLongPress();

  // Optional OTA / mDNS service
   ArduinoOTA.handle();
#if defined(ESP8266)
   MDNS.update();
#endif

  // Periodic area/channel poll after preset changes
  dynetPollAreas();

  // Curtain state-machine: interlocking delays + travel-time auto-stop
  DynetEntities::em.pollCurtains();

  // Handle scheduled reboot (used by Save & Reboot, restore, etc.)
  serviceScheduledReboot();

  yield();
}
