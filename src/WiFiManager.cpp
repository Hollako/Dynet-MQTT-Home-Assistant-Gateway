#include "Globals.h"

// Debounced STA connect/retry
static bool          staBusy = false;
static unsigned long nextStaAttempt = 0;
static const unsigned long STA_CONNECT_GRACE = 4000;
static const unsigned long STA_RETRY_INTERVAL = 6000;
static const uint8_t       MAX_STA_RETRY      = 5;

static void logStaNetworkInfo() {
  LOGF("[NET] IP: %s\n", WiFi.localIP().toString().c_str());
  LOGF("[NET] Subnet: %s\n", WiFi.subnetMask().toString().c_str());
  LOGF("[NET] Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
#if defined(ESP8266)
  LOGF("[NET] DNS: %s\n", WiFi.dnsIP().toString().c_str());
#else
  LOGF("[NET] DNS1: %s\n", WiFi.dnsIP(0).toString().c_str());
  LOGF("[NET] DNS2: %s\n", WiFi.dnsIP(1).toString().c_str());
#endif
}

const char* reasonToStr(uint8_t r) {
  switch (r) {
    case 1:  return "UNSPECIFIED"; case 2:  return "AUTH_EXPIRE"; case 3:  return "AUTH_LEAVE";
    case 4:  return "ASSOC_EXPIRE"; case 5:  return "ASSOC_TOOMANY"; case 6:  return "NOT_AUTHED";
    case 7:  return "NOT_ASSOCED"; case 8:  return "ASSOC_LEAVE"; case 9:  return "ASSOC_NOT_AUTHED";
    case 14: return "4WAY_HANDSHAKE_TIMEOUT"; case 15: return "GROUP_KEY_UPDATE_TIMEOUT";
    case 16: return "IE_IN_4WAY_DIFFERS"; case 17: return "GROUP_CIPHER_INVALID";
    case 18: return "PAIRWISE_CIPHER_INVALID"; case 19: return "AKMP_INVALID";
    case 20: return "UNSUPP_RSN_IE_VERSION"; case 21: return "INVALID_RSN_IE_CAP";
    case 22: return "802_1X_AUTH_FAILED"; case 23: return "CIPHER_SUITE_REJECTED";
    case 200:return "BEACON_TIMEOUT"; case 201:return "NO_AP_FOUND"; case 202:return "AUTH_FAIL";
    case 203:return "ASSOC_FAIL"; case 204:return "HANDSHAKE_TIMEOUT"; default: return "UNKNOWN";
  }
}

void beginSTAIfCreds() {
  if (strlen(cfg.wifi_ssid) == 0) return;

  Serial.printf("[WIFI] begin STA ssid='%s'\n", cfg.wifi_ssid);
  staTriedSsid = String(cfg.wifi_ssid);

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
#if defined(ESP8266)
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
#else
  WiFi.setSleep(false);
#endif
  WiFi.hostname(deviceId.c_str());

  WiFi.disconnect(true);
  delay(50);
  WiFi.begin(cfg.wifi_ssid, cfg.wifi_pass);

  staBusy = true;
  staRetries++;
  nextStaAttempt = millis() + STA_CONNECT_GRACE;
}

void startAP() {
  if (apActive) return;

  if (apSsid.length() == 0) apSsid = String(DEFAULT_AP_SSID_PREFIX) + deviceId;
  if (apPass.length() < 8)  apPass = "Wtouch6980";   // WPA2 is more reliable than open APs

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPdisconnect(true);
  WiFi.softAPConfig(AP_IP, AP_GW, AP_SN);

  bool ok = WiFi.softAP(apSsid.c_str(), apPass.c_str(), 6, 0, 4);
  apActive = ok;

  // Re-bind HTTP so 192.168.4.1 always works
  server.close(); delay(50); server.begin();

  Serial.printf("[NET] AP %s (%s)\n", ok ? "UP" : "FAIL", WiFi.softAPIP().toString().c_str());
}

void stopAP() {
  if (!apActive) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive = false;
}

void updateWiFiSM() {
  // Keep AP ON for the first 5 minutes after boot, only while STA is not connected
  static bool apWindowInit = false;
  static unsigned long apWindowUntil = 0; // deadline to keep AP on

  if (!apWindowInit) {
    apWindowInit = true;
    apWindowUntil = millis() + 300000UL; // 5 minutes
    if (!apActive) startAP();            // bring AP up at boot so UI is reachable
  }

  if (WiFi.status() == WL_CONNECTED) {
    // STA connected → turn AP OFF immediately
    if (apActive) {
      stopAP();
      LOGLN("[NET] STA connected -> AP OFF");
      logStaNetworkInfo();
    }
    staBusy = false;
    staRetries = 0;
    return;
  }

  // Not connected:
  // While inside the 5‑minute window → AP ON. After it expires → AP OFF.
  if (millis() <= apWindowUntil) {
    if (!apActive) startAP();
  } else {
    if (apActive) {
      stopAP();
      Serial.println("[NET] AP window expired -> AP OFF");
    }
  }

  // Debounced STA retries
  if (strlen(cfg.wifi_ssid) > 0) {
    if (!staBusy && millis() >= nextStaAttempt && staRetries < MAX_STA_RETRY) {
      Serial.printf("[WIFI] retry %u to '%s' status=%d\n", staRetries, cfg.wifi_ssid, WiFi.status());
      beginSTAIfCreds(); // sets staBusy + nextStaAttempt
    }
  }
}

void installWiFiDebugHandlers() {
#if defined(ESP8266)
  WiFi.onStationModeConnected([](const WiFiEventStationModeConnected& ev){
    staStatus = WL_CONNECTED; 
    staDiscReason = 0;
    staLastEvent = "CONNECTED to " + String(ev.ssid) + " (ch " + String(ev.channel) + ")";
    lastStaChangeMs = millis();
    staBusy = true;  // until we get IP
    Serial.printf("[WIFI] %s\n", staLastEvent.c_str());
  });

  WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP& ev){
    staStatus = WL_CONNECTED; 
    staDiscReason = 0;
    staLastEvent = "GOT_IP " + ev.ip.toString();
    lastStaChangeMs = millis();
    staBusy = false;  // success
    staRetries = 0;   // reset retries
    LOGF("[WIFI] %s gw=%s mask=%s rssi=%d dBm\n",
      staLastEvent.c_str(), ev.gw.toString().c_str(), ev.mask.toString().c_str(), WiFi.RSSI());
    logStaNetworkInfo();
  });

  WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& ev){
    staStatus = WL_DISCONNECTED;
    staDiscReason = ev.reason;
    staLastEvent = String("DISCONNECTED from ") + ev.ssid + " reason=" + reasonToStr(ev.reason)
                 + " (" + String(ev.reason) + ")";
    lastStaChangeMs = millis();
    staBusy = false;

    const bool manualLeave = (ev.reason == 8);  // ASSOC_LEAVE
    if (!manualLeave && staRetries < MAX_STA_RETRY) staRetries++;

    nextStaAttempt = millis() + STA_RETRY_INTERVAL;
    Serial.printf("[WIFI] %s (retry=%u)\n", staLastEvent.c_str(), staRetries);
  });

  WiFi.onStationModeDHCPTimeout([](){
    staStatus = WL_DISCONNECTED;
    staDiscReason = 0;
    staLastEvent = "DHCP_TIMEOUT";
    lastStaChangeMs = millis();
    staBusy = false;
    if (staRetries < MAX_STA_RETRY) staRetries++;
    nextStaAttempt = millis() + (STA_RETRY_INTERVAL / 2);
    Serial.println("[WIFI] DHCP_TIMEOUT");
  });
#else
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
      staStatus = WL_CONNECTED;
      staDiscReason = 0;
      staLastEvent = "CONNECTED";
      lastStaChangeMs = millis();
      staBusy = true;  // until we get IP
      Serial.println("[WIFI] CONNECTED");
      return;
    }

    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      staStatus = WL_CONNECTED;
      staDiscReason = 0;
      staLastEvent = "GOT_IP " + WiFi.localIP().toString();
      lastStaChangeMs = millis();
      staBusy = false;  // success
      staRetries = 0;   // reset retries
      LOGF("[WIFI] %s gw=%s mask=%s rssi=%d dBm\n",
        staLastEvent.c_str(),
        WiFi.gatewayIP().toString().c_str(),
        WiFi.subnetMask().toString().c_str(),
        WiFi.RSSI());
      logStaNetworkInfo();
      return;
    }

    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      staStatus = WL_DISCONNECTED;
      staDiscReason = info.wifi_sta_disconnected.reason;
      staLastEvent = String("DISCONNECTED reason=") + reasonToStr(staDiscReason)
                  + " (" + String(staDiscReason) + ")";
      lastStaChangeMs = millis();
      staBusy = false;

      const bool manualLeave = (staDiscReason == 8);  // ASSOC_LEAVE
      if (!manualLeave && staRetries < MAX_STA_RETRY) staRetries++;

      nextStaAttempt = millis() + STA_RETRY_INTERVAL;
      Serial.printf("[WIFI] %s (retry=%u)\n", staLastEvent.c_str(), staRetries);
    }
  });
#endif
}

void wifiSetup() { 
  WiFi.hostname(deviceId.c_str()); 
  startAP(); 
  installWiFiDebugHandlers(); 
  beginSTAIfCreds(); 
}

void wifiLoop()  { updateWiFiSM(); }
