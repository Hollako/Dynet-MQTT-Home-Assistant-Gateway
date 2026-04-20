#pragma once

// ===== Platform selection =====
#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  #include <ESPmDNS.h>
  #include <ArduinoOTA.h>
  using HttpServer = WebServer;
  #define CHIP_ID_STR String((uint32_t)ESP.getEfuseMac(), HEX)
#else
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <ESP8266mDNS.h>
  #include <ArduinoOTA.h>
  #include <user_interface.h>
  using HttpServer = ESP8266WebServer;
  #define CHIP_ID_STR String(ESP.getChipId(), HEX)
#endif

#include <LittleFS.h>
#include <EEPROM.h>
#define MQTT_MAX_PACKET_SIZE 512
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Optional Ethernet (W5500 or ESP32-ETH)
// Define ETHERNET_SUPPORTED in platformio or Arduino build flags when you wire it in.
// For ESP32-ETH, we still use WiFiClient with ETH; for W5500 we use Ethernet/EthernetClient.
#if defined(ETHERNET_SUPPORTED)
  #if defined(ESP32)
    #include <ETH.h>         // ESP32 internal Ethernet MAC (LAN8720/etc.)
  #else
    #include <SPI.h>
    #include <Ethernet.h>    // W5100/W5500
  #endif
#endif

// ---------------- constants ----------------
#define DEFAULT_AP_SSID_PREFIX  "ESPDynet_"
#define DEFAULT_AP_PASS         ""         // open AP by default; Config can override
#define CONFIG_FILE             "/config.json"
#define MAP_FILE                "/map.json"  // Areas/Channels/types
#define EEPROM_SIZE             512
#define DEBUG_FS                0

// MQTT / HA
#define HA_DISCOVERY_PREFIX     "homeassistant"
#define BASE_TOPIC_PREFIX       "dynet"
#define HA_ONLINE               "online"
#define HA_OFFLINE              "offline"

extern const char* HA_MANUFACTURER;
extern const char* HA_MODEL;
extern const char* HA_SW_VERSION;

// ---------------- EEPROM device_id blob ----------------
struct DevIdBlob {
  uint32_t magic;      // 'DYNT' = 0x44594E54
  char     id[32];     // device_id (null-terminated)
  uint8_t  crc;        // CRC-8 of id[]
} __attribute__((packed));
extern const uint32_t DEV_ID_MAGIC;

uint8_t crc8(const uint8_t* data, size_t len);
bool eepromLoadDeviceId(String &out);
bool eepromSaveDeviceId(const String &id);

// ---------------- singletons ----------------
extern HttpServer     server;
extern PubSubClient   mqtt;

// Active TCP client for MQTT (WiFiClient or EthernetClient)
class Client;
extern Client*        netClient;
extern bool           ethModeActive;

// ---------------- pins / options ----------------
extern int  ledPin;            // -1 = disabled
extern bool ledActiveLow;
int  sanitizeLedGpio(int pin);
int  sanitizeGpio(int v);
void setLed(bool on);
void applyLedPin();

extern int  buttonPin;         // -1 = disabled
extern bool buttonActiveLow;
int  sanitizeButtonGpio(int pin);
void applyButtonPin();

extern const unsigned long BTN_LONG_MS;
extern const unsigned long BTN_SAMPLE_MS;
void pollButtonLongPress();
void factoryResetAndReboot();

// ---------------- App config ----------------
enum NetMode : uint8_t { NET_WIFI=0, NET_ETHERNET=1 };

struct AppConfig {
  char device_id[32];
  // Network
  uint8_t net_mode;            // 0=WiFi, 1=Ethernet
  char wifi_ssid[64];
  char wifi_pass[64];
  // MQTT
  char mqtt_server[64];
  int  mqtt_port;
  char mqtt_user[32];
  char mqtt_pass[32];
  bool ha_discovery;
  // RS-485 / Dynet
  int  uart_no;                // ESP32: 0/1/2 ; ESP8266 ignore
  uint8_t dynet_max_channels;  // 1..DYNET_MAX_CHANNELS
  uint8_t dynet_max_areas;     // 1..DYNET_MAX_AREAS
};
extern AppConfig cfg;
extern int txPin;   // RS485 TX GPIO (or -1)
extern int rxPin;   // RS485 RX GPIO (or -1)
extern int dePin;   // RS485 DE/RE GPIO (or -1)
int sanitizeGpio(int pin);

// AP network
extern String deviceId;
extern String apSsid;
extern String apPass;

extern const IPAddress AP_IP;
extern const IPAddress AP_GW;
extern const IPAddress AP_SN;

// Wi-Fi state + helpers
extern volatile wl_status_t staStatus;
extern volatile uint8_t     staDiscReason;
extern String               staLastEvent;
extern String               staTriedSsid;
extern uint8_t              staRetries;
extern unsigned long        lastStaChangeMs;

bool isApPortalMode();
extern bool           apActive;
extern uint8_t        staRetry;
extern unsigned long  lastStaAttempt;
extern unsigned long  staConnectedAt;

// NetMgr (WiFi/Ethernet)
void wifiSetup();
void wifiLoop();
void startAP();
void stopAP();
void beginSTAIfCreds();
void updateWiFiSM();
void installWiFiDebugHandlers();
const char* reasonToStr(uint8_t r);

// Ethernet (optional)
void ethSetup();
void ethLoop();
bool ethConnected();

// Reboot scheduler
void scheduleReboot(uint32_t delayMs = 1200);
void serviceScheduledReboot();
extern volatile bool rebootScheduled;
extern unsigned long rebootAtMs;
extern volatile bool rediscoveryScheduled;
extern volatile uint16_t rediscoveryPtr;
extern volatile unsigned long nextRediscoveryAt;

// JSON helper
void setStr(char* dest, size_t len, const String& s);
String readWholeFile(const char* path);

// MQTT / HA topics & publishers (implemented in MqttManager.cpp)
String availabilityTopic();
String areaChannelBaseTopic(uint8_t area, uint8_t ch);
String mqttSafeId(const String& s);
void publishAvailability(const char* payload);
void mqttEnsureConnected();

// Web routes (WebUI.cpp)
void registerWebRoutes();
void handleRootGet();
void handleConfigGet();
void handleConfigPost();
void handleApPortalGet();
void handleApPortalConfigPost();
void handleRegenPost(); // here used to force rediscovery
void handleRestoreBackupUpload();
void handleRestoreBackupPost();
void sendRebootingPage(const String& title, const String& detail, int countdownSec = 10, uint32_t startPollDelayMs = 5000);
void webOtaLoop();   // deferred OTA download — call every loop() iteration

// Dynet stack
void dynetSetup();
void dynetLoop();
void dynetRequestAllKnownLevels(uint8_t area);

// One‑shot area sweep (triggered from WebUI)
extern volatile bool         areasSweepActive;   // true while sweeping
extern volatile uint8_t      areasSweepArea;     // next area to poll (1..N)
extern volatile unsigned long areasSweepNextAt;  // pacing timestamp (ms)

// Persistence APIs (ConfigStore.cpp)
bool loadConfig();
bool saveConfig();
bool loadMap();
bool saveMap();

// Channel mapping/types
enum ChanType : uint8_t { CH_LIGHT_DIM=0, CH_LIGHT_ONOFF=1, CH_SWITCH=2 };

struct ChanEntry {
  uint8_t area;
  uint8_t ch;
  uint8_t type;      // ChanType
  String  name;
  bool    known;     // discovered
  uint8_t level;     // 0..255 (HA brightness semantics)
};

ChanEntry* mapFind(uint8_t area, uint8_t ch, bool createIfMissing=false);
void mapSetType(uint8_t area, uint8_t ch, ChanType t);
void mapSetName(uint8_t area, uint8_t ch, const String& nm);
void mapMarkKnown(uint8_t area, uint8_t ch);
void mapSetLevel(uint8_t area, uint8_t ch, uint8_t br);

// Temperature per Area
struct AreaHVAC {
  uint8_t area;
  bool    present;
  float   tempC;       // actual
  float   setC;        // desired
};
AreaHVAC* hvacGet(uint8_t area, bool createIfMissing=false);

// HA discovery triggers
void haPublishChannelDiscovery(const ChanEntry& e);
void haPublishAreaSensors(uint8_t area, bool publishTemp, bool publishSetpoint);
void haPublishSavePresetButton(uint8_t area);

// MQTT command hooks
void onMqttChannelCommand(uint8_t area, uint8_t ch, const String& cmd);
void onMqttChannelBrightness(uint8_t area, uint8_t ch, int br);
void onMqttAreaPreset(uint8_t area, int preset, uint16_t fade20ms);
void onMqttSavePreset(uint8_t area);
void onMqttRequestLevels(uint8_t area);
void onMqttSetSetpoint(uint8_t area, float celsius);

// ---------------- Web Console Logger ----------------
void logf(const char* fmt, ...);          // printf-style
void logln(const String& s);              // println-style
void log_put_raw(const char* s);          // internal/raw

// Init + readiness flags
void logs_init(size_t capBytes = 0);
void logs_serial_ready();     // call right after Serial.begin()
bool logs_ready();

void logs_clear();
uint32_t logs_seq();                      // last sequence id
// JSON-encode new lines since 'sinceSeq' into 'out'. returns new last seq.
uint32_t logs_serialize_since(uint32_t sinceSeq, String& out);

// Prefer these over Serial.* so logs go to both Serial & Web
#define LOGF(...)  do { logf(__VA_ARGS__); } while(0)
#define LOGLN(s)   do { logln((s)); } while(0)
