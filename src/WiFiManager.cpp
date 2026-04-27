#include "Globals.h"
#include <time.h>

// ── Private state ─────────────────────────────────────────────────────────────
static bool          staBusy        = false;
static unsigned long nextStaAttempt = 0;
static unsigned long staBusySince   = 0;

static const unsigned long STA_CONNECT_GRACE  = 4000UL;   // ms to wait after WiFi.begin()
static const unsigned long STA_RETRY_INTERVAL = 6000UL;   // ms between retries
static const unsigned long ATTEMPT_TIMEOUT    = 12000UL;  // hard timeout per attempt
static const uint8_t       MAX_STA_RETRY      = 3;        // attempts per SSID before switching

// ── Network info logger ───────────────────────────────────────────────────────
static void logStaNetworkInfo() {
  const String ip = WiFi.localIP().toString();
  const String sn = WiFi.subnetMask().toString();
  const String gw = WiFi.gatewayIP().toString();
#if defined(ESP8266)
  const String dns = WiFi.dnsIP().toString();
  LOGF("[NET] IP=%s subnet=%s gateway=%s dns=%s\n",
       ip.c_str(), sn.c_str(), gw.c_str(), dns.c_str());
#else
  const String dns1 = WiFi.dnsIP(0).toString();
  const String dns2 = WiFi.dnsIP(1).toString();
  LOGF("[NET] IP=%s subnet=%s gateway=%s dns1=%s dns2=%s\n",
       ip.c_str(), sn.c_str(), gw.c_str(), dns1.c_str(), dns2.c_str());
#endif
}

// ── NTP sync ──────────────────────────────────────────────────────────────────
static bool          ntpSynced        = false;
static unsigned long nextNtpRetryAt   = 0;
static const unsigned long NTP_RETRY_INTERVAL_MS = 30000;

static void syncNtpTime() {
  if (ntpSynced) {
    LOGF("[NTP] Already synced (epoch=%lu)\n", (unsigned long)time(nullptr));
    return;
  }
  LOGLN("[NTP] Starting time sync with pool.ntp.org/time.nist.gov");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  uint8_t attempts = 0;
  while (now < 100000 && attempts < 40) { delay(500); now = time(nullptr); attempts++; }
  if (now >= 100000) {
    ntpSynced = true;
    nextNtpRetryAt = 0;
    struct tm* utc = gmtime(&now);
    char ts[32] = {0};
    if (utc && strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S UTC", utc) > 0)
      LOGF("[NTP] Sync OK epoch=%lu after %u attempts — %s\n", (unsigned long)now, attempts, ts);
    else
      LOGF("[NTP] Sync OK epoch=%lu after %u attempts\n", (unsigned long)now, attempts);
  } else {
    LOGF("[NTP] Sync failed after %u attempts (epoch=%lu)\n", attempts, (unsigned long)now);
    nextNtpRetryAt = millis() + NTP_RETRY_INTERVAL_MS;
  }
}

// ── Disconnect reason decoder ─────────────────────────────────────────────────
const char* reasonToStr(uint8_t r) {
  switch (r) {
    case 1:  return "UNSPECIFIED";           case 2:  return "AUTH_EXPIRE";
    case 3:  return "AUTH_LEAVE";            case 4:  return "ASSOC_EXPIRE";
    case 5:  return "ASSOC_TOOMANY";         case 6:  return "NOT_AUTHED";
    case 7:  return "NOT_ASSOCED";           case 8:  return "ASSOC_LEAVE";
    case 9:  return "ASSOC_NOT_AUTHED";      case 14: return "4WAY_HANDSHAKE_TIMEOUT";
    case 15: return "GROUP_KEY_UPDATE_TIMEOUT"; case 16: return "IE_IN_4WAY_DIFFERS";
    case 17: return "GROUP_CIPHER_INVALID";  case 18: return "PAIRWISE_CIPHER_INVALID";
    case 19: return "AKMP_INVALID";          case 20: return "UNSUPP_RSN_IE_VERSION";
    case 21: return "INVALID_RSN_IE_CAP";    case 22: return "802_1X_AUTH_FAILED";
    case 23: return "CIPHER_SUITE_REJECTED"; case 200: return "BEACON_TIMEOUT";
    case 201: return "NO_AP_FOUND";          case 202: return "AUTH_FAIL";
    case 203: return "ASSOC_FAIL";           case 204: return "HANDSHAKE_TIMEOUT";
    default: return "UNKNOWN";
  }
}

// ── AP management ─────────────────────────────────────────────────────────────
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
  // NOTE: do NOT call WiFi.mode(WIFI_STA) here — it briefly drops the STA connection
  apActive = false;
}

// ── Connect attempt ───────────────────────────────────────────────────────────
// staRetries++ happens HERE only — event callbacks must never touch it
void beginSTAIfCreds() {
  const char* ssid = (staWhichSsid == 2 && cfg.wifi_ssid2[0]) ? cfg.wifi_ssid2 : cfg.wifi_ssid;
  const char* pass = (staWhichSsid == 2 && cfg.wifi_ssid2[0]) ? cfg.wifi_pass2 : cfg.wifi_pass;
  if (strlen(ssid) == 0) return;

  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);  // must be OFF — SDK auto-retries bypass beginSTAIfCreds()
#if defined(ESP8266)              // and prevent staRetries from incrementing correctly
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
#else
  WiFi.setSleep(false);
#endif
  WiFi.disconnect(false);
  delay(100);
  WiFi.hostname(deviceId.c_str());   // must be after disconnect — ESP8266 resets it
  WiFi.begin(ssid, pass);

  staBusy        = true;
  staBusySince   = millis();
  staRetries++;
  staTriedSsid   = String(ssid);
  nextStaAttempt = millis() + STA_CONNECT_GRACE;
  LOGF("[WIFI] attempt %u/%u -> '%s' (slot %u)\n",
       staRetries, MAX_STA_RETRY, ssid, staWhichSsid);
}

// ── State machine (call every loop()) ────────────────────────────────────────
void updateWiFiSM() {
  static bool lastSsidSaved = false;

  // ── Connected ─────────────────────────────────────────────────────────────
  if (WiFi.status() == WL_CONNECTED) {
    if (apActive) {
      stopAP();
      LOGF("[NET] STA up  IP=%s\n", WiFi.localIP().toString().c_str());
    }

    // Save the connected SSID name once per connection (from main loop — safe on ESP8266)
    if (!lastSsidSaved) {
      String ssid = WiFi.SSID();
      if (ssid.length() > 0) {
        lastSsidSaved = true;
        if (strcmp(cfg.last_ssid_name, ssid.c_str()) != 0) {
          strncpy(cfg.last_ssid_name, ssid.c_str(), sizeof(cfg.last_ssid_name) - 1);
          cfg.last_ssid_name[sizeof(cfg.last_ssid_name) - 1] = '\0';
          saveConfig();
          LOGF("[WIFI] Last SSID saved = '%s'\n", cfg.last_ssid_name);
        }
      }
    }

    staBusy = false;
    if (!ntpSynced && (nextNtpRetryAt == 0 || millis() >= nextNtpRetryAt))
      syncNtpTime();
    return;
  }

  lastSsidSaved = false;

  // ── Hard timeout: SDK silent on WL_NO_SSID_AVAIL ─────────────────────────
  if (staBusy && millis() - staBusySince > ATTEMPT_TIMEOUT) {
    LOGF("[WIFI] attempt timed out after %lu ms (retry=%u slot=%u)\n",
         millis() - staBusySince, staRetries, staWhichSsid);
    staBusy        = false;
    nextStaAttempt = millis() + STA_RETRY_INTERVAL;
  }

  const bool hasPrimary   = cfg.wifi_ssid[0]  != '\0';
  const bool hasSecondary = cfg.wifi_ssid2[0] != '\0';

  // No credentials at all → AP only
  if (!hasPrimary && !hasSecondary) {
    if (!apActive) startAP();
    return;
  }

  // ── Periodic debug log ────────────────────────────────────────────────────
  static unsigned long lastDump = 0;
  if (!staBusy && millis() - lastDump >= 5000UL) {
    lastDump = millis();
    LOGF("[SM] slot=%u retry=%u/%u ssid1='%s' ssid2='%s' last='%s'\n",
         staWhichSsid, staRetries, MAX_STA_RETRY,
         cfg.wifi_ssid, cfg.wifi_ssid2, cfg.last_ssid_name);
  }

  // ── SSID 1 exhausted → switch to SSID 2 ──────────────────────────────────
  if (staRetries >= MAX_STA_RETRY && staWhichSsid == 1 && hasSecondary) {
    LOGF("[WIFI] SSID 1 exhausted — switching to SSID 2 '%s'\n", cfg.wifi_ssid2);
    staWhichSsid   = 2;
    staRetries     = 0;
    staBusy        = false;
    nextStaAttempt = millis();   // fire immediately — falls through to Fire next attempt
  }

  // ── Both SSIDs exhausted → AP up + retry in 30 s ─────────────────────────
  if (staRetries >= MAX_STA_RETRY) {
    if (!apActive) startAP();
    LOGLN("[WIFI] Both SSIDs exhausted — AP up, retrying in 30 s");
    staWhichSsid   = 1;
    staRetries     = 0;
    staBusy        = false;
    nextStaAttempt = millis() + 30000UL;
    return;
  }

  // ── Fire next attempt ─────────────────────────────────────────────────────
  if (!staBusy && millis() >= nextStaAttempt)
    beginSTAIfCreds();
}

// ── WiFi event handlers ───────────────────────────────────────────────────────
void installWiFiHandlers() {
#if defined(ESP8266)
  WiFi.onStationModeConnected([](const WiFiEventStationModeConnected& ev) {
    staStatus       = WL_CONNECTED;
    staDiscReason   = 0;
    staLastEvent    = "CONNECTED to " + String(ev.ssid) + " (ch " + String(ev.channel) + ")";
    lastStaChangeMs = millis();
    staBusy         = true;   // keep busy until GOT_IP
    Serial.printf("[WIFI] %s\n", staLastEvent.c_str());
  });

  WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP& ev) {
    staStatus       = WL_CONNECTED;
    staDiscReason   = 0;
    staLastEvent    = "GOT_IP " + ev.ip.toString();
    lastStaChangeMs = millis();
    staBusy         = false;
    staRetries      = 0;   // reset on success; staWhichSsid kept — retry same slot on drop
    LOGF("[WIFI] %s gw=%s mask=%s rssi=%d dBm\n",
         staLastEvent.c_str(), ev.gw.toString().c_str(),
         ev.mask.toString().c_str(), WiFi.RSSI());
    logStaNetworkInfo();
    // NTP sync triggered from updateWiFiSM() — safe from main loop on ESP8266
  });

  WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& ev) {
    staStatus       = WL_DISCONNECTED;
    staDiscReason   = ev.reason;
    staLastEvent    = String("DISCONNECTED from ") + ev.ssid
                    + " reason=" + reasonToStr(ev.reason)
                    + " (" + String(ev.reason) + ")";
    lastStaChangeMs = millis();
    staBusy         = false;
    ntpSynced       = false;
    nextNtpRetryAt  = 0;
    nextStaAttempt  = millis() + STA_RETRY_INTERVAL;
    // NOTE: do NOT touch staRetries here — only beginSTAIfCreds() may increment it
    Serial.printf("[WIFI] %s\n", staLastEvent.c_str());
  });

  WiFi.onStationModeDHCPTimeout([]() {
    staStatus       = WL_DISCONNECTED;
    staDiscReason   = 0;
    staLastEvent    = "DHCP_TIMEOUT";
    lastStaChangeMs = millis();
    staBusy         = false;
    if (staRetries < MAX_STA_RETRY) staRetries++;   // DHCP failure counts as a used attempt
    nextStaAttempt  = millis() + (STA_RETRY_INTERVAL / 2);
    Serial.println("[WIFI] DHCP_TIMEOUT");
  });

#else  // ESP32
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
      staStatus       = WL_CONNECTED;
      staDiscReason   = 0;
      staLastEvent    = "CONNECTED";
      lastStaChangeMs = millis();
      staBusy         = true;
      Serial.println("[WIFI] CONNECTED");
      return;
    }
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      staStatus       = WL_CONNECTED;
      staDiscReason   = 0;
      staLastEvent    = "GOT_IP " + WiFi.localIP().toString();
      lastStaChangeMs = millis();
      staBusy         = false;
      staRetries      = 0;
      LOGF("[WIFI] %s gw=%s mask=%s rssi=%d dBm\n",
           staLastEvent.c_str(),
           WiFi.gatewayIP().toString().c_str(),
           WiFi.subnetMask().toString().c_str(),
           WiFi.RSSI());
      logStaNetworkInfo();
      return;
    }
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      staStatus       = WL_DISCONNECTED;
      staDiscReason   = info.wifi_sta_disconnected.reason;
      staLastEvent    = String("DISCONNECTED reason=") + reasonToStr(staDiscReason)
                      + " (" + String(staDiscReason) + ")";
      lastStaChangeMs = millis();
      staBusy         = false;
      ntpSynced       = false;
      nextNtpRetryAt  = 0;
      nextStaAttempt  = millis() + STA_RETRY_INTERVAL;
      // NOTE: do NOT touch staRetries here — only beginSTAIfCreds() may increment it
      Serial.printf("[WIFI] %s\n", staLastEvent.c_str());
    }
  });
#endif
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void wifiSetup() {
  WiFi.hostname(deviceId.c_str());

  // Determine boot slot from last connected SSID name (boot-swap-proof)
  LOGF("[WIFI] last='%s'  ssid1='%s'  ssid2='%s'\n",
       cfg.last_ssid_name, cfg.wifi_ssid, cfg.wifi_ssid2);

  if (cfg.last_ssid_name[0] != '\0' &&
      cfg.wifi_ssid2[0]     != '\0' &&
      strcmp(cfg.last_ssid_name, cfg.wifi_ssid2) == 0) {
    staWhichSsid = 2;
    LOGLN("[WIFI] Boot slot: SSID 2 (matches last connected)");
  } else {
    staWhichSsid = 1;
    LOGLN("[WIFI] Boot slot: SSID 1");
  }

  installWiFiHandlers();
  startAP();
  beginSTAIfCreds();
}

void wifiLoop() { updateWiFiSM(); }
