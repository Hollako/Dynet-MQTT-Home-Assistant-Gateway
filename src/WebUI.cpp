#include "Globals.h"
#include "ConfigStore.h"
#include "MqttManager.h"
#include "DynetBus.h"
#include "EntityManager.h"
#include <errno.h>

// === HTTP server type & Update include ======================================
#if defined(ESP8266)
  #include <ESP8266WebServer.h>
  #include <ESP8266HTTPClient.h>
  #include <ESP8266httpUpdate.h>
  #include <WiFiClientSecure.h>
  using HttpServer = ESP8266WebServer;
  #include <Updater.h>            // (alias of Update.h on ESP8266 cores)
  #define UPDATE_ABORT() do { Update.end(); } while(0)
#else
  #include <WebServer.h>
  #include <HTTPClient.h>
  #include <HTTPUpdate.h>
  #include <WiFiClientSecure.h>
  using HttpServer = WebServer;
  #include <Update.h>
   #define UPDATE_ABORT() do { Update.abort(); } while(0)
#endif
// This must be a pure declaration that matches your real definition
// (where you actually construct the server, e.g. in Globals.cpp or .ino)
extern HttpServer server;

// Forward declarations for the firmware page handlers (still file scope)
static void handleFwGet();
static void handleFwUpload();
static void handleFwCheckUpdate();
static void handleFwDoUpdate();
static void handleNetCheck();

// Expose the route registrar (Call this from registerWebRoutes)
void registerFwRoutes();


extern DynetBus dynet;
namespace DynetEntities { extern EntityManager em; }
extern void publishHADiscoveryForArea(uint8_t area);

// ---------- small helpers for streamed HTML ----------
static inline void pageBegin(const String& title) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  server.sendContent(
    F("<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>ESP DyNet Gateway - "));
  server.sendContent(title);
  server.sendContent(
    F("</title>"
      "<style>"
      ":root{--bg:#fff;--fg:#111;--muted:#666;--card:#fff;--border:#e5e5e5}"
      "body.dark {--bg:#0f1216;--fg:#e5e7eb;--muted:#9ca3af;--card:#161a20;--border:#273245;}"
      "body{background:var(--bg);color:var(--fg);font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif;margin:0;line-height:1.35}"
      ".nav{position:sticky;top:0;z-index:10;display:flex;gap:10px;align-items:center;padding:10px 14px;border-bottom:1px solid var(--border);background:var(--bg)}"
      ".nav .sp{flex:1}"
      ".btn{display:inline-block;padding:8px 12px;border:1px solid var(--border);border-radius:10px;background:var(--card);cursor:pointer}"
      "a{color:inherit;text-decoration:none}"
      "a.btn{text-decoration:none;color:inherit}"
      ".container{padding:16px}"
      ".card{border:1px solid var(--border);border-radius:12px;padding:12px;margin:10px 0;background:var(--card)}"
      ".muted{color:var(--muted);font-size:12px}"
      ".grid2{display:grid;grid-template-columns:repeat(2,minmax(280px,1fr));row-gap:2px;column-gap:12px}"
      "@media(max-width:900px){.grid2{grid-template-columns:1fr}}"
      ".title{font-weight:600;margin:0 0 6px}"
      ".row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}"
      ".row.space{justify-content:space-between}"
      ".grow{flex:1}"
      ".in,select,button,input[type=text],input[type=password],input[type=number]{min-width:120px;padding:8px;border:1px solid var(--border);border-radius:8px;background:var(--card);color:var(--fg)}"
      "label{font-size:13px;color:var(--muted)}"
      ".status{display:flex;gap:12px;align-items:center;margin:10px 0;flex-wrap:wrap}"
      ".badge{display:inline-flex;align-items:center;gap:6px;font-size:12px;padding:4px 10px;border-radius:999px;background:var(--card);border:1px solid var(--border)}"
      ".dot{width:8px;height:8px;border-radius:50%;background:#bbb}"
      ".ok .dot{background:#19c37d}.err .dot{background:#ef4444}.inactive .dot{background:#aaa}"
      ".form{display:block}"
      ".form-table{display:grid;grid-template-columns:150px minmax(0,1fr);gap:10px 14px;align-items:center}"
      "@media(max-width:640px){.form-table{grid-template-columns:120px minmax(0,1fr)}}"
      ".form-inline{display:flex;gap:10px;align-items:center;flex-wrap:wrap}"
      ".wide{width:100%}"
      "input.in, select.in { box-sizing:border-box; }"
      "button.btn { padding: 10px 16px; min-height: 38px; line-height: 1.2; font-size: 14px; }"
      ".pill{display:inline-block;padding:2px 8px;border:1px solid var(--border);border-radius:999px;font-size:12px;color:var(--muted)}"
      "button.prog{border:1px solid #d39e00;background:rgba(255,193,7,.16);color:#d39e00;border-radius:6px;padding:6px 16px;min-height:32px;font-size:14px;}"
      "button.action{border:1px solid #6c757d;background:rgba(108,117,125,.16);color:#6c757d;border-radius:6px;padding:6px 16px; min-height:32px;font-size:14px;}"
      ".card{border:1px solid var(--border);border-radius:12px;padding:12px;margin:14px 0;background:var(--card)}"

      "</style>"
      "<script>"
      "function upd(b){"
        "const m=document.getElementById('mqttBadge');"
        "const mt=document.getElementById('mqttText');"
        "if(m){m.classList.toggle('ok', b.mqtt_connected); m.classList.toggle('err', !b.mqtt_connected);} "
        "if(mt) mt.textContent = b.mqtt_connected ? ('MQTT: '+(b.mqtt_broker||'')) : 'MQTT disconnected';"
        "const sb=document.getElementById('staBadge');"
        "const st=document.getElementById('staText');"
        "const staUp = !!(b.sta_ip && b.sta_ip.length);"
        "if(sb){sb.classList.toggle('ok', staUp); sb.classList.toggle('err', !staUp);} "
        "if(st) st.textContent = staUp ? ('IP: '+(b.sta_ip||'')) : 'No IP';"
        "const a=document.getElementById('apText');"
        "if(a) a.textContent = b.ap_active ? ('AP: '+(b.ap_ssid||'')) : 'AP: Off';"
      "}"
      "function poll(){fetch('/status',{cache:'no-store'}).then(r=>r.json()).then(upd).catch(()=>{});} "
      "window.addEventListener('DOMContentLoaded',()=>{poll();setInterval(poll,2000);});"
      "function applyTheme(){document.body.classList.toggle('dark',localStorage.dark==='1')}"
      "function toggleDark(){localStorage.dark = (localStorage.dark==='1'?'0':'1');applyTheme()}"
      "</script>"
      "</head><body>")
  );

  // nav
  server.sendContent(F("<div class='nav'><strong>ESP DyNet Gateway - "));
  server.sendContent(deviceId);
  server.sendContent(
    F("</strong><div class='sp'></div>"
      "<a class='btn' href='/'>Home</a>"
      "<a class='btn' href='/config'>Config</a>"
      "<a class='btn' href='/logs'>Logs</a>"
      "<form style='display:inline' method='GET' action='/reboot'><button class='btn' type='submit'>Reboot</button></form>"
      "<button class='btn' onclick='toggleDark()'>Theme</button>"
      "<a class='nav' href='/fw'>Firmware</a>"

    "</div><div class='container'>")
  );

  // status badges
  server.sendContent(
    F("<div class='status'>"
      "<div id='staBadge'  class='badge'><span class='dot'></span><span id='staText'>IP…</span></div>"
      "<div id='mqttBadge' class='badge'><span class='dot'></span><span id='mqttText'>MQTT…</span></div>"
      "<div class='badge inactive'><span class='dot'></span><span id='apText'>AP…</span></div>"
      "<div style='margin-left:auto'></div>"
      "<div class='badge inactive' style='color:var'>v")
  );
  server.sendContent(HA_SW_VERSION);
  server.sendContent(F("</div></div>"));
}
static inline void pageWrite(const __FlashStringHelper* s){ server.sendContent(s); }
static inline void pageWrite(const String& s){ server.sendContent(s); }
static inline void pageEnd(){ server.sendContent(F("</div></body></html>")); }

// Safe helper: convert a fixed-length name buffer to an HTML-attribute-safe String.
// - Never reads past maxLen bytes (handles non-null-terminated buffers).
// - HTML-encodes ' " < > & so the value is safe inside value='...' attributes.
static inline String safeAttr(const char* buf, size_t maxLen) {
  size_t len = strnlen(buf, maxLen);   // stop at first '\0' or maxLen
  String out;
  out.reserve(len);
  for (size_t i = 0; i < len; i++) {
    char c = buf[i];
    if      (c == '\'') out += F("&#39;");
    else if (c == '"')  out += F("&quot;");
    else if (c == '<')  out += F("&lt;");
    else if (c == '>')  out += F("&gt;");
    else if (c == '&')  out += F("&amp;");
    else                out += c;
  }
  return out;
}

// ---- helpers ----
static String gpioOpt(int gpio, const char* label, int current){
  String sel = (current == gpio) ? " selected" : "";
  return String("<option value='") + gpio + "'" + sel + ">" + label +
         (gpio>=0 ? (" (GPIO"+String(gpio)+")") : "") + "</option>";
}

static String gpioOptions(int current) {
  String o;
  o += gpioOpt(-1, "None", current);

  #if defined(ESP8266)
  // UART0 pins (hidden before) — allow them now
  o += gpioOpt(1, "TX0 ⚠️", current);
  o += gpioOpt(3, "RX0 ⚠️", current);

  // "D" pins
  o += gpioOpt(16,"D0", current);
  o += gpioOpt(5, "D1",  current);
  o += gpioOpt(4, "D2",  current);
  o += gpioOpt(0, "D3",  current);
  o += gpioOpt(2, "D4",  current);
  o += gpioOpt(14,"D5", current);
  o += gpioOpt(12,"D6", current);
  o += gpioOpt(13,"D7", current);
  o += gpioOpt(15,"D8", current);
#else
  // ESP32 — list your preferred pins here
  for (int p : {1,3,4,5,12,13,14,15,16,17,18,19,21,22,23,25,26,27,32,33}) {
    String label = String("GPIO") + String(p);
    o += gpioOpt(p, label.c_str(), current);
  }
#endif
  return o;
}

// -------------- Home --------------
void handleRootGet() {
  using namespace DynetEntities;
  pageBegin("Home");
  pageWrite(F(
    "<div class='card'>"
      "<div class='section-title'>DyNet • Send Area Preset (test)</div>"
      "<div class='form-inline' style='gap:10px;flex-wrap:wrap'>"
        "<label>Area</label>"
        "<input class='in' id='ap_area' type='number' min='1' max='255' value='2' style='width:90px'>"
        "<label>Preset</label>"
        "<input class='in' id='ap_preset' type='number' min='1' max='255' value='1' style='width:90px'>"
        "<label>Fade (ms)</label>"
        "<input class='in' id='ap_fade' type='number' min='0' max='600000' value='0' style='width:120px'>"
        "<button class='btn action' onclick='sendAP()'>Send</button>"
        "<span id='ap_status' class='muted'></span>"
      "</div>"
    "</div>"
  ));
  // (Setpoint test card removed — HVAC setpoint is now managed via HA climate entity)
      
  pageWrite(F("<p style='font-size:12px;font-weight:600;color:var(--muted);text-transform:uppercase;letter-spacing:.06em;margin:10px 0 4px'>Areas &amp; Channels</p>"));

  // If nothing yet, hint
  if (em.channelsCount() == 0 && em.areasCount() == 0) {
    pageWrite(F("<div class='card'>No DyNet traffic discovered yet. "
                "Operate your panels or press buttons to let the gateway learn Areas/Channels. "
                "You can also request levels from the Config page.</div>"));
  }

  // Render per-area cards — sorted by area number
  for (int aNum = 1; aNum <= 255; aNum++) {
    int ai = em.findArea((uint8_t)aNum);
    if (ai < 0) continue;
  const AreaState& as = em.areaAt(ai);

  pageWrite(F("<div class='card'>"));
    // ── Area header ──────────────────────────────────────────────────────────
    pageWrite(F("<div class='row space' style='flex-wrap:wrap;gap:6px'>"));
      // Left: number + name input + status pills
      pageWrite(F("<div class='row' style='gap:6px;flex-wrap:wrap;align-items:center;flex:1;min-width:0'>"));
        pageWrite(F("<b style='white-space:nowrap'>Area "));
        pageWrite(String(as.area));
        pageWrite(F("</b>"));

        // Name input + save button
        pageWrite(F("<input id='an_"));
        pageWrite(String(as.area));
        pageWrite(F("' class='in' type='text' placeholder='Name' maxlength='40' value='"));
        pageWrite(as.name[0] ? safeAttr(as.name, sizeof(as.name)) : (String(F("Area ")) + String(as.area)));
        pageWrite(F("' style='width:120px;padding:3px 6px'>"));
        pageWrite(F("<button class='btn' style='padding:2px 14px;min-width:auto;min-height:auto;font-size:13px' title='Save area name' onclick='saveAreaName("));
        pageWrite(String(as.area)); pageWrite(F(",\"an_")); pageWrite(String(as.area)); pageWrite(F("\")'>&#10003;</button>"));

        // Area Type inline selector
        pageWrite(F("<span style='font-size:12px;color:var(--muted);white-space:nowrap'>Area Type:</span>"));
        pageWrite(F("<select class='in' style='padding:2px 6px;min-width:auto;width:auto;font-size:12px' onchange='setAreaType("));
        pageWrite(String(as.area)); pageWrite(F(",this.value)'>"));
        pageWrite(String("<option value='0'") + ((as.areaType==DynetEntities::AREA_LIGHTS)?"  selected":"") + ">Lights</option>");
        pageWrite(String("<option value='1'") + ((as.areaType==DynetEntities::AREA_CURTAIN)?" selected":"") + ">Curtain</option>");
        pageWrite(String("<option value='2'") + ((as.areaType==DynetEntities::AREA_HVAC)?   " selected":"") + ">HVAC</option>");
        pageWrite(F("</select>"));

        // Status pill: preset only (temp moved to HVAC card body)
        pageWrite(F("<span class='pill' id='preset_A")); pageWrite(String(as.area));
        pageWrite(F("'>P:&nbsp;"));
        pageWrite((as.preset0 == 0xFF) ? String("?") : String((int)as.preset0 + 1));
        pageWrite(F("</span>"));

      pageWrite(F("</div>")); // left group

      // Right: add-channel / add-curtain / add-hvac-mode + delete area
      pageWrite(F("<div class='row' style='gap:4px;flex-shrink:0;align-items:center'>"));
        if (as.areaType == DynetEntities::AREA_LIGHTS) {
          pageWrite(F("<span style='font-size:12px;color:var(--muted);white-space:nowrap'>Add Channel:</span>"));
          pageWrite(F("<input id='ac_")); pageWrite(String(as.area));
          pageWrite(F("' type='number' class='in' min='1' max='255' placeholder='Ch#' style='width:54px;padding:3px 5px;min-width:0'>"));
          pageWrite(F("<button class='btn' style='padding:3px 10px;min-width:auto;min-height:auto;font-size:13px' onclick='addCh(")); pageWrite(String(as.area));
          pageWrite(F(",\"ac_")); pageWrite(String(as.area)); pageWrite(F("\")'>+Ch</button>"));
        } else if (as.areaType == DynetEntities::AREA_CURTAIN) {
          pageWrite(F("<button class='btn' style='padding:3px 10px;min-width:auto;min-height:auto;font-size:13px' onclick='addAreaCurtain("));
          pageWrite(String(as.area)); pageWrite(F(")'>+Curtain</button>"));
        } else if (as.areaType == DynetEntities::AREA_HVAC) {
          pageWrite(F("<button class='btn' style='padding:3px 10px;min-width:auto;min-height:auto;font-size:13px' onclick='addHvacMode("));
          pageWrite(String(as.area)); pageWrite(F(")'>+Mode</button>"));
          pageWrite(F("<button class='btn' style='padding:3px 10px;min-width:auto;min-height:auto;font-size:13px' onclick='addHvacFan("));
          pageWrite(String(as.area)); pageWrite(F(")'>+Fan</button>"));
        }
        pageWrite(F("<button class='btn' style='background:#c0392b;color:#fff;padding:3px 8px;min-width:auto;min-height:auto;font-size:13px' title='Delete Area' onclick='delArea("));
        pageWrite(String(as.area)); pageWrite(F(")'>&#10006;</button>"));
      pageWrite(F("</div>")); // right group

    pageWrite(F("</div>")); // header row

    if (as.areaType == DynetEntities::AREA_CURTAIN) {
      // ── Curtain Area: preset count selector + per-curtain cards ─────────────
      uint8_t pCount = as.presetCount ? as.presetCount : (cfg.ha_preset_count ? cfg.ha_preset_count : 4);
      if (pCount > 128) pCount = 128;

      // Preset count selector row (no P1..Pn buttons, just the count dropdown)
      pageWrite(F("<div class='row' style='margin-top:6px;gap:6px;align-items:center;flex-wrap:wrap'>"));
        pageWrite(F("<span style='font-size:12px;color:var(--muted)'>Presets available:</span>"));
        pageWrite(F("<select class='pcSel in' data-area='"));
        pageWrite(String(as.area));
        pageWrite(F("' onchange='setPC("));
        pageWrite(String(as.area));
        pageWrite(F(",this.value)' title='Number of presets'"
                    " style='padding:2px 6px;min-width:auto;width:auto;font-size:13px'>"));
        for (int n = 1; n <= 128; n++) {
          if (n == pCount) pageWrite(String("<option value='") + n + "' selected>" + n + "</option>");
          else             pageWrite(String("<option value='") + n + "'>" + n + "</option>");
        }
        pageWrite(F("</select>"));
      pageWrite(F("</div>")); // preset count row

      // Per-curtain cards — side-by-side (flex-wrap)
      pageWrite(F("<div style='display:flex;flex-wrap:wrap;gap:8px;margin-top:8px'>"));
      if (as.curtains) for (uint8_t ci = 0; ci < DynetEntities::MAX_CURTAINS_PER_AREA; ci++) {
        const DynetEntities::AreaCurtainEntry& ce = as.curtains[ci];
        if (!ce.used) continue;

        pageWrite(F("<div style='border:1px solid var(--border);border-radius:12px;padding:8px 10px;background:var(--card)'>"));

          // ── Header row: name input + delete ────────────────────────────────
          pageWrite(F("<div class='row space' style='gap:6px;align-items:center;margin-bottom:8px'>"));
            pageWrite(F("<input class='in' id='cen_")); pageWrite(String(as.area)); pageWrite(F("_")); pageWrite(String(ci));
            pageWrite(F("' type='text' maxlength='40' placeholder='Curtain name' value='"));
            pageWrite(ce.name[0] ? safeAttr(ce.name, sizeof(ce.name)) : (String("Curtain ") + (ci+1)));
            pageWrite(F("' style='width:150px;padding:3px 6px;font-size:13px'>"));
            pageWrite(F("<button class='btn' style='background:#c0392b;color:#fff;padding:3px 8px;"
                        "min-width:auto;min-height:auto;font-size:13px' title='Delete Curtain'"
                        " onclick='delAreaCurtain("));
            pageWrite(String(as.area)); pageWrite(F(",")); pageWrite(String(ci)); pageWrite(F(")'>&#10006;</button>"));
          pageWrite(F("</div>")); // header row

          // ── Body: preset box (left) + test buttons (right) ─────────────────
          pageWrite(F("<div class='row' style='gap:16px;flex-wrap:wrap;align-items:flex-start'>"));

            // Single box: Curtain Presets + test buttons + Save all together
            pageWrite(F("<div style='padding:8px 12px;border:1px solid var(--border);border-radius:8px;"
                        "display:flex;flex-direction:column;gap:6px;min-width:160px'>"));
              pageWrite(F("<div style='font-size:12px;color:var(--muted);font-weight:600'>Curtain Presets</div>"));

              // Preset row: label | preset dropdown | test button
              auto presetRow2 = [&](const char* label, const char* sfx, uint8_t cur, const char* cmd, const char* btnIcon) {
                pageWrite(F("<div class='row' style='gap:6px;align-items:center'>"));
                  pageWrite(F("<span style='font-size:13px;min-width:52px;white-space:nowrap'>")); pageWrite(label); pageWrite(F("</span>"));
                  pageWrite(F("<select class='in curtainPresetSel' data-area='"));  pageWrite(String(as.area));
                  pageWrite(F("' id='")); pageWrite(sfx); pageWrite(String(as.area)); pageWrite(F("_")); pageWrite(String(ci));
                  pageWrite(F("' style='padding:2px 6px;min-width:auto;width:auto;font-size:13px'>"));
                  for (uint8_t p = 1; p <= pCount; p++) {
                    pageWrite(String("<option value='") + p + "'" + ((cur==p)?" selected":"") + ">P" + p + "</option>");
                  }
                  pageWrite(F("</select>"));
                  pageWrite(F("<button class='btn action' style='padding:3px 10px;min-width:auto;min-height:auto;font-size:13px'"
                              " onclick=\"fetch('/api/area_cover',{method:'POST',"
                              "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
                              "body:'area="));
                  pageWrite(String(as.area)); pageWrite(F("&idx=")); pageWrite(String(ci));
                  pageWrite(F("&cmd=")); pageWrite(cmd); pageWrite(F("'})\"")); pageWrite(F(">")); pageWrite(btnIcon); pageWrite(F("</button>"));
                pageWrite(F("</div>"));
              };
              presetRow2("&#9650; Open",  "cop_", ce.openPreset,  "OPEN",  "&#9650;");
              presetRow2("&#9646; Stop",  "csp_", ce.stopPreset,  "STOP",  "&#9646;");
              presetRow2("&#9660; Close", "ccp_", ce.closePreset, "CLOSE", "&#9660;");

              pageWrite(F("<button class='btn' style='padding:3px 10px;min-width:auto;min-height:auto;"
                          "font-size:13px;margin-top:2px;width:100%' onclick='saveAreaCurtainEntry("));
              pageWrite(String(as.area)); pageWrite(F(",")); pageWrite(String(ci)); pageWrite(F(",this)'>Save</button>"));
            pageWrite(F("</div>")); // preset+test box

          pageWrite(F("</div>")); // body row
        pageWrite(F("</div>")); // curtain card
      }
      pageWrite(F("</div>")); // flex-wrap curtains row

    } else if (as.areaType == DynetEntities::AREA_LIGHTS) {
    // ── Area action row: preset buttons + count selector + utility buttons ──
    pageWrite(F("<div class='row' style='margin-top:5px;flex-wrap:wrap;gap:5px;align-items:center'>"));

      // Preset buttons P1..Pn (visibility controlled by JS via class 'pb' + data-area + data-p)
      {
        for (int p = 1; p <= 128; p++) {
          pageWrite(F("<button class='btn action pb' data-area='"));
          pageWrite(String(as.area));
          pageWrite(F("' data-p='"));
          pageWrite(String(p));
          pageWrite(F("' style='padding:3px 9px;min-width:auto;min-height:auto;font-size:13px' onclick='sendPreset("));
          pageWrite(String(as.area)); pageWrite(F(",")); pageWrite(String(p));
          pageWrite(F(")'>P")); pageWrite(String(p)); pageWrite(F("</button>"));
        }
      }

      // Preset count dropdown — per area, pre-selected to this area's count
      {
        uint8_t thisPc = as.presetCount ? as.presetCount : (cfg.ha_preset_count ? cfg.ha_preset_count : 4);
        pageWrite(F("<select class='pcSel in' data-area='"));
        pageWrite(String(as.area));
        pageWrite(F("' onchange='setPC("));
        pageWrite(String(as.area));
        pageWrite(F(",this.value)' title='Number of presets shown'"
                    " style='padding:2px 4px;min-width:auto;width:auto;font-size:12px'>"));
        for (int n = 1; n <= 128; n++) {
          if (n == thisPc) pageWrite(String("<option value='") + n + "' selected>" + n + "</option>");
          else             pageWrite(String("<option value='") + n + "'>" + n + "</option>");
        }
        pageWrite(F("</select>"));
      }

      // Divider
      pageWrite(F("<span style='width:1px;height:18px;background:var(--border);display:inline-block;margin:0 2px'></span>"));

      // Utility buttons
      pageWrite(F("<form method='POST' action='/area/save_preset' style='margin:0'><input type='hidden' name='area' value='"));
      pageWrite(String(as.area));
      pageWrite(F("'><button class='btn prog' style='padding:3px 10px;min-width:auto;min-height:auto;font-size:13px' type='submit'>Save Preset</button></form>"));
      pageWrite(F("<button class='btn action' style='padding:3px 10px;min-width:auto;min-height:auto;font-size:13px' onclick=\"fetch('/api/area_req',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area="));
      pageWrite(String(as.area)); pageWrite(F("&do=req_preset'})\">Req Preset</button>"));
      pageWrite(F("<button class='btn action' style='padding:3px 10px;min-width:auto;min-height:auto;font-size:13px' onclick=\"fetch('/api/area_req',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area="));
      pageWrite(String(as.area)); pageWrite(F("&do=req_levels'})\">Req Levels</button>"));

    pageWrite(F("</div>")); // action row

    // ── Channel cards — sorted by channel number ──────────────────────────
    pageWrite(F("<div style='margin-top:8px;padding-top:6px' class='grid2'>"));

    for (int chNum = 0; chNum <= 254; chNum++) {
      int ci = em.findChannel((uint8_t)aNum, (uint8_t)chNum);
      if (ci < 0) continue;
      const ChannelState& cs = em.channelAt(ci);
      if (!cs.present) continue;
      if (cs.isCurtainSlave) continue; // DOWN relay — hidden, managed by master

      pageWrite(F("<div class='card' style='padding:8px 8px;margin:0'>"));

        // ── Row 1: Ch# · name input · ✓ · level pill · on/off pill · request · delete ──
        pageWrite(F("<div class='row space' style='gap:4px;flex-wrap:nowrap;align-items:center'>"));
          pageWrite(F("<div class='row' style='gap:4px;align-items:center;flex:1;min-width:0;overflow:hidden'>"));
            pageWrite(F("<b style='white-space:nowrap;font-size:13px'>Ch&nbsp;")); pageWrite(String((int)cs.channel0+1)); pageWrite(F("</b>"));
            pageWrite(F("<input id='cn_")); pageWrite(String(cs.area)); pageWrite(F("_")); pageWrite(String(cs.channel0));
            pageWrite(F("' class='in' type='text' placeholder='Name' maxlength='40' value='"));
            pageWrite(cs.name[0] ? safeAttr(cs.name, sizeof(cs.name)) : (String(F("Area ")) + String(cs.area) + String(F(" Ch ")) + String((int)cs.channel0 + 1)));
            pageWrite(F("' style='width:110px;min-width:0;padding:3px 6px;font-size:13px'>"));
            pageWrite(F("<button class='btn' style='padding:3px 14px;min-width:auto;min-height:auto;font-size:13px' title='Save channel name' onclick='saveChName("));
            pageWrite(String(cs.area)); pageWrite(F(",")); pageWrite(String(cs.channel0));
            pageWrite(F(",\"cn_")); pageWrite(String(cs.area)); pageWrite(F("_")); pageWrite(String(cs.channel0));
            pageWrite(F("\")'>&#10003;</button>"));
            pageWrite(F("<span class='pill' style='font-size:12px'>")); pageWrite(String((int)cs.levelPct)); pageWrite(F("%</span>"));
            pageWrite(F("<span class='pill' style='font-size:12px'>")); pageWrite(cs.isOn ? F("ON") : F("OFF")); pageWrite(F("</span>"));
            pageWrite(F("<button class='btn' style='background:#2980b9;color:#fff;padding:3px 9px;min-width:auto;min-height:auto;font-size:15px'"
                        " onclick=\"fetch('/api/cmd',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
                        "body:'area="));
            pageWrite(String(cs.area)); pageWrite(F("&ch=")); pageWrite(String(cs.channel0));
            pageWrite(F("&cmd=REQ'})\" title='Request level'>&#8635;</button>"));
          pageWrite(F("</div>"));
          pageWrite(F("<button class='btn' style='background:#c0392b;color:#fff;padding:3px 9px;min-width:auto;min-height:auto;font-size:13px;flex-shrink:0' title='Delete Channel' onclick='delCh("));
          pageWrite(String(cs.area)); pageWrite(F(",")); pageWrite(String(cs.channel0));
          pageWrite(F(")'>&#10006;</button>"));
        pageWrite(F("</div>")); // row 1

        // ── Row 2: [Channel Type group] │ [quick control buttons] ──
        pageWrite(F("<div class='row' style='gap:6px;flex-wrap:wrap;margin-top:6px;align-items:center'>"));

          // Channel type group: label + select + save — boxed together
          pageWrite(F("<div style='display:flex;align-items:center;gap:4px;padding:3px 8px;border:1px solid var(--border);border-radius:8px;flex-shrink:0'>"));
            pageWrite(F("<span style='font-size:11px;color:var(--muted);white-space:nowrap'>Channel Type</span>"));
            pageWrite(F("<form class='row' method='POST' action='/api/type' style='gap:4px;margin:0'>"));
              pageWrite(F("<input type='hidden' name='area' value='")); pageWrite(String(cs.area)); pageWrite(F("'>"));
              pageWrite(F("<input type='hidden' name='ch' value='")); pageWrite(String(cs.channel0)); pageWrite(F("'>"));
              pageWrite(F("<select class='in' name='type' style='padding:3px 5px;min-width:auto;width:auto;font-size:13px'>"));
              pageWrite(String("<option value='0'") + ((cs.type==0)?" selected":"") + ">Dimmable</option>");
              pageWrite(String("<option value='1'") + ((cs.type==1)?" selected":"") + ">On/Off Light</option>");
              pageWrite(String("<option value='2'") + ((cs.type==2)?" selected":"") + ">Switch</option>");
              pageWrite(String("<option value='3'") + ((cs.type==3)?" selected":"") + ">Curtain</option>");
              pageWrite(F("</select>"));
              pageWrite(F("<button class='btn' type='submit' style='padding:3px 10px;min-width:auto;min-height:auto;font-size:13px'>Save</button>"));
            pageWrite(F("</form>"));
          pageWrite(F("</div>")); // type group

          // Quick control buttons — curtain vs normal channel
          if (cs.type == DynetEntities::CURTAIN) {
            // OPEN / STOP / CLOSE
            auto cbtn = [&](const char* cap, const char* cmd) {
              pageWrite(F("<button class='btn action' style='padding:3px 9px;min-width:auto;min-height:auto;font-size:13px' onclick=\"fetch('/api/cmd',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area="));
              pageWrite(String(cs.area)); pageWrite(F("&ch=")); pageWrite(String(cs.channel0));
              pageWrite(F("&cmd=CURTAIN_")); pageWrite(cmd); pageWrite(F("'})\">"));
              pageWrite(cap); pageWrite(F("</button>"));
            };
            cbtn("&#9650; Open",  "OPEN");
            cbtn("&#9646; Stop",  "STOP");
            cbtn("&#9660; Close", "CLOSE");
            // Travel time input
            pageWrite(F("<div style='display:flex;align-items:center;gap:4px;padding:3px 8px;border:1px solid var(--border);border-radius:8px;flex-shrink:0'>"));
              pageWrite(F("<span style='font-size:11px;color:var(--muted);white-space:nowrap'>Travel (s)</span>"));
              pageWrite(F("<input id='ct_")); pageWrite(String(cs.area)); pageWrite(F("_")); pageWrite(String(cs.channel0));
              pageWrite(F("' type='number' min='1' max='255' value='")); pageWrite(String(cs.curtainTimeSec));
              pageWrite(F("' style='width:52px;padding:3px 5px;min-width:0;font-size:13px;border:1px solid var(--border);border-radius:6px;background:var(--card);color:var(--fg)'>"));
              pageWrite(F("<button class='btn' style='padding:3px 8px;min-width:auto;min-height:auto;font-size:13px' title='Save travel time' onclick='saveCurtainTime("));
              pageWrite(String(cs.area)); pageWrite(F(",")); pageWrite(String(cs.channel0));
              pageWrite(F(",\"ct_")); pageWrite(String(cs.area)); pageWrite(F("_")); pageWrite(String(cs.channel0));
              pageWrite(F("\")'>&#10003;</button>"));
            pageWrite(F("</div>"));
          } else {
            auto qbtn = [&](const char* cap, const char* cmd){
              pageWrite(F("<button class='btn action' style='padding:3px 9px;min-width:auto;min-height:auto;font-size:13px' onclick=\"fetch('/api/cmd',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area="));
              pageWrite(String(cs.area)); pageWrite(F("&ch=")); pageWrite(String(cs.channel0));
              pageWrite(F("&cmd=")); pageWrite(cmd); pageWrite(F("'})\">"));
              pageWrite(cap); pageWrite(F("</button>"));
            };
            qbtn("ON","ON"); qbtn("OFF","OFF");
            qbtn("25%","SET=25"); qbtn("50%","SET=50");
            qbtn("75%","SET=75"); qbtn("100%","SET=100");
          }
        pageWrite(F("</div>")); // row 2

      pageWrite(F("</div>")); // channel card
    }

    pageWrite(F("</div>")); // grid
    } // end else AREA_LIGHTS

    // ── HVAC Area body ───────────────────────────────────────────────────────
    if (as.areaType == DynetEntities::AREA_HVAC) {
      // Temperature / setpoint live display
      pageWrite(F("<div class='row' style='margin-top:6px;gap:10px;align-items:center;flex-wrap:wrap'>"));
        pageWrite(F("<span style='font-size:13px;color:var(--muted)'>Current Temp:</span>"));
        pageWrite(F("<span class='pill' id='hvac_temp_A")); pageWrite(String(as.area));
        if (as.hasTemp) { pageWrite(F("'>")); pageWrite(String(as.tempC,1)); pageWrite(F("°C</span>")); }
        else            { pageWrite(F("' style='color:var(--muted)'>–°C</span>")); }
        pageWrite(F("<span style='font-size:13px;color:var(--muted)'>Setpoint:</span>"));
        pageWrite(F("<span class='pill' id='hvac_sp_A")); pageWrite(String(as.area));
        if (as.hasSetpt) { pageWrite(F("'>")); pageWrite(String(as.setptC,1)); pageWrite(F("°C</span>")); }
        else             { pageWrite(F("' style='color:var(--muted)'>–°C</span>")); }
        pageWrite(F("<span style='font-size:13px;color:var(--muted)'>Mode:</span>"));
        pageWrite(F("<span class='pill' id='hvac_mode_A")); pageWrite(String(as.area));
        pageWrite(F("'>"));
        if (as.hvac && as.hvac->currentMode[0]) pageWrite(String(as.hvac->currentMode));
        else pageWrite(F("–"));
        pageWrite(F("</span>"));
      pageWrite(F("</div>"));

      if (as.hvac) {
        // ── Modes + Fan side by side ─────────────────────────────────────────
        pageWrite(F("<div style='display:flex;flex-wrap:wrap;gap:24px;margin-top:8px;align-items:flex-start'>"));

        // Left: HVAC Modes
        pageWrite(F("<div>"));
        pageWrite(F("<div style='font-size:12px;font-weight:600;color:var(--muted);margin-bottom:4px'>HVAC Modes (name → preset)</div>"));
        for (uint8_t mi = 0; mi < DynetEntities::MAX_HVAC_MODES; mi++) {
          const DynetEntities::HvacModeEntry& me = as.hvac->modes[mi];
          if (!me.used) continue;
          pageWrite(F("<div class='row' style='gap:6px;align-items:center;margin-bottom:4px'>"));
            pageWrite(F("<input class='in' id='hvm_n_")); pageWrite(String(as.area)); pageWrite(F("_")); pageWrite(String(mi));
            pageWrite(F("' type='text' maxlength='23' placeholder='Mode name' value='"));
            pageWrite(me.name[0] ? safeAttr(me.name, sizeof(me.name)) : "");
            pageWrite(F("' style='width:120px;padding:3px 6px'>"));
            pageWrite(F("<span style='font-size:12px;color:var(--muted)'>P:</span>"));
            pageWrite(F("<input class='in' id='hvm_p_")); pageWrite(String(as.area)); pageWrite(F("_")); pageWrite(String(mi));
            pageWrite(F("' type='number' min='1' max='255' value='"));
            pageWrite(String(me.preset1));
            pageWrite(F("' style='width:56px;padding:3px 5px'>"));
            pageWrite(F("<button class='btn' style='padding:2px 8px;min-width:auto;min-height:auto;font-size:12px' onclick='saveHvacMode("));
            pageWrite(String(as.area)); pageWrite(F(",")); pageWrite(String(mi)); pageWrite(F(")'>&#10003;</button>"));
            pageWrite(F("<button class='btn' style='background:#c0392b;color:#fff;padding:2px 6px;min-width:auto;min-height:auto;font-size:12px' onclick='delHvacMode("));
            pageWrite(String(as.area)); pageWrite(F(",")); pageWrite(String(mi)); pageWrite(F(")'>&#10006;</button>"));
          pageWrite(F("</div>"));
        }
        pageWrite(F("</div>")); // end modes column

        // Right: Fan Modes
        if (as.hvac->fanCount > 0) {
          pageWrite(F("<div>"));
          pageWrite(F("<div style='font-size:12px;font-weight:600;color:var(--muted);margin-bottom:4px'>Fan Modes (name → preset)</div>"));
          for (uint8_t fi = 0; fi < DynetEntities::MAX_HVAC_FANMODES; fi++) {
            const DynetEntities::HvacModeEntry& fe = as.hvac->fanModes[fi];
            if (!fe.used) continue;
            pageWrite(F("<div class='row' style='gap:6px;align-items:center;margin-bottom:4px'>"));
              pageWrite(F("<input class='in' id='hvf_n_")); pageWrite(String(as.area)); pageWrite(F("_")); pageWrite(String(fi));
              pageWrite(F("' type='text' maxlength='23' placeholder='Fan mode name' value='"));
              pageWrite(fe.name[0] ? safeAttr(fe.name, sizeof(fe.name)) : "");
              pageWrite(F("' style='width:120px;padding:3px 6px'>"));
              pageWrite(F("<span style='font-size:12px;color:var(--muted)'>P:</span>"));
              pageWrite(F("<input class='in' id='hvf_p_")); pageWrite(String(as.area)); pageWrite(F("_")); pageWrite(String(fi));
              pageWrite(F("' type='number' min='1' max='255' value='"));
              pageWrite(String(fe.preset1));
              pageWrite(F("' style='width:56px;padding:3px 5px'>"));
              pageWrite(F("<button class='btn' style='padding:2px 8px;min-width:auto;min-height:auto;font-size:12px' onclick='saveHvacFan("));
              pageWrite(String(as.area)); pageWrite(F(",")); pageWrite(String(fi)); pageWrite(F(")'>&#10003;</button>"));
              pageWrite(F("<button class='btn' style='background:#c0392b;color:#fff;padding:2px 6px;min-width:auto;min-height:auto;font-size:12px' onclick='delHvacFan("));
              pageWrite(String(as.area)); pageWrite(F(",")); pageWrite(String(fi)); pageWrite(F(")'>&#10006;</button>"));
            pageWrite(F("</div>"));
          }
          pageWrite(F("</div>")); // end fan column
        }

        pageWrite(F("</div>")); // end flex row
      }
    } // end HVAC

  pageWrite(F("</div>"));   // area card
  }

  // --- Add Area ---
  pageWrite(F(
    "<div class='card'>"
      "<div class='row' style='gap:8px;align-items:center'>"
        "<b>Add Area:</b>"
        "<input id='add_area_num' class='in' type='number' min='2' max='255' placeholder='Area #' style='width:80px'>"
        "<button class='btn' onclick='addArea()'>+Area</button>"
      "</div>"
    "</div>"
  ));

  // --- Scripts: keep existing sendAP + add live pills updater ---
  pageWrite(F(
  "<script>"
    "function sendAP(){"
      "const a=document.getElementById('ap_area').valueAsNumber||0;"
      "const p=document.getElementById('ap_preset').valueAsNumber||0;"
      "const f=document.getElementById('ap_fade').valueAsNumber||0;"
      "const s=document.getElementById('ap_status');"
      "s.textContent='Sending…';"
      "fetch('/api/area_preset',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "body:'area='+encodeURIComponent(a)+'&preset='+encodeURIComponent(p)+'&fade='+encodeURIComponent(f)})"
        ".then(r=>r.json()).then(j=>{s.textContent=j.ok?'Sent ✓':'Failed';})"
        ".catch(()=>{s.textContent='Error';});"
    "}"
    // --- Live UI updater for Preset pill + HVAC card elements
    "function updAreas(list){"
      "if(!Array.isArray(list)) return;"
      "for(const a of list){"
        "const id=String(a.area);"
        // preset pill (+1, unknown=255)
        "const p=document.getElementById('preset_A'+id);"
        "if(p && typeof a.preset0!=='undefined'){"
          "p.textContent='P:\u00a0'+(a.preset0===255?'?':(a.preset0+1));"
        "}"
        // HVAC live elements
        "const ht=document.getElementById('hvac_temp_A'+id);"
        "if(ht)ht.textContent=(a.hasTemp && typeof a.tempC==='number')?a.tempC.toFixed(1)+'°C':'–°C';"
        "const hs=document.getElementById('hvac_sp_A'+id);"
        "if(hs)hs.textContent=(a.hasSetpt && typeof a.setptC==='number')?a.setptC.toFixed(1)+'°C':'–°C';"
        "const hm=document.getElementById('hvac_mode_A'+id);"
        "if(hm && a.hvacMode)hm.textContent=a.hvacMode;"
      "}"
    "}"
    "function pollAreas(){"
      "fetch('/areas_status',{cache:'no-store'})"
        ".then(r=>r.json())"
        ".then(j=>updAreas(j.areas))"
        ".catch(()=>{});"
    "}"
    // ---- preset buttons (per-area counts) ----
    "var aPC={"));
  // Embed per-area preset counts as a JS object literal
  for (int _i = 0; _i < DynetEntities::em.areasCount(); _i++) {
    const auto& _a = DynetEntities::em.areaAt(_i);
    if (!_a.present) continue;
    uint8_t _pc = _a.presetCount ? _a.presetCount : (cfg.ha_preset_count ? cfg.ha_preset_count : 4);
    pageWrite(String(_a.area) + ":" + _pc + ",");
  }
  pageWrite(F("};"
    "function applyPC(area){"
      "var pc=aPC[area]||4;"
      "document.querySelectorAll('.pb[data-area=\"'+area+'\"]').forEach(function(b){"
        "b.style.display=(parseInt(b.dataset.p)<=pc)?'':'none';"
      "});"
      "document.querySelectorAll('.pcSel[data-area=\"'+area+'\"]').forEach(function(s){s.value=String(pc);});"
      "document.querySelectorAll('.curtainPresetSel[data-area=\"'+area+'\"]').forEach(function(s){"
        "var cur=parseInt(s.value)||1;"
        "var html='';"
        "for(var p=1;p<=pc;p++)html+='<option value=\"'+p+'\"'+(p===cur?' selected':'')+'>P'+p+'</option>';"
        "s.innerHTML=html;"
        "if(cur>pc)s.value=String(pc);"
      "});"
    "}"
    "function applyAllPC(){"
      "Object.keys(aPC).forEach(function(a){applyPC(parseInt(a));});"
    "}"
    "function setPC(area,n){"
      "aPC[area]=parseInt(n);"
      "applyPC(area);"
      "fetch('/api/set_preset_count',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area='+area+'&n='+n}).catch(function(){});"
    "}"
    "function sendPreset(a,p){"
      "fetch('/api/area_preset',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area='+a+'&preset='+p+'&fade=0'});"
    "}"
    "window.addEventListener('DOMContentLoaded',()=>{"
      "setInterval(pollAreas,1500);"
      "pollAreas();"
      "applyAllPC();"
    "});"
    // ---- manual add/delete helpers ----
    "function apiPost(url,body){"
      "fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})"
      ".then(r=>r.json()).then(j=>{if(j.ok)location.reload();else alert(j.error||'Failed');}).catch(()=>alert('Error'));"
    "}"
    "function saveName(url,body,btnEl){"
      "fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})"
      ".then(r=>r.json()).then(j=>{"
        "if(btnEl){const t=btnEl.textContent;btnEl.textContent='✓';setTimeout(()=>{btnEl.textContent=t;},1200);}"
      "}).catch(()=>alert('Error'));"
    "}"
    "function saveChName(area,ch,inputId){"
      "const el=document.getElementById(inputId);"
      "if(!el)return;"
      "const v=el.value.trim();"
      "if(!v){el.style.borderColor='#e74c3c';setTimeout(()=>{el.style.borderColor='';},1500);return;}"
      "el.value=v;"
      "saveName('/api/set_name','type=ch&area='+area+'&ch='+ch+'&name='+encodeURIComponent(v),el.nextElementSibling);"
    "}"
    "function saveAreaName(area,inputId){"
      "const el=document.getElementById(inputId);"
      "if(!el)return;"
      "const v=el.value.trim();"
      "if(!v){el.style.borderColor='#e74c3c';setTimeout(()=>{el.style.borderColor='';},1500);return;}"
      "el.value=v;"
      "saveName('/api/set_name','type=area&area='+area+'&name='+encodeURIComponent(v),el.nextElementSibling);"
    "}"
    "function saveCurtainTime(area,ch,inputId){"
      "const el=document.getElementById(inputId);"
      "if(!el)return;"
      "const v=parseInt(el.value)||30;"
      "fetch('/api/set_curtain_time',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area='+area+'&ch='+ch+'&t='+v})"
        ".then(r=>r.json()).then(j=>{"
          "const btn=el.nextElementSibling;"
          "if(btn){const t=btn.textContent;btn.textContent='✓';setTimeout(()=>{btn.textContent=t;},1200);}"
        "}).catch(()=>alert('Error'));"
    "}"
    "function delCh(area,ch){"
      "if(!confirm('Delete Area '+area+' Channel '+(ch+1)+'?'))return;"
      "apiPost('/api/del_channel','area='+area+'&ch='+ch);"
    "}"
    "function addCh(area,inputId){"
      "const v=parseInt(document.getElementById(inputId).value);"
      "if(!v||v<1||v>255){alert('Enter channel number 1..255');return;}"
      "apiPost('/api/add_channel','area='+area+'&ch='+(v-1));"
    "}"
    "function delArea(area){"
      "if(!confirm('Delete Area '+area+' and ALL its channels?'))return;"
      "apiPost('/api/del_area','area='+area);"
    "}"
    "function addArea(){"
      "const v=parseInt(document.getElementById('add_area_num').value);"
      "if(!v||v<2||v>255){alert('Enter area number 2..255');return;}"
      "apiPost('/api/add_area','area='+v);"
    "}"
    "function setAreaType(area,t){"
      "fetch('/api/area_type',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area='+area+'&type='+t})"
        ".then(r=>r.json()).then(j=>{if(j.ok)location.reload();else alert('Failed');}).catch(()=>alert('Error'));"
    "}"
    "function addHvacMode(area){"
      "const name=prompt('Mode name (e.g. cool, heat, off):'); if(!name)return;"
      "const preset=parseInt(prompt('Dynalite preset number for this mode:')); if(!preset)return;"
      "apiPost('/api/hvac/add_mode','area='+area+'&name='+encodeURIComponent(name)+'&preset='+preset);"
    "}"
    "function addHvacFan(area){"
      "const name=prompt('Fan mode name (e.g. low, medium, high):'); if(!name)return;"
      "const preset=parseInt(prompt('Dynalite preset number for this fan mode:')); if(!preset)return;"
      "apiPost('/api/hvac/add_fan','area='+area+'&name='+encodeURIComponent(name)+'&preset='+preset);"
    "}"
    "function saveHvacMode(area,idx){"
      "const n=document.getElementById('hvm_n_'+area+'_'+idx);"
      "const p=document.getElementById('hvm_p_'+area+'_'+idx);"
      "if(!n||!p)return;"
      "fetch('/api/hvac/save_mode',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "body:'area='+area+'&idx='+idx+'&name='+encodeURIComponent(n.value.trim())+'&preset='+p.value})"
        ".then(r=>r.json()).then(j=>{if(!j.ok)alert('Failed');}).catch(()=>alert('Error'));"
    "}"
    "function saveHvacFan(area,idx){"
      "const n=document.getElementById('hvf_n_'+area+'_'+idx);"
      "const p=document.getElementById('hvf_p_'+area+'_'+idx);"
      "if(!n||!p)return;"
      "fetch('/api/hvac/save_fan',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "body:'area='+area+'&idx='+idx+'&name='+encodeURIComponent(n.value.trim())+'&preset='+p.value})"
        ".then(r=>r.json()).then(j=>{if(!j.ok)alert('Failed');}).catch(()=>alert('Error'));"
    "}"
    "function delHvacMode(area,idx){"
      "if(!confirm('Delete this HVAC mode?'))return;"
      "apiPost('/api/hvac/del_mode','area='+area+'&idx='+idx);"
    "}"
    "function delHvacFan(area,idx){"
      "if(!confirm('Delete this fan mode?'))return;"
      "apiPost('/api/hvac/del_fan','area='+area+'&idx='+idx);"
    "}"
    "function addAreaCurtain(area){"
      "fetch('/api/add_area_curtain',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area='+area})"
        ".then(r=>r.json()).then(j=>{if(j.ok)location.reload();else alert(j.error||'Full (max 32)');}).catch(()=>alert('Error'));"
    "}"
    "function delAreaCurtain(area,idx){"
      "if(!confirm('Delete curtain '+(idx+1)+' from Area '+area+'?'))return;"
      "fetch('/api/del_area_curtain',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area='+area+'&idx='+idx})"
        ".then(r=>r.json()).then(j=>{if(j.ok)location.reload();else alert('Failed');}).catch(()=>alert('Error'));"
    "}"
    "function saveAreaCurtainEntry(area,idx,btn){"
      "var n=document.getElementById('cen_'+area+'_'+idx).value.trim();"
      "var op=document.getElementById('cop_'+area+'_'+idx).value;"
      "var cp=document.getElementById('ccp_'+area+'_'+idx).value;"
      "var sp=document.getElementById('csp_'+area+'_'+idx).value;"
      "fetch('/api/save_area_curtain',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "body:'area='+area+'&idx='+idx+'&name='+encodeURIComponent(n)+'&open='+op+'&close='+cp+'&stop='+sp})"
        ".then(r=>r.json()).then(j=>{"
          "if(j.ok&&btn){var t=btn.textContent;btn.textContent='✓';setTimeout(function(){btn.textContent=t;},1200);}"
          "else if(!j.ok)alert('Save failed');"
        "}).catch(function(){alert('Error');});"
    "}"
  "</script>"
  ));

  pageEnd();
}

// -------------- Config --------------
void handleConfigGet() {
  pageBegin("Configuration");

  pageWrite(F("<h1>Configuration</h1>"));
  pageWrite(F("<form id='cfg' class='form' method='POST' action='/config'>"));

  // Device
  pageWrite(F("<div class='card'><div class='form-table'>"));
  pageWrite(F("<div>Device ID</div><div><input class='in' name='device_id' type='text' value='"));
  pageWrite(deviceId);
  pageWrite(F("'></div>"));

  // RS485 Tx GPIO
  pageWrite(F("<div class='lbl'>RS485 Tx GPIO</div><div class='ctl'><select class='in' name='tx_pin'>"));
  pageWrite(gpioOptions(txPin));
  pageWrite(F("</select></div>"));

  // RS485 Rx GPIO
  pageWrite(F("<div class='lbl'>RS485 Rx GPIO</div><div class='ctl'><select class='in' name='rx_pin'>"));
  pageWrite(gpioOptions(rxPin));
  pageWrite(F("</select></div>"));

  // RS485 DE/RE GPIO (optional)
  pageWrite(F("<div class='lbl'>RS485 DE/RE GPIO</div><div class='ctl'><select class='in' name='de_pin'>"));
  pageWrite(gpioOptions(dePin));
  pageWrite(F("</select><div class='muted'>Set to None if your MAX485 auto‑controls DE/RE.</div></div>"));

    // DyNet limits
  uint8_t defCh = cfg.dynet_max_channels ? cfg.dynet_max_channels : (uint8_t)DYNET_MAX_CHANNELS;
  //uint8_t defAr = cfg.dynet_max_areas    ? cfg.dynet_max_areas    : (uint8_t)DYNET_MAX_AREAS;
  uint8_t defAr = cfg.dynet_max_areas ? cfg.dynet_max_areas : (uint8_t)DYNET_MAX_AREAS;

  pageWrite(F("<div>DyNet Max Channels</div><div>"
              "<input class='in' name='dynet_max_channels' type='number' min='1' max='128' value='"));
  pageWrite(String(defCh));
  pageWrite(F("'><div class='muted'>How many channels to track per area.</div></div>"));

  pageWrite(F("<div>DyNet Max Areas</div><div>"
              "<input class='in' name='dynet_max_areas' type='number' min='2' max='255' value='"));
  pageWrite(String(defAr));
  pageWrite(F("'><div class='muted'>Highest Area number to scan (1..N).</div></div>"));

  
  // LED
  pageWrite(F("<div>Link LED</div><div><select class='in' name='led_pin'>"));
  pageWrite(gpioOptions(ledPin));
  pageWrite(F("</select><label style='margin-left:8px'><input type='checkbox' name='led_invert' "));
  if (ledActiveLow) pageWrite(F("checked"));
  pageWrite(F("> Active low</label></div>"));

  // Button
  pageWrite(F("<div>Reset Button</div><div><select class='in' name='btn_pin'>"));
  pageWrite(gpioOptions(buttonPin));
  pageWrite(F("</select><label style='margin-left:8px'><input type='checkbox' name='btn_invert' "));
  if (buttonActiveLow) pageWrite(F("checked"));
  pageWrite(F("> Use internal pull-up (hold 15s)</label></div>"));

  pageWrite(F("</div></div>")); // /card

  // Wi-Fi
  pageWrite(F("<div class='card'><div class='form-table'>"));
  pageWrite(F("<div>Wi‑Fi SSID (STA)</div><div>"
              "<input class='in' id='staSsid' name='wifi_ssid' type='text' value='"));
  pageWrite(String(cfg.wifi_ssid));
  pageWrite(F("'> <select class='in' id='ssidSelect'><option value=''> Select from scan </option></select>"
              " <button id='scanBtn' class='btn' type='button' onclick='doScan()'>Scan</button>"
              "</div>"));

  pageWrite(F("<div>Wi‑Fi Password</div><div><input class='in' name='wifi_pass' type='password' value='"));
  pageWrite(String(cfg.wifi_pass));
  pageWrite(F("'></div>"));

  // AP
  pageWrite(F("<div>AP SSID</div><div><input class='in' name='ap_ssid' type='text' value='"));
  pageWrite(apSsid);
  pageWrite(F("'></div>"));
  pageWrite(F("<div>AP Password</div><div><input class='in' name='ap_pass' type='password' value='"));
  pageWrite(apPass);
  pageWrite(F("'></div>"));

  pageWrite(F("</div></div>"));

  // MQTT
  pageWrite(F("<div class='card'><div class='form-table'>"));
  pageWrite(F("<div>MQTT Server</div><div><input class='in' name='mqtt_server' type='text' value='"));
  if (mqtt.connected()) pageWrite(String(cfg.mqtt_server));
  pageWrite(F("'></div>"));
  pageWrite(F("<div>MQTT Port</div><div><input class='in' name='mqtt_port' type='number' value='"));
  if (mqtt.connected()) pageWrite(String(cfg.mqtt_port));
  pageWrite(F("'></div>"));
  pageWrite(F("<div>MQTT User</div><div><input class='in' name='mqtt_user' type='text' value='"));
  if (mqtt.connected()) pageWrite(String(cfg.mqtt_user));
  pageWrite(F("'></div>"));
  pageWrite(F("<div>MQTT Password</div><div><input class='in' name='mqtt_pass' type='password' value='"));
  if (mqtt.connected()) pageWrite(String(cfg.mqtt_pass));
  pageWrite(F("'></div>"));
  pageWrite(F("<div>Home Assistant</div><div><label><input name='ha_discovery' type='checkbox' "));
  if(cfg.ha_discovery) pageWrite(F("checked"));
  pageWrite(F("> Enable Discovery</label></div>"));
  pageWrite(F("</div></div>"));

  // Save row
  pageWrite(F("<div class='form-inline' style='margin-top:10px'>"
                "<button class='btn' type='submit'>Save & Reboot</button>"
                "<a class='btn' href='/'>Back</a>"
              "</div></form>"));

  // Actions
  pageWrite(F("<div class='card'>"
              "<div class='row'>"
                "<button class='btn action' onclick=\"fetch('/api/global_req',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'do=req_all_levels'})\">Request All Levels (All Areas)</button>"));
  pageWrite(F("<button class='btn action' onclick=\"fetch('/api/poll_all',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'do=start'}) .then(()=>alert('Area sweep started (2.."));
  pageWrite(String(defAr));
  pageWrite(F(")')).catch(()=>alert('Failed'))\">Poll Areas • Sweep 2.."));
  pageWrite(String(defAr));
  pageWrite(F("</button>"));
  pageWrite(F("</div></div>"));
    
  // Backup / Restore
  pageWrite(F("<div class='card'>"
                "<div class='row'>"
                  "<form method='GET' action='/backup'><button class='btn' type='submit'>Backup</button></form>"
                  "<form method='POST' action='/restore_backup' enctype='multipart/form-data'>"
                    "<input class='in' type='file' name='upload' accept='.json'/>"
                    "<button class='btn' type='submit'>Restore</button>"
                  "</form>"
                "</div>"
                "<div class='muted' style='margin-top:6px'>Backup bundles <b>config</b> and <b>entities</b> (area/channel types).</div>"
              "</div>"));

  // Wi‑Fi scan JS
  pageWrite(F("<script>"
    "function doScan(){"
    "  const sel=document.getElementById('ssidSelect');"
    "  const ss=document.getElementById('staSsid');"
    "  if(!sel||!ss) return;"
    "  sel.innerHTML='<option>Scanning…</option>'; sel.disabled=true;"
    "  const deadline=Date.now()+10000;"
    "  function fill(list){"
    "    list.sort((a,b)=>(b.rssi||-999)-(a.rssi||-999));"
    "    let opts=\"<option value=''> Select from scan </option>\";"
    "    for(const n of list){"
    "      const s=n.ssid||''; const r=n.rssi; const enc=(n.enc&&n.enc!==0)?' 🔒':'';"
    "      const label=(s.length?s:'(hidden)')+'  ['+r+' dBm]'+enc;"
    "      const val=s.replace(/\"/g,'&quot;');"
    "      opts+=\"<option value=\\\"\"+val+\"\\\">\"+label+\"</option>\";"
    "    }"
    "    sel.innerHTML=opts; sel.disabled=false; sel.onchange=()=>{if(sel.value) ss.value=sel.value;};"
    "  }"
    "  (function poll(){"
    "    fetch('/wifi_scan',{cache:'no-store'}).then(r=>r.json()).then(list=>{"
    "      if(Array.isArray(list)&&list.length>0){fill(list);}else{if(Date.now()<deadline){setTimeout(poll,600);}else{sel.innerHTML=\"<option value=''>No networks found</option>\";sel.disabled=false;}}"
    "    }).catch(()=>{sel.innerHTML=\"<option value=''>Scan failed — try again</option>\";sel.disabled=false;});"
    "  })();"
    "}"
  "</script>"));

  pageEnd();
}

void sendRebootingPage(const char* title, const char* msg, int seconds, int delayMs) {
  if (seconds < 0) seconds = 0;
  // Show a friendly page and auto-redirect to /
  pageBegin(title ? title : "Rebooting");
  pageWrite(F("<div class='card'>"));
    pageWrite(F("<div class='title'>"));
      pageWrite(title ? String(title) : String("Rebooting"));
    pageWrite(F("</div>"));

    pageWrite(F("<p>"));
      if (msg && *msg) pageWrite(String(msg));
      else             pageWrite(F("Device will restart to apply changes."));
    pageWrite(F("</p>"));

    pageWrite(F("<p>Redirecting to <code>/</code> in "));
      pageWrite(String(seconds));
      pageWrite(F("s…</p>"));

    pageWrite(F("<a class='btn' href='/'>Go now</a>"));
  pageWrite(F("</div>"));

  // Meta refresh
  pageWrite(String("<meta http-equiv='refresh' content='") + seconds + ";url=/'/>");

  pageEnd();

  // If you already call scheduleReboot() separately at call sites, do nothing here.
  // If you prefer this function to *also* schedule the reboot, uncomment the next line:
  // scheduleReboot(delayMs);
  (void)delayMs; // keep signature; caller may still use its own scheduleReboot()
}



static String normalizeVersion(String v) {
  v.trim();
  if (v.length() && (v[0] == 'v' || v[0] == 'V')) v.remove(0, 1);
  return v;
}

static int compareVersion(const String& aRaw, const String& bRaw) {
  String a = normalizeVersion(aRaw);
  String b = normalizeVersion(bRaw);
  int ai = 0, bi = 0;
  while (ai < (int)a.length() || bi < (int)b.length()) {
    long av = 0, bv = 0;
    while (ai < (int)a.length() && isDigit(a[ai])) { av = av * 10 + (a[ai++] - '0'); }
    while (bi < (int)b.length() && isDigit(b[bi])) { bv = bv * 10 + (b[bi++] - '0'); }
    if (av < bv) return -1;
    if (av > bv) return 1;
    while (ai < (int)a.length() && a[ai] != '.') ai++;
    while (bi < (int)b.length() && b[bi] != '.') bi++;
    if (ai < (int)a.length()) ai++;
    if (bi < (int)b.length()) bi++;
  }
  return 0;
}


// GitHub uses a DigiCert certificate chain. We embed DigiCert Global Root G2 (valid until 2038)
// plus ISRG Root X1 (Let's Encrypt, valid until 2035) as a fallback for GitHub's CDN/asset hosts.
//
// HEAP WARNING: BearSSL needs ~22 KB of free heap for the TLS handshake.
// Free heap is checked before attempting TLS so the error is reported clearly instead
// of a silent HTTP -1.
//
// IMPORTANT: certBundle is built lazily via a pointer so that append() never runs more
// than once — calling append() on a static X509List on every invocation would grow the
// list unboundedly and waste heap on repeated OTA checks.

static const char kGithubRootCA_ISRG_X1[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDExHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoBggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwK
dJzd91J2w+JR/S1+vNBe+BxzF8JtgJRMVMnMdJTCsB2OCF+d+kUUIDMKDyCpCRIS
A7I5w+g9nObOVnzZq7oAx6PYnz1gnL1bfcC8pfOH7/R4bANqN5TfFMpk9Oqfwz5i
6lsoAb/lhCfPAQ2Qm1NPXR2gGGFLpJLrHJMbPF+wFVbL93MKGSP6kPlRp7nccRFw
KyF0IOKnBdKGqJa6Qx8RMJGbBvzmHVnhKaUMKaL6lE2Y0A==
-----END CERTIFICATE-----
)EOF";

static const char kGithubRootCA_DigiCertG2[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ/5exGAnjSdBOiVjkNc5scal6+tqgw5LdmWQIDAQABo2MwYTAO
BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwHwYDVR0jBBgwFoAUTiJUIBiV5uNu5g/6+rkS7QYXjzkw
DQYJKoZIhvcNAQELBQADggEBAGnkInG525hsRHiTiTbEhebWBFN5yb+fH1B8RRR8
x6jHsapTqjFr02k+Hxo0+l1DkLWTVLU8GHzjqYu+AEhUv7cMU87J6lE9YxQ5aOy
xpFJMbxFiMNLJN7LJMF4iOoXIbj2oPuSV9/HSmxRaJhbQFPoB/0OxSnl/zCAGmOE
2V0E4EelVRY4cyvKNsIBHpMDT77wD0HZa1SknD5TdyXBJXtNV7KeKrXU5H6BQXCD
bHLFnI+dsMf4IAlqc4lqW2BirEOeWEL5Sj5b5MrLKqDnw3L3sguI7MpMoFJGiMI1
d7PQOGbGWYxqH8o9R7eSYvdAd4uNjI23KzwBmH8=
-----END CERTIFICATE-----
)EOF";

// ---- OTA progress state ----
// Prefixed FWOTA_ to avoid clash with ArduinoOTA.h's ota_state_t (OTA_IDLE etc.)
enum FwOtaPhase : uint8_t { FWOTA_IDLE=0, FWOTA_RESOLVING, FWOTA_DOWNLOADING, FWOTA_SUCCESS, FWOTA_FAILED };
static FwOtaPhase gOtaPhase  = FWOTA_IDLE;
static String     gOtaError;

// ---- Deferred OTA: handler stores the URL, main loop does the download ----
// Running the TLS download inside the HTTP handler means web-server buffers +
// BearSSL buffers compete for heap simultaneously → fragmentation crash.
// By returning from the handler first, the web server releases its request
// buffers before BearSSL allocates its own, giving a clean contiguous block.
static bool   gOtaPending        = false;
static String gOtaFinalUrl;
static String gOtaCurrentVersion;

// ── TLS helpers ────────────────────────────────────────────────────────────
// BearSSL buffer strategy for low-heap ESP8266:
//
//  Tier 1 (>16KB free): MFLN 1024 + verified cert  → ~6KB buffers, full security
//  Tier 2 ( >8KB free): MFLN 512  + setInsecure()  → ~3KB buffers, no cert check
//  Tier 3 (<8KB free) : abort with a clear message
//
// On ESP8266 we use setInsecure() + MFLN (via setBufferSizes) to keep
// heap usage to ~4.5 KB for the TLS session.  Certificate verification is
// skipped because the binary is public and unsigned anyway.
// On ESP32 heap is not a concern so we always verify.

// Cross-platform "largest contiguous free heap block" helper.
// ESP8266: getMaxFreeBlockSize()  |  ESP32: getMaxAllocHeap()
static inline uint32_t maxFreeBlock() {
#if defined(ESP8266)
  return ESP.getMaxFreeBlockSize();
#else
  return ESP.getMaxAllocHeap();
#endif
}

static void configureGithubTls(WiFiClientSecure& client) {
  client.setTimeout(15000);
#if defined(ESP8266)
  // setInsecure() skips X509List allocation (~1.5 KB saved).
  // setBufferSizes(recv=1024, xmit=512): BearSSL automatically advertises
  // MFLN=1024 in ClientHello when recv < 16384. GitHub supports MFLN and
  // will send all records in <=1024-byte chunks. No probe needed.
  client.setInsecure();
  client.setBufferSizes(1024, 512);
#else
  static const String combinedCA = String(kGithubRootCA_DigiCertG2) + String(kGithubRootCA_ISRG_X1);
  client.setCACert(combinedCA.c_str());
#endif
}

static bool checkHeapForTls(String& outErr) {
#if defined(ESP8266)
  // Use getMaxFreeBlockSize(): total free heap can look fine while fragmentation
  // means no single block is large enough for the required allocations.
  // Sequence after this check:
  //   Update.begin()  → 4096 B sector buffer
  //   setBufferSizes(512,512) + BearSSL handshake → ~5500 B (state + I/O buffers)
  // Minimum contiguous block needed: 4096 + 5500 ≈ 11 KB.
  // This check runs AFTER logs_clear() + mqtt.disconnect() so the number
  // reflects the cleaned-up state, not the busy-device worst case.
  uint32_t maxBlock = maxFreeBlock();
  if (maxBlock < 11000) {
    outErr = String("Heap fragmented (largest free block ")
           + String(maxBlock)
           + " bytes, need 11 KB). Reboot and retry immediately after boot.";
    return false;
  }
#endif
  return true;
}

static bool fetchLatestReleaseInfo(String& outTag, String& outBinUrl, int& outSize, String& outErr) {
  if (WiFi.status() != WL_CONNECTED) {
    outErr = "Wi-Fi not connected. Connect STA Wi-Fi first, then retry.";
    return false;
  }
  // BearSSL TLS with MFLN+setInsecure needs ~4.5 KB — check before allocating.
  if (!checkHeapForTls(outErr)) return false;

  WiFiClientSecure client;
  configureGithubTls(client);
  HTTPClient http;
  http.setTimeout(15000);

  const char* apiUrl = "https://api.github.com/repos/hollako/Dynet-MQTT-Home-Assistant-Gateway/releases/latest";
  if (!http.begin(client, apiUrl)) {
    outErr = "HTTP begin failed for GitHub API.";
    return false;
  }
  http.addHeader("Accept", "application/vnd.github+json");
  http.addHeader("User-Agent", "dynet-gateway-ota");

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    outErr = String("GitHub API HTTP ") + code + ": " + HTTPClient::errorToString(code) + ".";
    if (code < 0)
      outErr += " Check DNS/internet access, firewall, or try again after reboot.";
    return false;
  }

  // Use an ArduinoJson filter so only tag_name + assets[].{name, size,
  // browser_download_url} are stored.  This reduces the document from 16 KB
  // to ~1.5 KB, which is the key fix that allows BearSSL (~22 KB) and the
  // JSON doc to coexist within ESP8266 heap.  The size field lets us call
  // Update.begin() before opening the download TLS session (see handleFwDoUpdate).
  StaticJsonDocument<256> filter;
  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["size"] = true;
  filter["assets"][0]["browser_download_url"] = true;

  DynamicJsonDocument doc(1536);
  DeserializationError jerr = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();

  if (jerr) {
    outErr = String("JSON parse error: ") + jerr.c_str();
    return false;
  }

  outTag = doc["tag_name"] | "";
  JsonArray assets = doc["assets"].as<JsonArray>();
  for (JsonVariant v : assets) {
    const char* name = v["name"] | "";
    const char* url  = v["browser_download_url"] | "";
    if (!name || !url || !String(name).endsWith(".bin")) continue;

    // Platform-aware selection: skip binaries built for the other platform.
    // Matches common naming patterns: ESP8266_xxx.bin / ESP32_xxx.bin
    String sName = String(name);
    sName.toLowerCase();
#if defined(ESP8266)
    if (sName.indexOf("esp32") >= 0) {
      LOGF("[OTA] Skipping ESP32 asset: %s\n", name);
      continue;
    }
#else
    if (sName.indexOf("esp8266") >= 0 || sName.indexOf("8266") >= 0) {
      LOGF("[OTA] Skipping ESP8266 asset: %s\n", name);
      continue;
    }
#endif
    LOGF("[OTA] Selected asset: %s\n", name);
    outBinUrl = String(url);
    outSize   = v["size"] | 0;
    break;
  }

  if (!outTag.length())    { outErr = "No release tag found in API response.";         return false; }
  if (!outBinUrl.length()) { outErr = "No matching .bin asset found for this platform in latest release."; return false; }
  if (outSize <= 0)        { outErr = "Asset size is 0 or missing in API response.";   return false; }
  return true;
}

static String gPendingUpdateTag;
static String gPendingUpdateUrl;
static int    gPendingUpdateSize = 0;   // asset byte count from GitHub API

// Result of the last "Check for Update" — shown inline on /fw after redirect.
// 0 = idle/no result yet, 1 = up to date, 2 = error
static int    gCheckResult = 0;
static String gCheckError;


static bool hostReachable(const char* host, uint16_t port, uint32_t timeoutMs, String& outErr) {
  WiFiClient client;
  // Stream/WiFiClient timeout is in milliseconds; keep the caller-provided value.
  client.setTimeout(timeoutMs);
#if defined(ESP8266)
  bool ok = client.connect(host, port);
#else
  bool ok = client.connect(host, port, timeoutMs);
#endif
  if (!ok) {
    const int errNo = errno;
    outErr = String("TCP connect failed to ") + host + ":" + String(port);
    if (errNo != 0) outErr += String(" errno=") + String(errNo);
    return false;
  }
  client.stop();
  return true;
}

static bool hostReachableIp(const IPAddress& ip, uint16_t port, uint32_t timeoutMs, String& outErr) {
  WiFiClient client;
  // Stream/WiFiClient timeout is in milliseconds; keep the caller-provided value.
  client.setTimeout(timeoutMs);
#if defined(ESP8266)
  bool ok = client.connect(ip, port);
#else
  bool ok = client.connect(ip, port, timeoutMs);
#endif
  if (!ok) {
    const int errNo = errno;
    outErr = String("TCP connect failed to ") + ip.toString() + ":" + String(port);
    if (errNo != 0) outErr += String(" errno=") + String(errNo);
    return false;
  }
  client.stop();
  return true;
}

static bool resolveHost(const char* host, IPAddress& outIp, String& outErr) {
  if (WiFi.hostByName(host, outIp)) return true;
  outErr = String("DNS resolve failed for ") + host;
  return false;
}

static void handleNetCheck() {
  DynamicJsonDocument doc(3072);
  doc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
  doc["sta_ssid"] = WiFi.SSID();
  doc["sta_ip"] = WiFi.localIP().toString();
  doc["gateway"] = WiFi.gatewayIP().toString();
#if defined(ESP8266)
  doc["dns1"] = WiFi.dnsIP().toString();
#else
  doc["dns1"] = WiFi.dnsIP(0).toString();
  doc["dns2"] = WiFi.dnsIP(1).toString();
#endif

  String err;
  IPAddress ip;
  bool googleDns = resolveHost("google.com", ip, err);
  doc["dns_google_ok"] = googleDns;
  doc["dns_google_ip"] = googleDns ? ip.toString() : "";
  if (!googleDns) doc["dns_google_err"] = err;

  err = "";
  bool githubDns = resolveHost("api.github.com", ip, err);
  doc["dns_github_ok"] = githubDns;
  doc["dns_github_ip"] = githubDns ? ip.toString() : "";
  if (!githubDns) doc["dns_github_err"] = err;

  err = "";
  bool googleTcp = hostReachable("google.com", 443, 5000, err);
  doc["tcp_google_443_ok"] = googleTcp;
  if (!googleTcp) doc["tcp_google_443_err"] = err;

  err = "";
  bool githubTcp = hostReachable("api.github.com", 443, 5000, err);
  doc["tcp_github_443_ok"] = githubTcp;
  if (!githubTcp) doc["tcp_github_443_err"] = err;

  const String gateway = doc["gateway"].as<String>();
  err = "";
  bool gateway80 = hostReachable(gateway.c_str(), 80, 3000, err);
  doc["tcp_gateway_80_ok"] = gateway80;
  if (!gateway80) doc["tcp_gateway_80_err"] = err;

  err = "";
  bool gateway53 = hostReachable(gateway.c_str(), 53, 3000, err);
  doc["tcp_gateway_53_ok"] = gateway53;
  if (!gateway53) doc["tcp_gateway_53_err"] = err;

  err = "";
  bool githubIpTcp = hostReachable("140.82.121.5", 443, 5000, err);
  doc["tcp_github_ip_443_ok"] = githubIpTcp;
  if (!githubIpTcp) doc["tcp_github_ip_443_err"] = err;

  err = "";
  bool example80 = hostReachable("example.com", 80, 5000, err);
  doc["tcp_example_80_ok"] = example80;
  if (!example80) doc["tcp_example_80_err"] = err;

  err = "";
  IPAddress githubV4Ip;
  bool githubV4Dns = resolveHost("api.github.com", githubV4Ip, err);
  doc["dns_github_ipv4_ok"] = githubV4Dns;
  doc["dns_github_ipv4_ip"] = githubV4Dns ? githubV4Ip.toString() : "";
  if (!githubV4Dns) doc["dns_github_ipv4_err"] = err;

  bool githubV4Tcp = false;
  err = "";
  if (githubV4Dns) {
    githubV4Tcp = hostReachableIp(githubV4Ip, 443, 5000, err);
  } else {
    err = "Skipped IPv4-only TCP check because DNS resolution failed.";
  }
  doc["tcp_github_ipv4_443_ok"] = githubV4Tcp;
  if (!githubV4Tcp) doc["tcp_github_ipv4_443_err"] = err;

#if defined(ESP32) || defined(ESP8266)
  doc["free_heap"] = ESP.getFreeHeap();
#endif
  doc["open_sockets_info"] = "Runtime open socket count is not exposed by Arduino WiFiClient API.";

  String dnsGoogleErr = String(doc["dns_google_err"] | "");
  String dnsGithubErr = String(doc["dns_github_err"] | "");
  String tcpGoogleErr = String(doc["tcp_google_443_err"] | "");
  String tcpGithubErr = String(doc["tcp_github_443_err"] | "");
  String dnsGoogleSuffix = googleDns ? "" : String(" err=") + dnsGoogleErr;
  String dnsGithubSuffix = githubDns ? "" : String(" err=") + dnsGithubErr;
  String tcpGoogleSuffix = googleTcp ? "" : String(" err=") + tcpGoogleErr;
  String tcpGithubSuffix = githubTcp ? "" : String(" err=") + tcpGithubErr;
  String tcpGateway80Suffix = gateway80 ? "" : String(" err=") + String(doc["tcp_gateway_80_err"] | "");
  String tcpGateway53Suffix = gateway53 ? "" : String(" err=") + String(doc["tcp_gateway_53_err"] | "");
  String tcpGithubIpSuffix = githubIpTcp ? "" : String(" err=") + String(doc["tcp_github_ip_443_err"] | "");
  String tcpExample80Suffix = example80 ? "" : String(" err=") + String(doc["tcp_example_80_err"] | "");
  String tcpGithubV4Suffix = githubV4Tcp ? "" : String(" err=") + String(doc["tcp_github_ipv4_443_err"] | "");

  LOGF("[NETCHECK] wifi=%s ssid='%s' ip=%s gw=%s dns1=%s\n",
       doc["wifi_connected"].as<bool>() ? "ok" : "down",
       doc["sta_ssid"].as<const char*>(),
       doc["sta_ip"].as<const char*>(),
       doc["gateway"].as<const char*>(),
       doc["dns1"].as<const char*>());
  LOGF("[NETCHECK] dns google=%s ip=%s%s\n",
       googleDns ? "ok" : "fail",
       doc["dns_google_ip"].as<const char*>(),
       dnsGoogleSuffix.c_str());
  LOGF("[NETCHECK] dns github=%s ip=%s%s\n",
       githubDns ? "ok" : "fail",
       doc["dns_github_ip"].as<const char*>(),
       dnsGithubSuffix.c_str());
  LOGF("[NETCHECK] tcp google:443=%s%s\n",
       googleTcp ? "ok" : "fail",
       tcpGoogleSuffix.c_str());
  LOGF("[NETCHECK] tcp api.github.com:443=%s%s\n",
       githubTcp ? "ok" : "fail",
       tcpGithubSuffix.c_str());
  LOGF("[NETCHECK] tcp gateway:80=%s%s\n",
       gateway80 ? "ok" : "fail",
       tcpGateway80Suffix.c_str());
  LOGF("[NETCHECK] tcp gateway:53=%s%s\n",
       gateway53 ? "ok" : "fail",
       tcpGateway53Suffix.c_str());
  LOGF("[NETCHECK] tcp github-ip:443=%s%s\n",
       githubIpTcp ? "ok" : "fail",
       tcpGithubIpSuffix.c_str());
  LOGF("[NETCHECK] tcp example.com:80=%s%s\n",
       example80 ? "ok" : "fail",
       tcpExample80Suffix.c_str());
  LOGF("[NETCHECK] tcp api.github.com(v4):443=%s%s\n",
       githubV4Tcp ? "ok" : "fail",
       tcpGithubV4Suffix.c_str());

  server.sendHeader("Cache-Control", "no-store");
  String out;
  serializeJsonPretty(doc, out);
  server.send(200, "application/json", out);
}

static void handleFwGet() {
  pageBegin("Firmware Update");

  // Show the result of the last OTA attempt (if any).
  if (gOtaPhase == FWOTA_SUCCESS) {
    pageWrite(F("<div class='card' style='border-left:4px solid green'>"
                "<div class='title' style='color:green'>Update Successful</div>"
                "<p>Firmware updated successfully. Current version: <b>"));
    pageWrite(String(HA_SW_VERSION));
    pageWrite(F("</b></p></div>"));
    gOtaPhase = FWOTA_IDLE;
  } else if (gOtaPhase == FWOTA_FAILED) {
    pageWrite(F("<div class='card' style='border-left:4px solid red'>"
                "<div class='title' style='color:red'>Update Failed</div>"
                "<p>"));
    pageWrite(gOtaError.length() ? gOtaError : String("Unknown error"));
    pageWrite(F("</p></div>"));
    gOtaPhase = FWOTA_IDLE;
  }

  pageWrite(F("<div class='card'>"
              "<div class='title'>Manual Firmware Update</div>"));
  pageWrite(F("<p>Current release: <b>"));
  pageWrite(String(HA_SW_VERSION));
  if (gPendingUpdateTag.length() && gPendingUpdateUrl.length()) {
    pageWrite(F("</b> &nbsp;<span style='color:#c80'>&#8593; v"));
    pageWrite(gPendingUpdateTag);
    pageWrite(F(" available</span>"));
  } else {
    pageWrite(F("</b>"));
  }
  pageWrite(F("</p>"
              "<form method='POST' action='/fw' enctype='multipart/form-data' class='form-sec'>"
              "<div class='field'>"
                "<input type='file' name='fw' accept='.bin' required>"
              "</div>"
              "<div class='row' style='margin-top:10px'>"
                "<button type='submit' class='btn'>Update</button>"
              "</div>"
              "</form>"
              "<form method='POST' action='/fw/check' class='form-sec'>"
              "<div class='row' style='margin-top:10px'>"
                "<button type='submit' class='btn'>Check for Update</button>"
              "</div>"
              "</form>"
              ));

  // ── Inline check result ────────────────────────────────────────────────────
  if (gCheckResult == 1) {
    pageWrite(F("<p style='color:green;margin:8px 0'>&#10003; Firmware is up to date.</p>"));
    gCheckResult = 0;
  } else if (gCheckResult == 2) {
    pageWrite(F("<p style='color:red;margin:8px 0'>&#10007; Check failed: "));
    pageWrite(gCheckError);
    pageWrite(F("</p>"));
    gCheckResult = 0;
  }

  // ── Download / Install buttons when a newer version was found ─────────────
  if (gPendingUpdateTag.length() && gPendingUpdateUrl.length()) {
    pageWrite(F("<div class='row' style='margin-top:10px;gap:8px'>"));
    pageWrite(F("<a class='btn' target='_blank' rel='noopener' href='"));
    pageWrite(gPendingUpdateUrl);
    pageWrite(F("'>&#8595; Download v"));
    pageWrite(gPendingUpdateTag);
    pageWrite(F("</a>"));
#if !defined(ESP8266)
    pageWrite(F("<button class='btn' onclick='doUpd()'>&#8679; Install Update</button>"));
#endif
    pageWrite(F("</div>"));
#if !defined(ESP8266)
    // ESP32 SSE progress + JS — only injected when an update is pending.
    pageWrite(F("<div id='prg' style='display:none;margin-top:10px'>"
                "<table style='border-collapse:collapse;width:100%;margin-top:8px'>"
                "<tr><td id='p1' style='padding:4px 0'>[ ] Connecting to download server</td></tr>"
                "<tr><td id='p2' style='padding:4px 0;color:#aaa'>[ ] Downloading firmware</td></tr>"
                "<tr><td id='p3' style='padding:4px 0;color:#aaa'>[ ] Flashing &amp; verifying</td></tr>"
                "</table>"
                "<div id='pres' style='margin-top:10px'></div>"
                "</div>"
                "<script>"
                "var lastPhase='';"
                "function doUpd(){"
                  "document.querySelector('button[onclick]').disabled=true;"
                  "document.getElementById('prg').style.display='block';"
                  "document.getElementById('p1').textContent='>>> Connecting to download server...';"
                  "fetch('/fw/update',{method:'POST'})"
                  ".then(function(r){"
                    "var rd=r.body.getReader(),dc=new TextDecoder(),buf='';"
                    "function pump(){"
                      "rd.read().then(function(x){"
                        "if(x.done){"
                          "if(lastPhase==='downloading'){"
                            "document.getElementById('p2').textContent='[..] Downloading & flashing (please wait)...';"
                            "document.getElementById('pres').innerHTML='<p>Device is writing firmware. Will reboot automatically.<br><b>This page will redirect in 90 seconds.</b></p>';"
                            "setTimeout(function(){location.href='/fw';},90000);"
                          "}"
                          "return;"
                        "}"
                        "buf+=dc.decode(x.value,{stream:true});"
                        "var parts=buf.split('\\n\\n');"
                        "buf=parts.pop();"
                        "parts.forEach(function(p){"
                          "if(p.slice(0,6)==='data: '){"
                            "try{onEvt(JSON.parse(p.slice(6)));}catch(e){}"
                          "}"
                        "});"
                        "pump();"
                      "});"
                    "}"
                    "pump();"
                  "})"
                  ".catch(function(e){"
                    "document.getElementById('pres').innerHTML='<span style=color:red>Connection error: '+e+'</span>';"
                  "});"
                "}"
                "function onEvt(d){"
                  "var p=d.phase;lastPhase=p;"
                  "if(p==='resolving'){"
                    "document.getElementById('p1').textContent='>>> Connecting...';"
                  "}else if(p==='downloading'){"
                    "document.getElementById('p1').textContent='[OK] Connected';"
                    "document.getElementById('p1').style.color='green';"
                    "document.getElementById('p2').textContent='>>> Downloading firmware...';"
                    "document.getElementById('p2').style.color='';"
                  "}else if(p==='success'){"
                    "document.getElementById('p2').textContent='[OK] Downloaded';"
                    "document.getElementById('p2').style.color='green';"
                    "document.getElementById('p3').textContent='[OK] Flash complete!';"
                    "document.getElementById('p3').style.color='green';"
                    "document.getElementById('pres').innerHTML='<b>Update successful!</b> Rebooting&hellip; Redirecting in 15s.';"
                    "setTimeout(function(){location.href='/';},15000);"
                  "}else if(p==='failed'){"
                    "document.getElementById('pres').innerHTML='<span style=color:red><b>Failed:</b> '+d.error+'</span>';"
                  "}"
                "}"
                "</script>"));
#endif
  }
  pageWrite(F(
              "<div class='row' style='margin-top:10px'>"
                "<a class='btn' href='/netcheck' target='_blank' rel='noopener'>Run Internet Check</a>"
              "</div>"
              "<p style='opacity:.7'>Do not power off during update. Device will reboot automatically.</p>"
              "</div>"));

  pageEnd();
}

static void handleFwCheckUpdate() {
  String latestTag, binUrl, err;
  int    binSize = 0;

  if (!fetchLatestReleaseInfo(latestTag, binUrl, binSize, err)) {
    gCheckResult       = 2;
    gCheckError        = err;
    gPendingUpdateTag  = "";
    gPendingUpdateUrl  = "";
    gPendingUpdateSize = 0;
  } else {
    String current = normalizeVersion(String(HA_SW_VERSION));
    String latest  = normalizeVersion(latestTag);
    if (compareVersion(latest, current) <= 0) {
      gCheckResult       = 1;   // up to date
      gPendingUpdateTag  = "";
      gPendingUpdateUrl  = "";
      gPendingUpdateSize = 0;
    } else {
      gCheckResult       = 0;   // update found — shown via gPendingUpdateTag
      gPendingUpdateTag  = latest;
      gPendingUpdateUrl  = binUrl;
      gPendingUpdateSize = binSize;
    }
  }

  server.sendHeader("Location", "/fw");
  server.send(303, "text/plain", "");
}

static void handleFwDoUpdate() {
#if defined(ESP8266)
  // Remote OTA is not available on ESP8266 — insufficient RAM for BearSSL TLS
  // plus the 4 KB flash sector buffer simultaneously.  The Check for Updates
  // page already shows a direct download link and an Upload Firmware button
  // instead of an Install button, so this handler should never normally be
  // reached on ESP8266.  Return a graceful page just in case.
  pageBegin("Firmware Update");
  pageWrite(F("<div class='card'><div class='title'>Not Supported on ESP8266</div>"
              "<p>Remote firmware installation is not available on this hardware.</p>"
              "<p>Please download the firmware file and install it manually:</p>"
              "<div class='row' style='gap:8px;margin-top:12px'>"));
  if (gPendingUpdateUrl.length()) {
    pageWrite(F("<a class='btn' target='_blank' rel='noopener' href='"));
    pageWrite(gPendingUpdateUrl);
    pageWrite(F("'>&#8595; Download .bin</a>"));
  }
  pageWrite(F("<a class='btn' href='/fw'>&#8679; Upload Firmware File</a>"
              "</div></div>"));
  pageEnd();
  return;

#else
  // ESP32 — full remote OTA via background task + SSE progress feed.

  if (!gPendingUpdateUrl.length() || !gPendingUpdateTag.length()) {
    pageBegin("Firmware Update");
    pageWrite(F("<div class='card'><div class='title'>No Pending Update</div>"
                "<p>Run <b>Check for Update</b> first.</p><a class='btn' href='/fw'>Back</a></div>"));
    pageEnd();
    return;
  }

  gOtaPhase = FWOTA_IDLE;
  gOtaError = "";

  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "text/event-stream", "data: {\"phase\":\"downloading\"}\n\n");

  {
    WiFiClient cl = server.client();
    if (cl) cl.stop();
  }

  String current = normalizeVersion(String(HA_SW_VERSION));
  gOtaFinalUrl       = gPendingUpdateUrl;
  gOtaCurrentVersion = current;
  gOtaPending        = true;
#endif
}

static void handleFwUpload() {
  HTTPUpload& up = server.upload();

  static size_t total = 0;
  static bool hadError = false;

  switch (up.status) {
    case UPLOAD_FILE_START: {
      total = 0;
      hadError = false;

#if defined(ESP8266)
      uint32_t maxSketch = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(maxSketch)) hadError = true;
#else
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) hadError = true;
#endif
    } break;

    case UPLOAD_FILE_WRITE: {
      if (!hadError) {
        size_t written = Update.write(up.buf, up.currentSize);
        if (written != up.currentSize) hadError = true;
        total += written;
      }
    } break;

    case UPLOAD_FILE_END: {
      if (!hadError) {
        if (!Update.end(true)) hadError = true;
      }
      if (hadError || Update.hasError()) {
        UPDATE_ABORT();
        pageBegin("Firmware Update");
        pageWrite(F("<div class='card'><div class='title'>Update Failed</div>"
                    "<p>There was an error applying the firmware.</p>"
                    "<a class='btn' href='/fw'>Try Again</a></div>"));
        pageEnd();
      } else {
        sendRebootingPage("Update Successful",
                          (String("Flashed ") + total + " bytes. Rebooting...").c_str(),
                          10, 1200);
        scheduleReboot(1200);
      }
    } break;

    case UPLOAD_FILE_ABORTED: {
      UPDATE_ABORT();
      pageBegin("Firmware Update");
      pageWrite(F("<div class='card'><div class='title'>Upload Aborted</div>"
                  "<a class='btn' href='/fw'>Back</a></div>"));
      pageEnd();
    } break;

    default: break;
  }
}


void handleConfigPost() {
  if (server.hasArg("device_id")) { deviceId = server.arg("device_id"); eepromSaveDeviceId(deviceId); }
  if (server.hasArg("wifi_ssid")) setStr(cfg.wifi_ssid, sizeof(cfg.wifi_ssid), server.arg("wifi_ssid"));
  if (server.hasArg("wifi_pass")) setStr(cfg.wifi_pass, sizeof(cfg.wifi_pass), server.arg("wifi_pass"));
  if (server.hasArg("mqtt_server")) setStr(cfg.mqtt_server, sizeof(cfg.mqtt_server), server.arg("mqtt_server"));
  if (server.hasArg("mqtt_port"))   cfg.mqtt_port = server.arg("mqtt_port").toInt();
  if (server.hasArg("mqtt_user"))   setStr(cfg.mqtt_user, sizeof(cfg.mqtt_user), server.arg("mqtt_user"));
  if (server.hasArg("mqtt_pass"))   setStr(cfg.mqtt_pass, sizeof(cfg.mqtt_pass), server.arg("mqtt_pass"));
  if (server.hasArg("ap_ssid"))     apSsid = server.arg("ap_ssid");
  if (server.hasArg("ap_pass"))     apPass = server.arg("ap_pass");
  cfg.ha_discovery  = server.hasArg("ha_discovery");
  if (server.hasArg("tx_pin"))  { txPin  = sanitizeGpio(server.arg("tx_pin").toInt()); }
  if (server.hasArg("rx_pin"))  { rxPin  = sanitizeGpio(server.arg("rx_pin").toInt()); }
  if (server.hasArg("de_pin"))  { dePin  = sanitizeGpio(server.arg("de_pin").toInt()); }
  if (server.hasArg("led_pin")) { ledPin = sanitizeLedGpio(server.arg("led_pin").toInt()); }
  if (server.hasArg("btn_pin")) { buttonPin = sanitizeButtonGpio(server.arg("btn_pin").toInt()); }
  ledActiveLow    = server.hasArg("led_invert");
  buttonActiveLow = server.hasArg("btn_invert");

  if (server.hasArg("dynet_max_channels")) {
  int v = server.arg("dynet_max_channels").toInt();
  if (v < 1) v = 1;
  if (v > (int)DYNET_MAX_CHANNELS) v = DYNET_MAX_CHANNELS;
  cfg.dynet_max_channels = (uint8_t)v;
}
if (server.hasArg("dynet_max_areas")) {
  int v = server.arg("dynet_max_areas").toInt();
  if (v < 1) v = 1;
  if (v > (int)DYNET_MAX_AREAS) v = DYNET_MAX_AREAS;
  cfg.dynet_max_areas = (uint8_t)v;
}

  saveConfig();

  sendRebootingPage("Saved",
                    "If the page doesn’t return, reconnect via the new IP or AP 192.168.4.1.",
                    10, 5000);
  if (server.client()) server.client().flush();
  delay(150);
  scheduleReboot(2200);
}

// -------------- Backup/Restore --------------
static const char* ENTITIES_FILE = "/entities.json";
static const char* RESTORE_TMP   = "/.restore.tmp";



void handleRestoreBackupUpload() {
  HTTPUpload& up = server.upload();
  static File uf;
  if (up.status == UPLOAD_FILE_START) { if (LittleFS.exists(RESTORE_TMP)) LittleFS.remove(RESTORE_TMP); uf = LittleFS.open(RESTORE_TMP, "w"); }
  else if (up.status == UPLOAD_FILE_WRITE) { if (uf) uf.write(up.buf, up.currentSize); }
  else if (up.status == UPLOAD_FILE_END) { if (uf) uf.close(); }
}

void handleRestoreBackupPost() {
  if (!LittleFS.exists(RESTORE_TMP)) { server.send(400, "text/plain", "no file uploaded"); return; }
  String body;
  {
    File tf = LittleFS.open(RESTORE_TMP, "r");
    while (tf && tf.available()) body += char(tf.read());
    tf.close();
    LittleFS.remove(RESTORE_TMP);
  }
  body.trim();
  if (!body.length()) { server.send(400, "text/plain", "empty file"); return; }

  // extract "config" and "entities" objects if present
  String cfgObj, entObj;
  bool hasCfg = extractTopObject(body, "config", cfgObj);
  bool hasEnt = extractTopObject(body, "entities", entObj);

  auto writeText = [&](const char* path, const String& txt)->bool{
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.print(txt);
    f.flush();
    f.close();
    return true;
  };

  bool wroteCfg=false, wroteEnt=false;
  if (hasCfg) wroteCfg = writeText(CONFIG_FILE, cfgObj);
  if (hasEnt) wroteEnt = writeText(ENTITIES_FILE, entObj);
  if (!wroteCfg && !wroteEnt) {
    // try best-effort: maybe whole file is a config.json
    wroteCfg = writeText(CONFIG_FILE, body);
  }

  loadConfig();
  loadEntities();  

  // Reconnect MQTT & republish discovery
  if (mqtt.connected()) mqtt.disconnect();
  delay(50);
  mqttEnsureConnected();
  rediscoveryScheduled = true; rediscoveryPtr = 0; nextRediscoveryAt = millis() + 300;

  sendRebootingPage("Restore complete","Settings applied. The device will come back online shortly.",10,5000);
  if (server.client()) { server.client().flush(); }
  delay(150);
  scheduleReboot(2200);
}

// ---------- deferred OTA download (runs in main loop) ----------
// On ESP8266 this is a no-op: OTA runs directly inside handleFwDoUpdate().
// On ESP32 this handles the deferred download triggered by that handler.
void webOtaLoop() {
#if defined(ESP8266)
  // Nothing to do — ESP8266 OTA is done synchronously in the HTTP handler.
  (void)gOtaPending;
  return;
#else
  if (!gOtaPending) return;
  gOtaPending = false;

  LOGF("[OTA] === Deferred download starting (ESP32) ===\n");
  LOGF("[OTA] URL: %s\n", gOtaFinalUrl.c_str());
  LOGF("[OTA] Current ver: %s\n", gOtaCurrentVersion.c_str());

  WiFiClientSecure secureClient;
  secureClient.setTimeout(90000);
  {
    static const String ca = String(kGithubRootCA_DigiCertG2) + String(kGithubRootCA_ISRG_X1);
    secureClient.setCACert(ca.c_str());
  }
  LOGF("[OTA] Calling httpUpdate.update()...\n");
  httpUpdate.rebootOnUpdate(false);
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  t_httpUpdate_return result = httpUpdate.update(secureClient, gOtaFinalUrl, gOtaCurrentVersion);
  LOGF("[OTA] update() returned %d\n", (int)result);

  if (result == HTTP_UPDATE_OK) {
    gOtaPhase = FWOTA_SUCCESS;
    LOGLN("[OTA] Flash OK — rebooting");
    scheduleReboot(1500);
  } else {
    gOtaError = httpUpdate.getLastErrorString()
              + " (err " + String(httpUpdate.getLastError()) + ")";
    gOtaPhase = FWOTA_FAILED;
    LOGF("[OTA] FAILED: %s\n", gOtaError.c_str());
  }

  gOtaFinalUrl       = String();
  gOtaCurrentVersion = String();
#endif
}

// ---------- firmware upgrade from Web ----------
  void registerFwRoutes() {
    // GET: show form
    server.on("/fw", HTTP_GET, handleFwGet);
    server.on("/fw/check", HTTP_POST, handleFwCheckUpdate);
    server.on("/fw/update", HTTP_POST, handleFwDoUpdate);
    server.on("/netcheck", HTTP_GET, handleNetCheck);

    // POST: upload (note the "upload handler" as 2nd lambda/func)
    server.on("/fw", HTTP_POST,
      [](){ /* finalizer; response is sent from handleFwUpload() */ },
      handleFwUpload
    );
  }
  

// ---------- reboot page (non-blocking) ----------
void sendRebootingPage(const String& title, const String& detail, int countdownSec, uint32_t startPollDelayMs) {
  String html =
    String(F("<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<style>body{font-family:system-ui;margin:24px}</style>"))
    + "<h2>"+title+" &#10003;</h2>"
    + "<p>"+detail+"</p>"
    + "<p>Rebooting… <b><span id='s'>"+String(countdownSec)+"</span>s</b></p>"
    + "<script>var s="+String(countdownSec)+";function t(){var e=document.getElementById('s');if(e)e.textContent=s;if(s>0){s--;setTimeout(t,900);}}"
      "function p(){fetch('/status',{cache:'no-store'}).then(r=>r.json()).then(()=>location.replace('/')).catch(()=>setTimeout(p,700));}"
      "t();setTimeout(p,"+String(startPollDelayMs)+");</script>";
  server.sendHeader("Cache-Control","no-store");
  server.send(200, "text/html; charset=utf-8", html);
}

// -------------- AP-Portal (quick setup) --------------
void handleApPortalGet() {
  String s;
  s.reserve(3000);
  s +=
  "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>ESP DyNet • Setup</title>"
  "<style>"
  "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif;margin:14px;line-height:1.35}"
  ".card{border:1px solid #ddd;border-radius:10px;padding:12px;margin:10px 0}"
  "label{display:block;margin:6px 0 2px;font-size:13px;color:#333}"
  "input,select,button{padding:8px;border:1px solid #ccc;border-radius:8px}"
  ".row{display:grid;grid-template-columns:1fr 1fr;gap:8px}"
  ".btn{cursor:pointer;background:#fafafa}"
  ".muted{color:#666;font-size:12px}"
  "</style>";

  s += "<h1>ESP DyNet • Setup</h1>";
  s += "<div class='card'><div class='muted'>Device is in Access Point mode. Connect to Wi‑Fi below or restore a backup.</div></div>";

  // Wi-Fi form
  s += "<div class='card'><h3>Wi‑Fi</h3>"
       "<form method='POST' action='/ap_portal_config'>"
       "<label>SSID</label><input name='wifi_ssid' type='text' value=''>"
       "<label>Password</label><input name='wifi_pass' type='password' value=''>"
       "<div style='margin-top:8px;display:flex;gap:8px;flex-wrap:wrap'>"
       "<button class='btn' type='submit'>Save & Connect</button>"
       "<button class='btn' type='button' onclick='scanW()'>Scan</button>"
       "<select id='nets' style='min-width:220px'></select>"
       "</div>"
       "</form>"
       "<div class='muted' style='margin-top:6px'>Tip: tap <b>Scan</b>, pick a network, then press <b>Save & Connect</b>.</div>"
       "</div>";

  // Restore
  s += "<div class='card'><h3>Restore Backup</h3>"
       "<form method='POST' action='/restore_backup' enctype='multipart/form-data'>"
       "<input type='file' name='upload' accept='.json'> "
       "<button class='btn' type='submit'>Restore</button>"
       "</form>"
       "<div class='muted' style='margin-top:6px'>Uploads a JSON created via <b>Backup</b>.</div>"
       "</div>";

  // Scan script
  s += "<script>"
        "function scanW(){"
        "  const sel=document.getElementById('nets');"
        "  const ss=document.querySelector(\"input[name='wifi_ssid']\");"
        "  if(!sel||!ss) return;"
        "  sel.innerHTML='<option>Scanning…</option>'; sel.disabled=true;"
        "  const deadline=Date.now()+10000;"
        "  function fill(list){"
        "    let opts='<option value=\"\">Select from scan</option>';"
        "    list.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{"
        "      const s=n.ssid||'(hidden)'; const lock=(n.enc&&n.enc!==0)?' 🔒':'';"
        "      opts += '<option value=\"'+(n.ssid||'')+'\">'+s+'  ['+n.rssi+' dBm]'+lock+'</option>';"
        "    });"
        "    sel.innerHTML=opts; sel.disabled=false; sel.onchange=function(){ ss.value=sel.value; };"
        "  }"
        "  (function poll(){"
        "    fetch('/wifi_scan',{cache:'no-store'})"
        "      .then(r=>r.json()).then(list=>{"
        "        if(Array.isArray(list)&&list.length>0){fill(list);}else if(Date.now()<deadline){setTimeout(poll,600);}else{sel.innerHTML='<option>No networks found</option>'; sel.disabled=false;}"
        "      }).catch(()=>{sel.innerHTML='<option>Scan failed — try again</option>'; sel.disabled=false;});"
        "  })();"
        "}"
       "</script>";

  server.send(200, "text/html; charset=utf-8", s);
}

void handleApPortalConfigPost() {
  if (server.hasArg("wifi_ssid")) setStr(cfg.wifi_ssid, sizeof(cfg.wifi_ssid), server.arg("wifi_ssid"));
  if (server.hasArg("wifi_pass")) setStr(cfg.wifi_pass, sizeof(cfg.wifi_pass), server.arg("wifi_pass"));
  saveConfig();

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(cfg.wifi_ssid, cfg.wifi_pass);

  unsigned long t0 = millis();
  bool ok = false;
  while (millis() - t0 < 12000) {
    if (WiFi.status() == WL_CONNECTED) { ok = true; break; }
    delay(150);
  }
  String detail;
  if (ok) {
    String ip = WiFi.localIP().toString();
    detail = String("Wi‑Fi connected.<br>STA IP: <b>") + ip + "</b><br>"
            "After reboot, open: <a href='http://" + ip + "/'>http://" + ip + "/</a>";
  } else {
    detail = "Settings saved. Rebooting…<br>"
            "If STA fails, reconnect to AP at <b>http://192.168.4.1/</b>";
  }

  // Show the standard rebooting page (it will auto‑poll /status and return to UI)
  sendRebootingPage(
    "Saved",
    detail,
    10,    // countdown seconds
    5000   // wait before first /status probe
  );
  if (server.client()) { server.client().flush(); }
  delay(150);
  scheduleReboot(2200);
}

// -------------- Routes --------------
void registerWebRoutes() {

  dynet.begin();
  // Note: loadConfig() + loadEntities() already called in setup() before registerWebRoutes().
  // Do NOT call them again here — it would double-allocate EntityManager arrays.
  registerFwRoutes();
  
  // Root/AP portal
  server.on("/", HTTP_GET, [](){
    if (isApPortalMode()) handleApPortalGet();
    else                  handleRootGet();
  });

  server.on("/config",  HTTP_GET, handleConfigGet);
  server.on("/config",  HTTP_POST, handleConfigPost);
  server.on("/reboot", HTTP_GET, [](){ sendRebootingPage("Reboot requested","",10,2000); scheduleReboot(800); });

  // Channel command API
  server.on("/api/cmd", HTTP_POST, [](){
    if (!server.hasArg("area") || !server.hasArg("ch") || !server.hasArg("cmd")) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    uint8_t area = (uint8_t)server.arg("area").toInt();
    uint8_t ch   = (uint8_t)server.arg("ch").toInt();
    String  cmd  = server.arg("cmd");

    if      (cmd == "ON")  { dynet.sendFadeToLevel_1s(area, ch, 100, 0x02); dynet.scheduleLevelReq(area, ch, 400); }
    else if (cmd == "OFF") { dynet.sendFadeToLevel_1s(area, ch,   0, 0x02); dynet.scheduleLevelReq(area, ch, 400); }
    else if (cmd == "REQ") dynet.sendRequestChannelLevel(area, ch);
    else if (cmd.startsWith("SET=")) {
      uint8_t pct = (uint8_t)cmd.substring(4).toInt();
      dynet.sendFadeToLevel_1s(area, ch, pct, 0x02);
      dynet.scheduleLevelReq(area, ch, 400);
    }
    else if (cmd == "CURTAIN_OPEN")  { using namespace DynetEntities; em.commandCurtain(area, ch, "OPEN");  }
    else if (cmd == "CURTAIN_CLOSE") { using namespace DynetEntities; em.commandCurtain(area, ch, "CLOSE"); }
    else if (cmd == "CURTAIN_STOP")  { using namespace DynetEntities; em.commandCurtain(area, ch, "STOP");  }
    else { server.send(400, "application/json", "{\"ok\":false}"); return; }

    server.send(200, "application/json", "{\"ok\":true}");
  });

  // Area preset API: POST area=<n>&preset=<p>
  server.on("/api/area_preset", HTTP_POST, [](){
    // Require params
    if (!server.hasArg("area") || !server.hasArg("preset")) {
      server.send(400, "application/json", "{\"ok\":false,\"err\":\"missing params\"}");
      return;
    }
    uint8_t  area   = (uint8_t)server.arg("area").toInt();     // 1..255
    uint8_t  preset = (uint8_t)server.arg("preset").toInt();   // 1..255 (you mapped to 0‑based internally)
    uint16_t fade   = server.hasArg("fade") ? (uint16_t)server.arg("fade").toInt() : 0;

    // Debug prints help a ton
    LOGF("[DyNet] /api/area_preset area=%u preset=%u fade=%u\n", area, preset, fade);

    // Fire it (your send fn already accepts 1‑based preset)
    dynet.sendAreaPreset(area, preset, fade);
    // Schedule level refresh after the preset fade completes
    dynet.scheduleAreaLevelReqs(area, (uint32_t)fade + 500);

    server.sendHeader("Cache-Control","no-store");
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // Live Areas JSON (preset/temp/setpoint for UI polling)
server.on("/areas_status", HTTP_GET, [](){
  using namespace DynetEntities;
  DynamicJsonDocument d(4096);
  JsonArray arr = d.createNestedArray("areas");
  for (int i = 0; i < em.areasCount(); i++) {
    const auto& as = em.areaAt(i);
    if (!as.present) continue;
    JsonObject o = arr.createNestedObject();
    o["area"]     = as.area;
    o["preset0"]  = as.preset0;     // 0xFF if unknown
    o["hasTemp"]  = as.hasTemp;
    if (as.hasTemp && !isnan(as.tempC))   o["tempC"]  = as.tempC;
    o["hasSetpt"] = as.hasSetpt;
    if (as.hasSetpt && !isnan(as.setptC)) o["setptC"] = as.setptC;
    if (as.areaType == DynetEntities::AREA_HVAC && as.hvac && as.hvac->currentMode[0])
      o["hvacMode"] = as.hvac->currentMode;
  }
  String out; serializeJson(d, out);
  server.sendHeader("Cache-Control","no-store");
  server.send(200, "application/json", out);
});

  // One-shot poll: force a single dynetPollAreas() sweep
  server.on("/api/poll_all", HTTP_POST, [](){
    areasSweepActive  = true;
    areasSweepArea    = 2;          // start from Area 2
    areasSweepChannel = 0;
    areasSweepPass    = 2;          // 3 total passes (2 extra after first)
    areasSweepNextAt  = millis() + 50;
    LOGF("[DyNet] sweep START (3 passes) requested from WebUI\n");
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // Update preset count — per-area, saves to entities and republishes HA discovery for that area
  server.on("/api/set_preset_count", HTTP_POST, [](){
    using namespace DynetEntities;
    if (!server.hasArg("n") || !server.hasArg("area")) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    uint8_t area = (uint8_t)server.arg("area").toInt();
    uint8_t n    = (uint8_t)constrain(server.arg("n").toInt(), 1, 128);
    int ai = em.findArea(area);
    if (ai < 0) { server.send(404, "application/json", "{\"ok\":false,\"error\":\"area not found\"}"); return; }
    em.areaAtMut(ai).presetCount = n;
    saveEntities();
    publishHADiscoveryForArea(area);  // republish only this area's preset select
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // Set curtain travel time
  server.on("/api/set_curtain_time", HTTP_POST, [](){
    using namespace DynetEntities;
    if (!server.hasArg("area") || !server.hasArg("ch") || !server.hasArg("t")) {
      server.send(400, "application/json", "{\"ok\":false}"); return;
    }
    uint8_t area = (uint8_t)server.arg("area").toInt();
    uint8_t ch0  = (uint8_t)server.arg("ch").toInt();
    uint8_t secs = (uint8_t)constrain(server.arg("t").toInt(), 1, 255);
    em.setCurtainTime(area, ch0, secs);
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // Manual add / delete channel
  // Set channel / area name
  server.on("/api/set_name", HTTP_POST, [](){
    using namespace DynetEntities;
    String type = server.arg("type");
    uint8_t area = (uint8_t)server.arg("area").toInt();
    String  name = server.arg("name");
    name.trim();
    if (name.length() == 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"name cannot be empty\"}"); return; }
    if (name.length() > 40) name = name.substring(0, 40);
    if (type == "ch") {
      uint8_t ch0 = (uint8_t)server.arg("ch").toInt();
      em.setChannelName(area, ch0, name.c_str());
    } else if (type == "area") {
      em.setAreaName(area, name.c_str());
    } else {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid type\"}"); return;
    }
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/add_channel", HTTP_POST, [](){
    using namespace DynetEntities;
    if (!server.hasArg("area") || !server.hasArg("ch")) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing area/ch\"}"); return;
    }
    uint8_t area = (uint8_t)server.arg("area").toInt();
    uint8_t ch0  = (uint8_t)server.arg("ch").toInt();
    if (area < 1 || ch0 > 254) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid area/ch\"}"); return;
    }
    int idx = em.touchChannel(area, ch0);
    if (idx < 0) { server.send(200, "application/json", "{\"ok\":false,\"error\":\"capacity full\"}"); return; }
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/del_channel", HTTP_POST, [](){
    using namespace DynetEntities;
    if (!server.hasArg("area") || !server.hasArg("ch")) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing area/ch\"}"); return;
    }
    uint8_t area = (uint8_t)server.arg("area").toInt();
    uint8_t ch0  = (uint8_t)server.arg("ch").toInt();
    bool ok = em.deleteChannel(area, ch0);
    server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"not found\"}");
  });

  server.on("/api/add_area", HTTP_POST, [](){
    using namespace DynetEntities;
    if (!server.hasArg("area")) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing area\"}"); return;
    }
    uint8_t area = (uint8_t)server.arg("area").toInt();
    if (area < 1) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid area\"}"); return;
    }
    int idx = em.touchArea(area);
    if (idx < 0) { server.send(200, "application/json", "{\"ok\":false,\"error\":\"capacity full\"}"); return; }
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/del_area", HTTP_POST, [](){
    using namespace DynetEntities;
    if (!server.hasArg("area")) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing area\"}"); return;
    }
    uint8_t area = (uint8_t)server.arg("area").toInt();
    bool ok = em.deleteArea(area);
    server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"not found\"}");
  });

  // ---- HVAC mode/fan management ----
  server.on("/api/hvac/add_mode", HTTP_POST, [](){
    using namespace DynetEntities;
    if (!server.hasArg("area")||!server.hasArg("name")||!server.hasArg("preset")){server.send(400,"application/json","{\"ok\":false}");return;}
    uint8_t area   = (uint8_t)server.arg("area").toInt();
    uint8_t preset = (uint8_t)server.arg("preset").toInt();
    String  name   = server.arg("name");
    int idx = em.addHvacMode(area, name.c_str(), preset);
    server.send(200,"application/json", idx>=0 ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"full or wrong type\"}");
  });

  server.on("/api/hvac/add_fan", HTTP_POST, [](){
    using namespace DynetEntities;
    if (!server.hasArg("area")||!server.hasArg("name")||!server.hasArg("preset")){server.send(400,"application/json","{\"ok\":false}");return;}
    uint8_t area   = (uint8_t)server.arg("area").toInt();
    uint8_t preset = (uint8_t)server.arg("preset").toInt();
    String  name   = server.arg("name");
    int idx = em.addHvacFanMode(area, name.c_str(), preset);
    server.send(200,"application/json", idx>=0 ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"full or wrong type\"}");
  });

  server.on("/api/hvac/del_mode", HTTP_POST, [](){
    using namespace DynetEntities;
    if (!server.hasArg("area")||!server.hasArg("idx")){server.send(400,"application/json","{\"ok\":false}");return;}
    uint8_t area = (uint8_t)server.arg("area").toInt();
    uint8_t idx  = (uint8_t)server.arg("idx").toInt();
    em.deleteHvacMode(area, idx);
    server.send(200,"application/json","{\"ok\":true}");
  });

  server.on("/api/hvac/del_fan", HTTP_POST, [](){
    using namespace DynetEntities;
    if (!server.hasArg("area")||!server.hasArg("idx")){server.send(400,"application/json","{\"ok\":false}");return;}
    uint8_t area = (uint8_t)server.arg("area").toInt();
    uint8_t idx  = (uint8_t)server.arg("idx").toInt();
    em.deleteHvacFanMode(area, idx);
    server.send(200,"application/json","{\"ok\":true}");
  });

  server.on("/api/hvac/save_mode", HTTP_POST, [](){
    using namespace DynetEntities;
    if (!server.hasArg("area")||!server.hasArg("idx")||!server.hasArg("name")||!server.hasArg("preset")){server.send(400,"application/json","{\"ok\":false}");return;}
    uint8_t area   = (uint8_t)server.arg("area").toInt();
    uint8_t idx    = (uint8_t)server.arg("idx").toInt();
    uint8_t preset = (uint8_t)server.arg("preset").toInt();
    String  name   = server.arg("name");
    int ai = em.findArea(area);
    if (ai<0||!em.areaAt(ai).hvac||idx>=MAX_HVAC_MODES||!em.areaAt(ai).hvac->modes[idx].used){server.send(404,"application/json","{\"ok\":false}");return;}
    em.areaAtMut(ai).hvac->modes[idx].preset1 = preset;
    strncpy(em.areaAtMut(ai).hvac->modes[idx].name, name.c_str(), sizeof(DynetEntities::HvacModeEntry::name)-1);
    em.areaAtMut(ai).hvac->modes[idx].name[sizeof(DynetEntities::HvacModeEntry::name)-1] = '\0';
    publishHADiscoveryForArea(area);
    saveEntities();
    server.send(200,"application/json","{\"ok\":true}");
  });

  server.on("/api/hvac/save_fan", HTTP_POST, [](){
    using namespace DynetEntities;
    if (!server.hasArg("area")||!server.hasArg("idx")||!server.hasArg("name")||!server.hasArg("preset")){server.send(400,"application/json","{\"ok\":false}");return;}
    uint8_t area   = (uint8_t)server.arg("area").toInt();
    uint8_t idx    = (uint8_t)server.arg("idx").toInt();
    uint8_t preset = (uint8_t)server.arg("preset").toInt();
    String  name   = server.arg("name");
    int ai = em.findArea(area);
    if (ai<0||!em.areaAt(ai).hvac||idx>=MAX_HVAC_FANMODES||!em.areaAt(ai).hvac->fanModes[idx].used){server.send(404,"application/json","{\"ok\":false}");return;}
    em.areaAtMut(ai).hvac->fanModes[idx].preset1 = preset;
    strncpy(em.areaAtMut(ai).hvac->fanModes[idx].name, name.c_str(), sizeof(DynetEntities::HvacModeEntry::name)-1);
    em.areaAtMut(ai).hvac->fanModes[idx].name[sizeof(DynetEntities::HvacModeEntry::name)-1] = '\0';
    publishHADiscoveryForArea(area);
    saveEntities();
    server.send(200,"application/json","{\"ok\":true}");
  });

  // Area requests
  server.on("/api/area_req", HTTP_POST, [](){
    if (!server.hasArg("area") || !server.hasArg("do")) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    uint8_t area = (uint8_t)server.arg("area").toInt();
    String what = server.arg("do");
    if (what == "req_preset") {
      dynet.sendRequestPreset(area);
    } else if (what == "req_levels") {
      // Request first 64 channels for that area (0..63) — adjust as needed
      int n = DynetEntities::em.requestLevelsForArea(area, 16);  // fallback probe =16
      LOGF("[DyNet] requested %d levels for area %u\n", n, area);
    } else if (what == "set_setpoint") {
    float c = server.hasArg("val") ? server.arg("val").toFloat() : 22.0f;
    dynet.sendSetTempSetpoint_q025(area, c);
    server.send(200, "application/json", "{\"ok\":true}");
    return;
    } else {
      server.send(400, "application/json", "{\"ok\":false}"); return;
    }
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/logs", HTTP_GET, [](){
    uint32_t since = server.hasArg("since") ? server.arg("since").toInt() : 0;
    String out;
    uint32_t newSeq = logs_serialize_since(since, out);
    server.send(200, "application/json",
                String("{\"seq\":")+newSeq+",\"lines\":["+out+"]}");
  });

  // Global requests
  server.on("/api/global_req", HTTP_POST, [](){
    if (!server.hasArg("do")) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    String what = server.arg("do");
    if (what == "req_all_levels") {
      for (int i=0;i<DynetEntities::em.areasCount();i++) {
        uint8_t a = DynetEntities::em.areaAt(i).area;
        for (uint8_t ch=0; ch<64; ch++) { dynet.sendRequestChannelLevel(a, ch); delay(2); }
      }
      server.send(200, "application/json", "{\"ok\":true}");
    } else {
      server.send(400, "application/json", "{\"ok\":false}");
    }
  });

  // Area save preset
  server.on("/area/save_preset", HTTP_POST, [](){
    if (!server.hasArg("area")) { server.send(400, "text/plain", "bad"); return; }
    uint8_t area = (uint8_t)server.arg("area").toInt();
    dynet.sendProgramCurrentPreset(area);
    server.sendHeader("Location", "/"); server.send(302, "text/plain", "");
  });

  // Set area type (Lights=0 / Curtain=1)
  server.on("/api/area_type", HTTP_POST, [](){
    if (!server.hasArg("area") || !server.hasArg("type")) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    uint8_t area = (uint8_t)server.arg("area").toInt();
    uint8_t ty   = (uint8_t)server.arg("type").toInt();
    DynetEntities::em.setAreaType(area, (DynetEntities::AreaType)ty);
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // Add a curtain entry to a Curtain-type area
  server.on("/api/add_area_curtain", HTTP_POST, [](){
    if (!server.hasArg("area")) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    uint8_t area = (uint8_t)server.arg("area").toInt();
    int idx = DynetEntities::em.addAreaCurtain(area);
    if (idx < 0) { server.send(200, "application/json", "{\"ok\":false,\"error\":\"Full (max 32)\"}"); return; }
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // Delete a curtain entry
  server.on("/api/del_area_curtain", HTTP_POST, [](){
    if (!server.hasArg("area") || !server.hasArg("idx")) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    uint8_t area = (uint8_t)server.arg("area").toInt();
    uint8_t idx  = (uint8_t)server.arg("idx").toInt();
    DynetEntities::em.deleteAreaCurtain(area, idx);
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // Save curtain entry name + preset assignments
  server.on("/api/save_area_curtain", HTTP_POST, [](){
    if (!server.hasArg("area") || !server.hasArg("idx") ||
        !server.hasArg("open") || !server.hasArg("close") || !server.hasArg("stop")) {
      server.send(400, "application/json", "{\"ok\":false}"); return;
    }
    uint8_t area  = (uint8_t)server.arg("area").toInt();
    uint8_t idx   = (uint8_t)server.arg("idx").toInt();
    uint8_t openP = (uint8_t)constrain((int)server.arg("open").toInt(),  1, 128);
    uint8_t closeP= (uint8_t)constrain((int)server.arg("close").toInt(), 1, 128);
    uint8_t stopP = (uint8_t)constrain((int)server.arg("stop").toInt(),  1, 128);
    String  name  = server.arg("name");
    DynetEntities::em.setAreaCurtainEntry(area, idx, name.c_str(), openP, closeP, stopP);
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // Test command for an area curtain entry (OPEN / CLOSE / STOP) from WebUI
  server.on("/api/area_cover", HTTP_POST, [](){
    if (!server.hasArg("area") || !server.hasArg("idx") || !server.hasArg("cmd")) {
      server.send(400, "application/json", "{\"ok\":false}"); return;
    }
    uint8_t area = (uint8_t)server.arg("area").toInt();
    uint8_t idx  = (uint8_t)server.arg("idx").toInt();
    String  cmd  = server.arg("cmd");
    DynetEntities::em.commandAreaCurtain(area, idx, cmd.c_str());
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // Change channel type
  server.on("/api/type", HTTP_POST, [](){
    if (!server.hasArg("area") || !server.hasArg("ch") || !server.hasArg("type")) { server.send(400, "text/plain", "bad"); return; }
    uint8_t area = (uint8_t)server.arg("area").toInt();
    uint8_t ch   = (uint8_t)server.arg("ch").toInt();
    uint8_t ty   = (uint8_t)server.arg("type").toInt();
    DynetEntities::em.setChannelType(area, ch, (DynetEntities::EntityType)ty);
    saveEntities(); 
    // republish HA discovery for this channel
    int idx = DynetEntities::em.findChannel(area, ch);
    if (idx >= 0) publishHADiscoveryForChannel(idx);
    server.sendHeader("Location", "/"); server.send(302, "text/plain", "");
  });

  // Backup: bundle config + entities
  server.on("/backup", HTTP_GET, [](){
    String cfgJ = readWholeFile(CONFIG_FILE);
    String entJ = readWholeFile(ENTITIES_FILE);
    String out = "{\"version\":1,\"config\":" + cfgJ + ",\"entities\":" + entJ + "}";
    String fname = "ESPDyNet_" + deviceId + ".json";
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + fname + "\"");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", out);
  });

  // Restore (multipart)
  server.on("/restore_backup", HTTP_POST, handleRestoreBackupPost, handleRestoreBackupUpload);
  server.on("/ap_portal_config", HTTP_POST, handleApPortalConfigPost);
  
  // Status JSON for badges
  server.on("/status", HTTP_GET, [](){
    DynamicJsonDocument doc(4096);
    const bool staUp = (WiFi.status() == WL_CONNECTED);
    doc["mqtt_connected"] = mqtt.connected();
    doc["mqtt_broker"]    = String(cfg.mqtt_server);
    if (staUp) {
      doc["sta_ip"]   = WiFi.localIP().toString();
      doc["sta_ssid"] = WiFi.SSID();
    } else { doc["sta_ip"] = ""; doc["sta_ssid"] = ""; }
    doc["ap_active"] = apActive;
    doc["ap_ssid"]   = apSsid;
    server.sendHeader("Cache-Control","no-store");
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  // Wi‑Fi scan
  server.on("/wifi_scan", HTTP_GET, [](){
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING || n == -2) {
      if (n == -2) WiFi.scanNetworks(true /*async*/, true /*show_hidden*/);
      server.sendHeader("Cache-Control","no-store");
      server.send(200, "application/json", "[]");
      return;
    }
    String out = "[";
    for (int i = 0; i < n; i++) {
      if (i) out += ",";
      out += "{\"ssid\":\"" + WiFi.SSID(i) + "\""
          +  ",\"rssi\":" + String(WiFi.RSSI(i))
          +  ",\"enc\":"  + String(WiFi.encryptionType(i))
          +  ",\"ch\":"   + String(WiFi.channel(i))
          +  "}";
    }
    out += "]";
    WiFi.scanDelete();
    server.sendHeader("Cache-Control","no-store");
    server.send(200, "application/json", out);
  });

  // --- Web Console (Logs) UI ---
  server.on("/logs", HTTP_GET, [](){
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.sendHeader("Cache-Control","no-store");
    server.send(200, "text/html; charset=utf-8", "");
    server.sendContent(
      F("<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP DyNet • Logs</title>"
        "<style>"
        "body{font-family:system-ui;margin:0;background:#0f1216;color:#e5e7eb}"
        ".nav{display:flex;gap:8px;align-items:center;padding:10px 14px;border-bottom:1px solid #273245}"
        ".btn{padding:8px 12px;border:1px solid #273245;border-radius:10px;background:#161a20;color:#e5e7eb;text-decoration:none;display:inline-block}"
        ".row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}"
        ".meta{margin-left:auto;font-size:12px;color:#9ca3af}"
        "pre{margin:0;padding:12px;white-space:pre-wrap;word-break:break-word;font-family:ui-monospace,Consolas,monospace;"
        "background:#0f1216;min-height:calc(100vh - 58px);max-height:calc(100vh - 58px);overflow:auto}"
        "</style>"
        "<div class='nav'><a class='btn' href='/'>Home</a><a class='btn' href='/config'>Config</a>"
        "<a class='btn' href='/logs'>Logs</a>"
        "<form method='POST' action='/logs_clear' style='display:inline'><button class='btn' type='submit'>Clear</button></form>"
        "<a class='btn' href='/logs_pull?s=0' download='esp_dynet_logs.json'>Download</a>"
        "<span class='meta' id='st'></span></div>"
        "<pre id='out'></pre>"
        "<script>"
          "let seq = 0, busy = false;"
          "const out = document.getElementById('out');"
          "const st  = document.getElementById('st');"
          "let lastSet = new Set();"
          "function poll(){"
          "  if (busy) return; busy = true;"
          "  const stick = (out.scrollTop + out.clientHeight + 40 >= out.scrollHeight);"
          "  fetch('/logs_pull?s=' + seq, {cache:'no-store'})"
          "    .then(r => r.json())"
          "    .then(j => {"
          "      if (Array.isArray(j.lines)) {"
          "        const fresh = [];"
          "        for (const line of j.lines) if (!lastSet.has(line)) fresh.push(line);"
          "        if (fresh.length || (j.full === true)) {"
          "          out.textContent = j.lines.join('\\n') + (j.lines.length ? '\\n' : '');"
          "          lastSet = new Set(j.lines);"
          "        }"
          "      }"
          "      if (typeof j.seq === 'number') seq = j.seq;"
          "      st.textContent = 'seq ' + seq;"
          "      if (stick) out.scrollTop = out.scrollHeight;"
          "    })"
          "    .catch(()=>{})"
          "    .finally(()=>{ busy = false; });"
          "}"
          "setInterval(poll, 1000); poll();"
        "</script>")
    );
  });

  // --- Web Console feed (incremental JSON) ---
  server.on("/logs_pull", HTTP_GET, [](){
    uint32_t since = 0;
    if (server.hasArg("s")) since = (uint32_t) server.arg("s").toInt();
    String payload; logs_serialize_since(since, payload);
    server.sendHeader("Cache-Control","no-store, no-cache, must-revalidate");
    server.send(200, "application/json", payload);
  });

  // --- Clear logs ---
  server.on("/logs_clear", HTTP_POST, [](){
    logs_clear();
    server.sendHeader("Location","/logs");
    server.send(302, "text/plain", "");
  });

  server.begin();
}
