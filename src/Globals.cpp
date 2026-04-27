#include "Globals.h"

// -------------------- HA metadata --------------------
const char* HA_MANUFACTURER = "SmartWay";
const char* HA_MODEL        = "ESPDynetGateway";
const char* HA_SW_VERSION   = "2.1";

// -------------------- EEPROM device_id magic ----------------
const uint32_t DEV_ID_MAGIC = 0x44594E54; // 'DYNT'

// -------------------- Core singletons --------------------
HttpServer   server(80);
#if defined(ETHERNET_SUPPORTED) && !defined(ESP32)
  static EthernetClient ethClient_;
  Client*  netClient = &ethClient_;
#else
  static WiFiClient wifiClient_;
  Client*  netClient = &wifiClient_;
#endif
PubSubClient mqtt(*netClient);
bool         ethModeActive = false;

// -------------------- Pins / options --------------------
int  ledPin = -1;
bool ledActiveLow = false;

int  buttonPin = -1;
bool buttonActiveLow = true;
int txPin = 1; 
int rxPin = 3;
int dePin = -1;

// -------------------- Reboot scheduler --------------------
volatile bool rebootScheduled = false;
unsigned long rebootAtMs      = 0;

// -------------------- Identity / config --------------------
String deviceId;
String apSsid;
String apPass;
AppConfig cfg = {0};

// -------------------- AP network --------------------
const IPAddress AP_IP(192,168,4,1);
const IPAddress AP_GW(192,168,4,1);
const IPAddress AP_SN(255,255,255,0);

// -------------------- Wi-Fi state --------------------
volatile wl_status_t staStatus = WL_IDLE_STATUS;
volatile uint8_t     staDiscReason = 0;
String               staLastEvent = "";
String               staTriedSsid = "";
uint8_t              staRetries   = 0;
uint8_t              staWhichSsid = 1;
unsigned long        lastStaChangeMs = 0;

bool           apActive = false;
uint8_t        staRetry = 0;
unsigned long  lastStaAttempt = 0;
unsigned long  staConnectedAt = 0;

volatile bool rediscoveryScheduled = false;
volatile uint16_t rediscoveryPtr = 0;
volatile unsigned long nextRediscoveryAt = 0;

volatile bool          areasSweepActive  = false;
volatile uint8_t       areasSweepArea    = 2;
volatile uint8_t       areasSweepChannel = 0;
volatile uint8_t       areasSweepPass    = 0;
volatile unsigned long areasSweepNextAt  = 0;


// -------------------- Long-press reset timing --------------------
const unsigned long BTN_LONG_MS   = 15000;
const unsigned long BTN_SAMPLE_MS = 25;
static unsigned long btnLastSample = 0;
static unsigned long btnDownAt     = 0;
static bool          btnWasDown    = false;

// ================================================================
//                    Utility Implementations
// ================================================================
uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t c = 0;
  for (size_t i = 0; i < len; i++) {
    c ^= data[i];
    for (uint8_t b = 0; b < 8; b++) c = (c & 0x80) ? (c << 1) ^ 0x07 : (c << 1);
  }
  return c;
}

int sanitizeGpio(int pin) {
  // accept -1 for "None"; else just pass-through. Add board-specific filtering if needed.
  if (pin == -1) return -1;
  return pin;
}

bool eepromLoadDeviceId(String &out) {
  DevIdBlob blob;
  EEPROM.get(0, blob);
  if (blob.magic != DEV_ID_MAGIC) return false;
  blob.id[sizeof(blob.id)-1] = '\0';
  uint8_t want = crc8((uint8_t*)blob.id, sizeof(blob.id));
  if (want != blob.crc) return false;
  if (blob.id[0] == '\0') return false;
  out = String(blob.id);
  return true;
}

bool eepromSaveDeviceId(const String &id) {
  DevIdBlob blob;
  blob.magic = DEV_ID_MAGIC;
  memset(blob.id, 0, sizeof(blob.id));
  strncpy(blob.id, id.c_str(), sizeof(blob.id)-1);
  blob.crc = crc8((uint8_t*)blob.id, sizeof(blob.id));
  EEPROM.put(0, blob);
  return EEPROM.commit();
}

void scheduleReboot(uint32_t delayMs) {
  rebootScheduled = true;
  rebootAtMs = millis() + delayMs;
}
void serviceScheduledReboot() {
  if (rebootScheduled && (int32_t)(millis() - rebootAtMs) >= 0) {
    ESP.restart();
  }
}

int sanitizeLedGpio(int pin) {
  if (pin == -1) return -1;
#if defined(ESP32)
  if (pin >= 0 && pin <= 39) return pin;
  return -1;
#else
  switch (pin) { case 0: case 2: case 4: case 5: case 12: case 13: case 14: case 15: case 16: return pin; default: return -1; }
#endif
}
int sanitizeButtonGpio(int pin) {
  if (pin == -1) return -1;
#if defined(ESP32)
  if (pin >= 0 && pin <= 39) return pin;
  return -1;
#else
  switch (pin) { case 0: case 2: case 4: case 5: case 12: case 13: case 14: case 15: case 16: return pin; default: return -1; }
#endif
}
void setLed(bool on) {
  if (ledPin < 0) return;
  digitalWrite(ledPin, (ledActiveLow ? (on ? LOW : HIGH) : (on ? HIGH : LOW)));
}
void applyLedPin() {
  static int prev = -1;
  if (prev != ledPin && prev >= 0) pinMode(prev, INPUT);
  if (ledPin >= 0) { pinMode(ledPin, OUTPUT); setLed(false); }
  prev = ledPin;
}
void applyButtonPin() {
  static int prev = -1;
  if (prev != buttonPin && prev >= 0) pinMode(prev, INPUT);
  if (buttonPin >= 0) {
    if (buttonActiveLow) pinMode(buttonPin, INPUT_PULLUP);
    else                 pinMode(buttonPin, INPUT);
  }
  btnDownAt = 0; btnWasDown = false; prev = buttonPin;
}

void factoryResetAndReboot() {
  LittleFS.remove(CONFIG_FILE);
  LittleFS.remove(MAP_FILE);
  DevIdBlob z = {}; EEPROM.put(0, z); EEPROM.commit();
  ESP.restart();
}
void pollButtonLongPress() {
  if (buttonPin < 0) return;
  if (millis() - btnLastSample < BTN_SAMPLE_MS) return;
  btnLastSample = millis();
  int level = digitalRead(buttonPin);
  bool isDown = buttonActiveLow ? (level == LOW) : (level == HIGH);
  if (isDown) {
    if (!btnWasDown) { btnWasDown = true; btnDownAt = millis(); }
    else if (btnDownAt && (millis() - btnDownAt >= BTN_LONG_MS)) { factoryResetAndReboot(); }
  } else { btnWasDown = false; btnDownAt = 0; }
}

void setStr(char* dest, size_t len, const String& s) {
  strncpy(dest, s.c_str(), len);
  dest[len-1] = '\0';
}

String readWholeFile(const char* path) {
  if (!LittleFS.exists(path)) return String("null");
  File f = LittleFS.open(path, "r");
  if (!f) return String("null");
  String body; body.reserve(f.size() + 16);
  while (f && f.available()) body += char(f.read());
  f.close();
  if (body.length() == 0) return String("null");
  return body;
}

bool isApPortalMode() {
  // AP active and no STA IP
  return apActive && (WiFi.status() != WL_CONNECTED);
}

// -------------------- LED status blinker --------------------
// Non-blocking state machine driven from loop().
//
//  Mode 0 — SOLID         : WiFi + MQTT up (or WiFi up, no MQTT server set)
//  Mode 1 — DOUBLE PULSE  : WiFi up, waiting for MQTT   ●●_●●_
//  Mode 2 — FAST BLINK    : WiFi not connected, STA retrying  100ms on/off
//  Mode 3 — SLOW BLINK    : AP portal mode, no WiFi     500ms on / 1500ms off
void ledStatusLoop() {
  if (ledPin < 0) return;

  static unsigned long ledAt    = 0;
  static uint8_t       ledStep  = 0;
  static uint8_t       lastMode = 0xFF;

  const bool wifiOk = (WiFi.status() == WL_CONNECTED);
  const bool mqttOk = mqtt.connected();
  const bool noMqtt = (cfg.mqtt_server[0] == '\0');

  uint8_t mode;
  if      (wifiOk && (mqttOk || noMqtt)) mode = 0;  // solid
  else if (wifiOk)                        mode = 1;  // double pulse — waiting MQTT
  else if (isApPortalMode())              mode = 3;  // slow blink  — AP portal
  else                                    mode = 2;  // fast blink  — STA retrying

  if (mode != lastMode) { lastMode = mode; ledStep = 0; ledAt = 0; }

  const unsigned long now = millis();
  if (now < ledAt) return;

  switch (mode) {
    case 0:  // solid ON — re-assert every second in case of glitch
      setLed(true);
      ledAt = now + 1000;
      break;

    case 1:  // double pulse: ●100 ○100 ●100 ○700 (1000 ms cycle)
      switch (ledStep) {
        case 0: setLed(true);  ledAt = now + 100; break;
        case 1: setLed(false); ledAt = now + 100; break;
        case 2: setLed(true);  ledAt = now + 100; break;
        case 3: setLed(false); ledAt = now + 700; break;
      }
      ledStep = (ledStep + 1) & 3;
      break;

    case 2:  // fast blink — 100 ms on / 100 ms off
      if (ledStep == 0) { setLed(true);  ledAt = now + 100; ledStep = 1; }
      else              { setLed(false); ledAt = now + 100; ledStep = 0; }
      break;

    case 3:  // slow blink — 500 ms on / 1500 ms off
      if (ledStep == 0) { setLed(true);  ledAt = now + 500;  ledStep = 1; }
      else              { setLed(false); ledAt = now + 1500; ledStep = 0; }
      break;
  }
}

// mqttSafeId here (used by MqttManager)
String mqttSafeId(const String& s) {
  String out; out.reserve(s.length());
  for (size_t i=0;i<s.length();i++){
    char c=s[i];
    if ((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'){
      if (c>='A'&&c<='Z') c = char(c+32);
      out+=c;
    } else {
      if (out.length()==0 || out[out.length()-1] != '_') out+='_';
    }
  }
  while (out.length() && out[0]=='_') out.remove(0,1);
  while (out.length() && out[out.length()-1]=='_') out.remove(out.length()-1,1);
  if (!out.length()) out = CHIP_ID_STR;
  return out;
}