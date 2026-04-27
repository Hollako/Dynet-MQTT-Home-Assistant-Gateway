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
static void handleFwSetPending();
static void handleFwCheckUpdate();
static void handleFwDoUpdate();

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
      ".nav{position:sticky;top:0;z-index:10;display:flex;gap:6px;align-items:center;padding:8px 14px;border-bottom:1px solid var(--border);background:var(--bg)}"
      ".btn.nicn{display:inline-flex;align-items:center;justify-content:center;width:42px;height:42px;font-size:22px;padding:0;min-width:unset;min-height:unset;border-radius:10px}"
      ".nav .sp{flex:1}"
      ".btn{display:inline-block;padding:8px 12px;border:1px solid var(--border);border-radius:10px;background:var(--card);cursor:pointer}"
      "a{color:inherit;text-decoration:none}"
      "a.btn{text-decoration:none;color:inherit}"
      ".container{padding:16px}"
      ".card{border:1px solid var(--border);border-radius:12px;padding:12px;margin:10px 0;background:var(--card)}"
      ".muted{color:var(--muted);font-size:12px}"
      ".grid2{display:grid;grid-template-columns:repeat(4,minmax(180px,1fr));row-gap:2px;column-gap:12px}"
      "@media(max-width:900px){.grid2{grid-template-columns:repeat(2,1fr)}}"
      ".title{font-weight:600;margin:0 0 6px}"
      ".row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}"
      ".row.space{justify-content:space-between}"
      ".grow{flex:1}"
      ".in,select,button,input[type=text],input[type=password],input[type=number]{min-width:120px;padding:8px;border:1px solid var(--border);border-radius:8px;background:var(--card);color:var(--fg)}"
      "label{font-size:13px;color:var(--muted)}"
      ".status{display:flex;gap:12px;align-items:center;margin:10px 0;flex-wrap:wrap}"
      ".badge{display:inline-flex;align-items:center;gap:6px;font-size:12px;padding:4px 10px;border-radius:999px;background:var(--card);border:1px solid var(--border)}"
      ".dot{width:8px;height:8px;border-radius:50%;background:#bbb}"
      ".ok .dot{background:#19c37d}.warn .dot{background:#f59e0b}.err .dot{background:#ef4444}.inactive .dot{background:#aaa}"
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
      ".icon-btn:hover{filter:brightness(.92)}"

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
        "const ws=document.getElementById('wifiSig');"
        "if(ws){ws.classList.remove('ok','warn','err','inactive'); ws.classList.add(b.rssi_pct>50?'ok':b.rssi_pct>20?'warn':'err');} "
        "const wt=ws?.querySelector('span:last-child');"
        "if(wt) wt.textContent = 'Wi-Fi: '+(b.sta_ssid||'?')+' ('+(b.rssi_pct||0)+'%)';"
        "const ab=document.getElementById('apBadge');"
        "if(ab){ab.classList.remove('ok','warn','err','inactive');ab.classList.add(b.ap_active?'ok':'err');}"
        "const a=document.getElementById('apText');"
        "if(a) a.textContent = b.ap_active ? ('AP: '+(b.ap_ssid||'')) : 'AP: Off';"
      "}"
      "function poll(){fetch('/status',{cache:'no-store'}).then(r=>r.json()).then(upd).catch(()=>{});} "
      "var _sunSvg='<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><circle cx=\"12\" cy=\"12\" r=\"5\"></circle><line x1=\"12\" y1=\"1\" x2=\"12\" y2=\"3\"></line><line x1=\"12\" y1=\"21\" x2=\"12\" y2=\"23\"></line><line x1=\"4.22\" y1=\"4.22\" x2=\"5.64\" y2=\"5.64\"></line><line x1=\"18.36\" y1=\"18.36\" x2=\"19.78\" y2=\"19.78\"></line><line x1=\"1\" y1=\"12\" x2=\"3\" y2=\"12\"></line><line x1=\"21\" y1=\"12\" x2=\"23\" y2=\"12\"></line><line x1=\"4.22\" y1=\"19.78\" x2=\"5.64\" y2=\"18.36\"></line><line x1=\"18.36\" y1=\"5.64\" x2=\"19.78\" y2=\"4.22\"></line></svg>';"
      "var _moonSvg='<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z\"></path></svg>';"
      "function applyTheme(){"
        "var dark=localStorage.dark==='1';"
        "document.body.classList.toggle('dark',dark);"
        "var b=document.getElementById('themeBtn');"
        "if(b)b.innerHTML=dark?_sunSvg:_moonSvg;"
      "}"
      "function toggleDark(){localStorage.dark=(localStorage.dark==='1'?'0':'1');applyTheme();}"
      "window.addEventListener('DOMContentLoaded',()=>{applyTheme();poll();setInterval(poll,2000);});"
      "</script>"
      "</head><body>")
  );

  // nav
  server.sendContent(
    F("<div class='nav'>"
      "<img src='/logo.png' alt='Logo' style='height:52px;width:auto;object-fit:contain;margin-right:10px' "
           "onerror='this.style.display=\"none\"'>"
      "<strong>ESP DyNet Gateway - "));
  server.sendContent(deviceId);
  server.sendContent(
    F("</strong><div class='sp'></div>"
      "<a class='btn nicn icon-btn' href='/' title='Home'>"
        "<svg xmlns='http://www.w3.org/2000/svg' width='22' height='22' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'>"
          "<path d='M3 9.5L12 3l9 6.5V20a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V9.5z'/>"
          "<path d='M9 21V12h6v9'/>"
        "</svg>"
      "</a>"
      "<a class='btn nicn icon-btn' href='/config' title='Configuration'>"
        "<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
          "<circle cx='12' cy='12' r='3'></circle>"
          "<path d='M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z'></path>"
        "</svg>"
      "</a>"));
  if (cfg.log_web) server.sendContent(F("<a class='btn nicn icon-btn' href='/logs' title='Logs'>&#x2630;</a>"));
  server.sendContent(
    F("<a class='btn nicn icon-btn' href='/fw' title='Firmware Update'>"
        "<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
          "<polyline points='16 16 12 12 8 16'></polyline>"
          "<line x1='12' y1='12' x2='12' y2='21'></line>"
          "<path d='M20.39 18.39A5 5 0 0 0 18 9h-1.26A8 8 0 1 0 3 16.3'></path>"
        "</svg>"
      "</a>"
      "<button class='btn nicn icon-btn' id='themeBtn' onclick='toggleDark()' title='Toggle theme'>"
        "<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
          "<path d='M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z'></path>"
        "</svg>"
      "</button>"
      "<button class='btn nicn icon-btn' title='Reboot Device' style='color:#e74c3c' "
              "onclick='if(confirm(\"Reboot the device?\"))window.location.href=\"/reboot\"'>&#x21BB;</button>"

    "</div><div class='container'>")
  );

  // status badges — order: Wi-Fi · IP · MQTT · AP
  server.sendContent(
    F("<div class='status'>"
      "<div id='wifiSig'   class='badge inactive'><span class='dot'></span><span>Wi-Fi…</span></div>"
      "<div id='staBadge'  class='badge'><span class='dot'></span><span id='staText'>IP…</span></div>"
      "<div id='mqttBadge' class='badge'><span class='dot'></span><span id='mqttText'>MQTT…</span></div>"
      "<div id='apBadge' class='badge inactive'><span class='dot'></span><span id='apText'>AP…</span></div>"
      "<div style='margin-left:auto'></div>"
      "<div class='badge inactive' style='color:var'>v")
  );
  server.sendContent(HA_SW_VERSION);
  server.sendContent(F("</div></div>"));
}
static inline void pageWrite(const __FlashStringHelper* s){ server.sendContent(s); }
static inline void pageWrite(const String& s){ if(s.length()) server.sendContent(s); }
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
  // ESP32 DevKit / WROOM-32 — descriptive labels; GPIO number appended by gpioOpt()
  o += gpioOpt( 1, "TX0 ⚠️",  current);   // UART0 TX — shared with USB/Serial
  o += gpioOpt( 3, "RX0 ⚠️",  current);   // UART0 RX — shared with USB/Serial
  o += gpioOpt( 2, "LED ⚠️",  current);   // built-in LED — strapping pin, must be LOW at boot
  o += gpioOpt( 4, "GPIO4",   current);
  o += gpioOpt( 5, "GPIO5",   current);
  o += gpioOpt(12, "GPIO12 ⚠️", current); // strapping pin — boot fails if HIGH
  o += gpioOpt(13, "GPIO13",  current);
  o += gpioOpt(14, "GPIO14",  current);
  o += gpioOpt(15, "GPIO15 ⚠️", current); // strapping pin
  o += gpioOpt(16, "RX2",     current);   // UART2 RX
  o += gpioOpt(17, "TX2",     current);   // UART2 TX
  o += gpioOpt(18, "SCK",     current);   // SPI clock
  o += gpioOpt(19, "MISO",    current);   // SPI
  o += gpioOpt(21, "SDA",     current);   // I2C data
  o += gpioOpt(22, "SCL",     current);   // I2C clock
  o += gpioOpt(23, "MOSI",    current);   // SPI
  o += gpioOpt(25, "GPIO25",  current);   // DAC1
  o += gpioOpt(26, "GPIO26",  current);   // DAC2
  o += gpioOpt(27, "GPIO27",  current);
  o += gpioOpt(32, "GPIO32",  current);
  o += gpioOpt(33, "GPIO33",  current);
#endif
  return o;
}

// ──────────────────────────────────────────────────────────────────────────
// Render the expandable body of one area card (LIGHTS / CURTAIN / HVAC).
// Uses pageWrite() which streams to the active HTTP response — call only
// while a CONTENT_LENGTH_UNKNOWN response is in progress.
// ──────────────────────────────────────────────────────────────────────────
static void renderAreaBody(uint8_t aNum) {
  using namespace DynetEntities;
  int ai = em.findArea(aNum);
  if (ai < 0) return;
  const AreaState& as = em.areaAt(ai);

  if (as.areaType == AREA_CURTAIN) {
    // ── Curtain Area: preset count selector + per-curtain cards ─────────────
    uint8_t pCount = as.presetCount ? as.presetCount : (cfg.ha_preset_count ? cfg.ha_preset_count : 4);
    if (pCount > 128) pCount = 128;

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
    if (as.curtains) for (uint8_t ci = 0; ci < MAX_CURTAINS_PER_AREA; ci++) {
      const AreaCurtainEntry& ce = as.curtains[ci];
      if (!ce.used) continue;

      pageWrite(F("<div style='border:1px solid var(--border);border-radius:12px;padding:8px 10px;background:var(--card)'>"));

        // Header row: name input + delete
        pageWrite(F("<div class='row space' style='gap:6px;align-items:center;margin-bottom:8px'>"));
          pageWrite(F("<input class='in' id='cen_")); pageWrite(String(as.area)); pageWrite(F("_")); pageWrite(String(ci));
          pageWrite(F("' type='text' maxlength='23' placeholder='Curtain name' value='"));
          pageWrite(ce.name[0] ? safeAttr(ce.name, sizeof(ce.name)) : (String("Curtain ") + (ci+1)));
          pageWrite(F("' style='width:150px;padding:3px 6px;font-size:13px'>"));
          pageWrite(F("<button class='btn' style='background:#c0392b;color:#fff;padding:3px 8px;"
                      "min-width:auto;min-height:auto;font-size:13px' title='Delete Curtain'"
                      " onclick='delAreaCurtain("));
          pageWrite(String(as.area)); pageWrite(F(",")); pageWrite(String(ci)); pageWrite(F(")'>&#10006;</button>"));
        pageWrite(F("</div>")); // header row

        // Body: preset box + test buttons
        pageWrite(F("<div class='row' style='gap:16px;flex-wrap:wrap;align-items:flex-start'>"));
          pageWrite(F("<div style='padding:8px 12px;border:1px solid var(--border);border-radius:8px;"
                      "display:flex;flex-direction:column;gap:6px;min-width:160px'>"));
            pageWrite(F("<div style='font-size:12px;color:var(--muted);font-weight:600'>Curtain Presets</div>"));

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

  } else if (as.areaType == AREA_LIGHTS) {
    // ── Area action row: preset buttons + count selector + utility buttons ──
    pageWrite(F("<div class='row' style='margin-top:5px;flex-wrap:wrap;gap:5px;align-items:center'>"));

      for (int p = 1; p <= MAX_LIGHT_PRESETS; p++) {
        pageWrite(F("<button class='btn action pb' data-area='"));
        pageWrite(String(as.area));
        pageWrite(F("' data-p='"));
        pageWrite(String(p));
        pageWrite(F("' style='padding:3px 9px;min-width:auto;min-height:auto;font-size:13px' onclick='sendPreset("));
        pageWrite(String(as.area)); pageWrite(F(",")); pageWrite(String(p));
        pageWrite(F(")'>P")); pageWrite(String(p)); pageWrite(F("</button>"));
      }

      {
        uint8_t thisPc = as.presetCount ? as.presetCount : (cfg.ha_preset_count ? cfg.ha_preset_count : 4);
        if (thisPc > MAX_LIGHT_PRESETS) thisPc = MAX_LIGHT_PRESETS;
        pageWrite(F("<select class='pcSel in' data-area='"));
        pageWrite(String(as.area));
        pageWrite(F("' onchange='setPC("));
        pageWrite(String(as.area));
        pageWrite(F(",this.value)' title='Number of presets shown'"
                    " style='padding:2px 4px;min-width:auto;width:auto;font-size:12px'>"));
        for (int n = 1; n <= MAX_LIGHT_PRESETS; n++) {
          if (n == thisPc) pageWrite(String("<option value='") + n + "' selected>" + n + "</option>");
          else             pageWrite(String("<option value='") + n + "'>" + n + "</option>");
        }
        pageWrite(F("</select>"));
      }

      pageWrite(F("<span style='width:1px;height:18px;background:var(--border);display:inline-block;margin:0 2px'></span>"));

      pageWrite(F("<form method='POST' action='/area/save_preset' style='margin:0'><input type='hidden' name='area' value='"));
      pageWrite(String(as.area));
      pageWrite(F("'><button class='btn prog' style='padding:3px 10px;min-width:auto;min-height:auto;font-size:13px' type='submit'>Save Preset</button></form>"));
      pageWrite(F("<button class='btn action' style='padding:3px 10px;min-width:auto;min-height:auto;font-size:13px' onclick=\"fetch('/api/area_req',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area="));
      pageWrite(String(as.area)); pageWrite(F("&do=req_preset'})\">Req Preset</button>"));
      pageWrite(F("<button class='btn action' style='padding:3px 10px;min-width:auto;min-height:auto;font-size:13px' onclick=\"fetch('/api/area_req',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area="));
      pageWrite(String(as.area)); pageWrite(F("&do=req_levels'})\">Req Levels</button>"));

    pageWrite(F("</div>")); // action row

    // ── Preset name editor ────────────────────────────────────────────────
    {
      uint8_t thisPc = as.presetCount ? as.presetCount : 4;
      if (thisPc > MAX_LIGHT_PRESETS) thisPc = MAX_LIGHT_PRESETS;
      pageWrite(F("<div style='margin-top:6px;border:1px solid var(--border);border-radius:8px;padding:8px'>"));
      pageWrite(F("<div style='font-size:12px;color:var(--muted);font-weight:600;margin-bottom:6px'>Preset Names</div>"));
      pageWrite(F("<div id='pnGrid_A")); pageWrite(String(as.area));
      pageWrite(F("' style='display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:4px'>"));
      for (int p = 1; p <= MAX_LIGHT_PRESETS; p++) {
        String nm = em.getPresetDisplayName((uint8_t)as.area, (uint8_t)p);
        pageWrite(F("<div class='pnRow' data-area='"));
        pageWrite(String(as.area)); pageWrite(F("' data-p='")); pageWrite(String(p));
        pageWrite(F("' style='display:"));
        pageWrite(p <= thisPc ? F("flex") : F("none"));
        pageWrite(F(";align-items:center;gap:3px'>"));
          pageWrite(F("<span style='font-size:12px;min-width:22px;color:var(--muted)'>P"));
          pageWrite(String(p)); pageWrite(F("</span>"));
          pageWrite(F("<input id='pn_")); pageWrite(String(as.area)); pageWrite(F("_")); pageWrite(String(p));
          pageWrite(F("' type='text' maxlength='15' value='"));
          pageWrite(safeAttr(nm.c_str(), nm.length()));
          pageWrite(F("' style='flex:1;padding:3px 5px;font-size:12px;min-width:0'>"));
          pageWrite(F("<button class='btn' style='padding:2px 7px;min-width:auto;min-height:auto;font-size:12px' onclick='savePresetName("));
          pageWrite(String(as.area)); pageWrite(F(",")); pageWrite(String(p));
          pageWrite(F(",\"pn_")); pageWrite(String(as.area)); pageWrite(F("_")); pageWrite(String(p));
          pageWrite(F("\")'>&#10003;</button>"));
        pageWrite(F("</div>")); // pnRow
      }
      pageWrite(F("</div>")); // pnGrid
      pageWrite(F("</div>")); // preset name editor
    }

    // ── Channel cards — sorted by channel number ──────────────────────────
    pageWrite(F("<div style='margin-top:8px;padding-top:6px' class='grid2'>"));
    for (int chNum = 0; chNum <= 254; chNum++) {
      int ci = em.findChannel((uint8_t)aNum, (uint8_t)chNum);
      if (ci < 0) continue;
      const ChannelState& cs = em.channelAt(ci);
      if (!cs.present) continue;
      if (cs.isCurtainSlave) continue;

      pageWrite(F("<div class='card' style='padding:8px 8px;margin:0'>"));

        // Row 1: Ch# · name · level · on/off · req · delete
        pageWrite(F("<div class='row space' style='gap:4px;flex-wrap:nowrap;align-items:center'>"));
          pageWrite(F("<div class='row' style='gap:4px;align-items:center;flex:1;min-width:0;overflow:hidden'>"));
            pageWrite(F("<b style='white-space:nowrap;font-size:13px'>Ch&nbsp;")); pageWrite(String((int)cs.channel0+1)); pageWrite(F("</b>"));
            pageWrite(F("<input id='cn_")); pageWrite(String(cs.area)); pageWrite(F("_")); pageWrite(String(cs.channel0));
            pageWrite(F("' class='in' type='text' placeholder='Name' maxlength='23' value='"));
            pageWrite(cs.name[0] ? safeAttr(cs.name, sizeof(cs.name)) : (String(F("Area ")) + String(cs.area) + String(F(" Ch ")) + String((int)cs.channel0 + 1)));
            pageWrite(F("' style='flex:1;min-width:140px;max-width:260px;padding:3px 6px;font-size:13px'>"));
            pageWrite(F("<button class='btn' style='padding:3px 14px;min-width:auto;min-height:auto;font-size:13px' title='Save channel name' onclick='saveChName("));
            pageWrite(String(cs.area)); pageWrite(F(",")); pageWrite(String(cs.channel0));
            pageWrite(F(",\"cn_")); pageWrite(String(cs.area)); pageWrite(F("_")); pageWrite(String(cs.channel0));
            pageWrite(F("\")'>&#10003;</button>"));
            pageWrite(F("<span class='pill' style='font-size:12px'>")); pageWrite(String((int)cs.levelPct)); pageWrite(F("%</span>"));
          pageWrite(F("</div>"));
          pageWrite(F("<button class='btn' style='background:#c0392b;color:#fff;padding:3px 9px;min-width:auto;min-height:auto;font-size:13px;flex-shrink:0' title='Delete Channel' onclick='delCh("));
          pageWrite(String(cs.area)); pageWrite(F(",")); pageWrite(String(cs.channel0));
          pageWrite(F(")'>&#10006;</button>"));
        pageWrite(F("</div>")); // row 1

        // Row 2: channel type + quick controls
        pageWrite(F("<div class='row' style='gap:6px;flex-wrap:wrap;margin-top:6px;align-items:center'>"));
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

          if (cs.type == CURTAIN) {
            auto cbtn = [&](const char* cap, const char* cmd) {
              pageWrite(F("<button class='btn action' style='padding:3px 9px;min-width:auto;min-height:auto;font-size:13px' onclick=\"fetch('/api/cmd',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area="));
              pageWrite(String(cs.area)); pageWrite(F("&ch=")); pageWrite(String(cs.channel0));
              pageWrite(F("&cmd=CURTAIN_")); pageWrite(cmd); pageWrite(F("'})\">"));
              pageWrite(cap); pageWrite(F("</button>"));
            };
            cbtn("&#9650; Open",  "OPEN");
            cbtn("&#9646; Stop",  "STOP");
            cbtn("&#9660; Close", "CLOSE");
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
          }
        pageWrite(F("</div>")); // row 2
      pageWrite(F("</div>")); // channel card
    }
    pageWrite(F("</div>")); // grid
  }

  // ── HVAC body (step selector + modes + fans) ─────────────────────────────
  if (as.areaType == AREA_HVAC && as.hvac) {
    pageWrite(F("<div class='row' style='margin-top:8px;gap:8px;align-items:center'>"));
      pageWrite(F("<span style='font-size:13px;color:var(--muted)'>Setpoint Step:</span>"));
      pageWrite(F("<select class='in' style='width:90px;padding:3px 6px' onchange=\"setHvacStep("));
      pageWrite(String(as.area));
      pageWrite(F(",this.value)\">"));
      float curStep = as.hvac->setptStep;
      pageWrite(F("<option value='0.5'")); if (curStep < 1.0f) pageWrite(F(" selected")); pageWrite(F(">0.5 \xC2\xB0" "C</option>"));
      pageWrite(F("<option value='1.0'")); if (curStep >= 1.0f) pageWrite(F(" selected")); pageWrite(F(">1 \xC2\xB0" "C</option>"));
      pageWrite(F("</select>"));
    pageWrite(F("</div>"));

    pageWrite(F("<div style='display:flex;flex-wrap:wrap;gap:24px;margin-top:8px;align-items:flex-start'>"));

    // Modes column
    pageWrite(F("<div>"));
    pageWrite(F("<div style='font-size:12px;font-weight:600;color:var(--muted);margin-bottom:4px'>HVAC Modes (name \xE2\x86\x92 preset)</div>"));
    for (uint8_t mi = 0; mi < MAX_HVAC_MODES; mi++) {
      const HvacModeEntry& me = as.hvac->modes[mi];
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
    pageWrite(F("</div>")); // modes column

    // Fan modes column
    if (as.hvac->fanCount > 0) {
      pageWrite(F("<div>"));
      pageWrite(F("<div style='font-size:12px;font-weight:600;color:var(--muted);margin-bottom:4px'>Fan Modes (name \xE2\x86\x92 preset)</div>"));
      for (uint8_t fi = 0; fi < MAX_HVAC_FANMODES; fi++) {
        const HvacModeEntry& fe = as.hvac->fanModes[fi];
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
      pageWrite(F("</div>")); // fan column
    }

    pageWrite(F("</div>")); // flex row
  }
}

// Serve the body of one area card for lazy AJAX loading
static void handleAreaDetail() {
  using namespace DynetEntities;
  int aNum = server.arg("area").toInt();
  if (aNum < 1 || aNum > 255) { server.send(400, "text/plain", "bad area"); return; }
  if (em.findArea((uint8_t)aNum) < 0) { server.send(404, "text/plain", "not found"); return; }
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  renderAreaBody((uint8_t)aNum);
  server.sendContent(""); // flush
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

  // ── Pagination ───────────────────────────────────────────────────────────────
  const int PAGE_SIZE  = 10;
  int       totalAreas = em.areasCount();
  int       totalPages = (totalAreas + PAGE_SIZE - 1) / PAGE_SIZE;
  if (totalPages < 1) totalPages = 1;

  int page = server.arg("page").toInt();
  if (page < 1) page = 1;
  if (page > totalPages) page = totalPages;

  // Helper: build a page URL preserving other future args
  auto pageUrl = [](int p) -> String {
    return String("/?page=") + p;
  };

  // Render pagination nav (only if more than one page)
  auto renderPagNav = [&]() {
    if (totalPages <= 1) return;
    pageWrite(F("<div style='display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin:6px 0'>"));
    // Prev
    if (page > 1) {
      pageWrite(F("<a class='btn' href='"));
      pageWrite(pageUrl(page - 1));
      pageWrite(F("' style='padding:4px 12px;font-size:13px'>&#x25C4; Prev</a>"));
    } else {
      pageWrite(F("<span class='btn' style='padding:4px 12px;font-size:13px;opacity:.35;cursor:default'>&#x25C4; Prev</span>"));
    }
    // Page numbers (show up to 9; collapse with … if many)
    for (int pg = 1; pg <= totalPages; pg++) {
      bool isCur = (pg == page);
      if (totalPages > 9 && !isCur && pg != 1 && pg != totalPages &&
          (pg < page - 2 || pg > page + 2)) {
        if (pg == page - 3 || pg == page + 3) pageWrite(F("<span style='align-self:center;color:var(--muted)'>…</span>"));
        continue;
      }
      pageWrite(isCur
        ? F("<span class='btn' style='padding:4px 10px;font-size:13px;background:#2980b9;color:#fff;border-color:#2980b9;cursor:default'>")
        : F("<a class='btn' href='"));
      if (!isCur) { pageWrite(pageUrl(pg)); pageWrite(F("' style='padding:4px 10px;font-size:13px'>")); }
      pageWrite(String(pg));
      pageWrite(isCur ? F("</span>") : F("</a>"));
    }
    // Next
    if (page < totalPages) {
      pageWrite(F("<a class='btn' href='"));
      pageWrite(pageUrl(page + 1));
      pageWrite(F("' style='padding:4px 12px;font-size:13px'>Next &#x25BA;</a>"));
    } else {
      pageWrite(F("<span class='btn' style='padding:4px 12px;font-size:13px;opacity:.35;cursor:default'>Next &#x25BA;</span>"));
    }
    // Area count summary
    pageWrite(F("<span class='muted' style='margin-left:6px;font-size:12px'>"));
    pageWrite(String(totalAreas));
    pageWrite(F(" areas total</span>"));
    pageWrite(F("</div>"));
  };

  renderPagNav();

  // Render per-area cards for this page only — sorted by area number (header only; body lazy-loaded)
  int skip  = (page - 1) * PAGE_SIZE;
  int shown = 0;
  for (int aNum = 1; aNum <= 255; aNum++) {
    int ai = em.findArea((uint8_t)aNum);
    if (ai < 0) continue;
    if (skip > 0) { skip--; continue; }   // skip areas before this page
    if (shown >= PAGE_SIZE) break;         // stop after page window
    shown++;
    const AreaState& as = em.areaAt(ai);

    pageWrite(F("<div class='card' style='padding:10px 12px'>"));
    // ── Area header ──────────────────────────────────────────────────────────
    pageWrite(F("<div class='row space' style='flex-wrap:wrap;gap:6px'>"));
      // Left: number + name + type + status pills
      pageWrite(F("<div class='row' style='gap:6px;flex-wrap:wrap;align-items:center;flex:1;min-width:0'>"));
        pageWrite(F("<b style='white-space:nowrap'>Area "));
        pageWrite(String(as.area));
        pageWrite(F("</b>"));
        // Name input + save
        pageWrite(F("<input id='an_"));
        pageWrite(String(as.area));
        pageWrite(F("' class='in' type='text' placeholder='Name' maxlength='23' value='"));
        pageWrite(as.name[0] ? safeAttr(as.name, sizeof(as.name)) : (String(F("Area ")) + String(as.area)));
        pageWrite(F("' style='flex:1;min-width:160px;max-width:320px;padding:3px 6px'>"));
        pageWrite(F("<button class='btn' style='padding:2px 14px;min-width:auto;min-height:auto;font-size:13px' title='Save area name' onclick='saveAreaName("));
        pageWrite(String(as.area)); pageWrite(F(",\"an_")); pageWrite(String(as.area)); pageWrite(F("\")'>&#10003;</button>"));
        // Area type selector
        pageWrite(F("<span style='font-size:12px;color:var(--muted);white-space:nowrap'>Area Type:</span>"));
        pageWrite(F("<select class='in' style='padding:2px 6px;min-width:auto;width:auto;font-size:12px' onchange='setAreaType("));
        pageWrite(String(as.area)); pageWrite(F(",this.value)'>"));
        pageWrite(String("<option value='0'") + ((as.areaType==DynetEntities::AREA_LIGHTS)?"  selected":"") + ">Lights</option>");
        pageWrite(String("<option value='1'") + ((as.areaType==DynetEntities::AREA_CURTAIN)?" selected":"") + ">Curtain</option>");
        pageWrite(String("<option value='2'") + ((as.areaType==DynetEntities::AREA_HVAC)?   " selected":"") + ">HVAC</option>");
        pageWrite(F("</select>"));
        // Preset status pill (always visible, updated by pollAreas)
        pageWrite(F("<span class='pill' id='preset_A")); pageWrite(String(as.area));
        pageWrite(F("'>P:&nbsp;"));
        pageWrite((as.preset0 == 0xFF) ? String("?") : String((int)as.preset0 + 1));
        pageWrite(F("</span>"));
        // HVAC live status pills — kept in header so pollAreas() always sees them
        if (as.areaType == DynetEntities::AREA_HVAC) {
          pageWrite(F("<span class='pill' id='hvac_temp_A")); pageWrite(String(as.area));
          if (as.hasTemp) { pageWrite(F("'>")); pageWrite(String(as.tempC,1)); pageWrite(F("\xC2\xB0\x43</span>")); }
          else            { pageWrite(F("' style='color:var(--muted)'>\xE2\x80\x93\xC2\xB0\x43</span>")); }
          pageWrite(F("<span class='pill' id='hvac_sp_A")); pageWrite(String(as.area));
          if (as.hasSetpt) { pageWrite(F("'>")); pageWrite(String(as.setptC,1)); pageWrite(F("\xC2\xB0\x43</span>")); }
          else             { pageWrite(F("' style='color:var(--muted)'>\xE2\x80\x93\xC2\xB0\x43</span>")); }
          pageWrite(F("<span class='pill' id='hvac_mode_A")); pageWrite(String(as.area)); pageWrite(F("'>"));
          if (as.hvac && as.hvac->currentMode[0]) pageWrite(String(as.hvac->currentMode));
          else pageWrite(F("\xE2\x80\x93"));
          pageWrite(F("</span>"));
        }
      pageWrite(F("</div>")); // left group

      // Right: add buttons + expand toggle + delete
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
        // Expand / collapse toggle
        pageWrite(F("<button id='atog_")); pageWrite(String(as.area));
        pageWrite(F("' class='btn' onclick='toggleArea("));
        pageWrite(String(as.area));
        pageWrite(F(")' title='Expand / collapse' style='padding:3px 9px;min-width:auto;min-height:auto;font-size:12px'>&#x25BA;</button>"));
        // Delete area
        pageWrite(F("<button class='btn' style='background:#c0392b;color:#fff;padding:3px 8px;min-width:auto;min-height:auto;font-size:13px' title='Delete Area' onclick='delArea("));
        pageWrite(String(as.area)); pageWrite(F(")'>&#10006;</button>"));
      pageWrite(F("</div>")); // right group

    pageWrite(F("</div>")); // header row

    // ── Lazy-loaded body placeholder ─────────────────────────────────────────
    pageWrite(F("<div id='abody_")); pageWrite(String(as.area)); pageWrite(F("' style='display:none'></div>"));
    pageWrite(F("</div>")); // area card
  }


  renderPagNav(); // bottom pagination nav

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
    // --- Lazy-load area body on expand/collapse
    "var _aLoaded={};"
    "function toggleArea(n){"
      "var d=document.getElementById('abody_'+n);"
      "var btn=document.getElementById('atog_'+n);"
      "if(!d)return;"
      "var vis=d.style.display!=='none';"
      "if(vis){"
        "d.style.display='none';"
        "if(btn)btn.innerHTML='&#x25BA;';"
      "}else{"
        "d.style.display='';"
        "if(btn)btn.innerHTML='&#x25BC;';"
        "if(!_aLoaded[n]){"
          "_aLoaded[n]=true;"
          "d.innerHTML='<div style=\"padding:8px;color:var(--muted)\">Loading\u2026</div>';"
          "fetch('/api/area_detail?area='+n)"
            ".then(function(r){return r.text();})"
            ".then(function(html){d.innerHTML=html;applyPC(n);})"
            ".catch(function(){d.innerHTML='<div style=\"padding:8px;color:#e74c3c\">Load failed \u2014 try again</div>';_aLoaded[n]=false;});"
        "}"
      "}"
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
    // ---- area type map (0=LIGHTS,1=CURTAIN,2=HVAC) ----
    "var aType={"));
  for (int _i = 0; _i < DynetEntities::em.areasCount(); _i++) {
    const auto& _a = DynetEntities::em.areaAt(_i);
    if (!_a.present) continue;
    pageWrite(String(_a.area) + ":" + (uint8_t)_a.areaType + ",");
  }
  pageWrite(F("};"
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
      // Show/hide preset name rows for AREA_LIGHTS
      "document.querySelectorAll('.pnRow[data-area=\"'+area+'\"]').forEach(function(r){"
        "r.style.display=(parseInt(r.dataset.p)<=pc)?'flex':'none';"
      "});"
    "}"
    "function applyAllPC(){"
      "Object.keys(aPC).forEach(function(a){applyPC(parseInt(a));});"
    "}"
    "function setPC(area,n){"
      "n=parseInt(n);"
      // Cap at 16 for light areas
      "if((aType[area]||0)===0 && n>16)n=16;"
      "aPC[area]=n;"
      "applyPC(area);"
      "fetch('/api/set_preset_count',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area='+area+'&n='+n}).catch(function(){});"
    "}"
    "function savePresetName(area,preset,inputId){"
      "var v=document.getElementById(inputId);"
      "if(!v)return;"
      "fetch('/api/set_preset_name',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "body:'area='+area+'&preset='+preset+'&name='+encodeURIComponent(v.value.trim())})"
      ".then(r=>r.json()).then(j=>{if(!j.ok)alert(j.error||'Failed');}).catch(()=>{});"
    "}"
    "function sendPreset(a,p){"
      "fetch('/api/area_preset',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area='+a+'&preset='+p+'&fade=0'});"
    "}"
    "window.addEventListener('DOMContentLoaded',()=>{"
      "setInterval(pollAreas,1500);"
      "pollAreas();"
      // applyAllPC() no longer called here: area bodies are lazy-loaded,
      // applyPC(n) is called per-area when each body finishes loading.
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
    "function setHvacStep(area,step){"
      "fetch('/api/hvac/set_step',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area='+area+'&step='+step})"
        ".then(r=>r.json()).then(j=>{if(!j.ok)alert('Failed');}).catch(()=>alert('Error'));"
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
              "<input class='in' name='dynet_max_channels' type='number' min='1' max='"));
  pageWrite(String((int)DYNET_MAX_CHANNELS));  // enforced by compile-time cap (16 on ESP8266, 8 default)
  pageWrite(F("' value='"));
  pageWrite(String(defCh));
  pageWrite(F("'><div class='muted'>Channels probed per area during sweep (e.g. 16 = check channels 1&ndash;16 in every area).</div></div>"));

  pageWrite(F("<div>DyNet Max Areas</div><div>"
              "<input class='in' name='dynet_max_areas' type='number' min='2' max='"));
  pageWrite(String((int)DYNET_MAX_AREAS));   // 32 on ESP8266, 64 on ESP32 — from compile-time constant
  pageWrite(F("' value='"));
  pageWrite(String(defAr));
#if defined(ESP8266)
  pageWrite(F("'><div class='muted'>Highest area number to store and sweep (1..N). Max 32 on ESP8266.</div></div>"));
#else
  pageWrite(F("'><div class='muted'>Highest area number to store and sweep (1..N).</div></div>"));
#endif

  
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
              "<input class='in' id='staSsid' name='wifi_ssid' type='text' required value='"));
  pageWrite(String(cfg.wifi_ssid));
  pageWrite(F("'> <select class='in' id='ssidSelect'><option value=''> Select from scan </option></select>"
              " <button id='scanBtn' class='btn' type='button' onclick='doScan()'>Scan</button>"
              "</div>"));

  pageWrite(F("<div>Wi‑Fi Password</div><div><input class='in' name='wifi_pass' type='password' value='"));
  pageWrite(String(cfg.wifi_pass));
  pageWrite(F("'></div>"));

  pageWrite(F("<div>Fallback SSID</div><div><input class='in' name='wifi_ssid2' type='text' placeholder='optional' value='"));
  pageWrite(String(cfg.wifi_ssid2));
  pageWrite(F("'></div>"));
  pageWrite(F("<div>Fallback Password</div><div><input class='in' name='wifi_pass2' type='password' value='"));
  pageWrite(String(cfg.wifi_pass2));
  pageWrite(F("'></div>"));

  // AP
  pageWrite(F("<div>AP SSID</div><div><input class='in' name='ap_ssid' type='text' required value='"));
  pageWrite(apSsid);
  pageWrite(F("'></div>"));
  pageWrite(F("<div>AP Password</div><div><input class='in' name='ap_pass' type='password' value='"));
  pageWrite(apPass);
  pageWrite(F("'></div>"));

  pageWrite(F("</div></div>"));

  // MQTT
  pageWrite(F("<div class='card'><div class='form-table'>"));
  pageWrite(F("<div>MQTT Server</div><div><input class='in' name='mqtt_server' type='text' required value='"));
  if (mqtt.connected()) pageWrite(String(cfg.mqtt_server));
  pageWrite(F("'></div>"));
  pageWrite(F("<div>MQTT Port</div><div><input class='in' name='mqtt_port' type='number' required min='1' max='65535' value='"));
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
  pageWrite(F("<div>Web Log</div><div><label><input name='log_web' type='checkbox' "));
  if(cfg.log_web) pageWrite(F("checked"));
  pageWrite(F("> Enable</label></div>"));
  pageWrite(F("</div></div>"));

  // Save row
  pageWrite(F("<div class='form-inline' style='margin-top:10px'>"
                "<button class='btn' type='submit'>Save & Reboot</button>"
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
  pageWrite(F("<a class='btn' href='/import' title='Import area/channel names from Dynalite LogicalExport XML'>&#x2B07; Import from XML</a>"));
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

void sendRebootingPage(const char* title, const char* msg, int seconds, int /*delayMs*/) {
  // Delegate to the String overload which has JS countdown + /status polling.
  // All callers (config save, firmware update, restore) get consistent behaviour.
  sendRebootingPage(
    String(title && *title ? title : "Rebooting"),
    String(msg   && *msg   ? msg   : "Device will restart to apply changes."),
    seconds < 0 ? 0 : seconds,
    5000U   // start polling /status after 5 s
  );
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
  client.setBufferSizes(1024, 512);
#endif
  // setInsecure() on both platforms: GitHub releases are public binaries so
  // skipping cert pinning is acceptable and avoids cert-rotation failures.
  // (ESP32 setCACert with concatenated PEMs is unreliable across SDK versions.)
  client.setInsecure();
}

static bool checkHeapForTls(String& outErr) {
#if defined(ESP8266)
  // Use getMaxFreeBlockSize(): total free heap can look fine while fragmentation
  // means no single block is large enough for the required allocations.
  // For a version CHECK (not an install), Update.begin() is NOT called so no
  // 4 KB sector buffer is needed.  Budget:
  //   BearSSL MFLN 1024 handshake  → ~5 KB (state + I/O buffers)
  //   DynamicJsonDocument(1536)    → ~1.5 KB
  //   HTTPClient + String overhead → ~1.5 KB
  //   Total contiguous needed      → ~8 KB
  // handleFwCheckUpdate() calls logs_clear()+mqtt.disconnect() before us so
  // the number here reflects the freed state.
  uint32_t maxBlock = maxFreeBlock();
  if (maxBlock < 8000) {
    outErr = String("Heap fragmented (largest free block ")
           + String(maxBlock)
           + " bytes, need 8 KB). Reboot and retry immediately after boot.";
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



static void handleFwSetPending() {
  String url = server.arg("url");
  String tag = server.arg("tag");
  if (url.length() && tag.length()) {
    gPendingUpdateUrl  = url;
    gPendingUpdateTag  = tag;
    gPendingUpdateSize = 0;
  }
  server.send(200, "text/plain", "ok");
}

static void handleFwGet() {
  pageBegin("Firmware Update");
  pageWrite(F("<h2 style='margin:0 0 14px;font-size:17px;font-weight:700'>Firmware Update</h2>"));

  // ── Post-install result banner ──────────────────────────────────────────
  if (gOtaPhase == FWOTA_SUCCESS) {
    pageWrite(F("<div class='card' style='border-left:4px solid green'>"
                "<div class='title' style='color:green'>Update Successful</div>"
                "<p>Firmware updated. Current version: <b>"));
    pageWrite(String(HA_SW_VERSION));
    pageWrite(F("</b></p></div>"));
    gOtaPhase = FWOTA_IDLE;
  } else if (gOtaPhase == FWOTA_FAILED) {
    pageWrite(F("<div class='card' style='border-left:4px solid red'>"
                "<div class='title' style='color:red'>Update Failed</div><p>"));
    pageWrite(gOtaError.length() ? gOtaError : String("Unknown error"));
    pageWrite(F("</p></div>"));
    gOtaPhase = FWOTA_IDLE;
  }

  // ── Main card ───────────────────────────────────────────────────────────
  pageWrite(F("<div class='card'>"
              // Two-column version row
              "<div style='display:flex;justify-content:space-between;align-items:flex-start;flex-wrap:wrap;gap:12px'>"
                "<div>"
                  "<div style='font-size:12px;color:var(--muted)'>Current version</div>"
                  "<div style='font-size:26px;font-weight:700;line-height:1.3'>v"));
  pageWrite(String(HA_SW_VERSION));
  pageWrite(F("</div>"
                "</div>"
                "<div id='latestCol' style='text-align:right;display:none'>"
                  "<div style='font-size:12px;color:var(--muted)'>Latest release</div>"
                  "<div id='latestVer' style='font-size:26px;font-weight:700;line-height:1.3'></div>"
                  "<div id='latestBtns' style='margin-top:6px;display:flex;gap:6px;justify-content:flex-end;flex-wrap:wrap'></div>"
                "</div>"
              "</div>"
              // Status line
              "<div id='verStatus' style='margin-top:10px;font-size:13px;color:var(--muted)'>Checking...</div>"
              "<hr style='border:none;border-top:1px solid var(--border);margin:14px 0'>"
              "<div class='title' style='font-size:14px;margin-bottom:8px'>Upload Firmware File</div>"
              "<form id='fwForm'>"
              "<div class='row' style='gap:8px;flex-wrap:wrap'>"
                "<input type='file' id='fwFile' name='fw' accept='.bin' required>"
                "<button type='submit' id='uploadBtn' class='btn'>Flash</button>"
              "</div></form>"
              "<div id='uploadProgress' style='display:none;margin-top:10px'>"
                "<progress id='uploadBar' value='0' max='100' style='width:100%;height:16px'></progress>"
                "<div style='display:flex;justify-content:space-between;font-size:12px;margin-top:3px'>"
                  "<span id='uploadPct'>0%</span><span id='uploadStatus'></span>"
                "</div>"
              "</div>"));

#if !defined(ESP8266)
  pageWrite(F("<div id='prg' style='display:none;margin-top:12px'>"
              "<table style='border-collapse:collapse;width:100%;margin-top:8px'>"
              "<tr><td id='p1' style='padding:4px 0'>[ ] Connecting to download server</td></tr>"
              "<tr><td id='p2' style='padding:4px 0;color:#aaa'>[ ] Downloading firmware</td></tr>"
              "<tr><td id='p3' style='padding:4px 0;color:#aaa'>[ ] Flashing &amp; verifying</td></tr>"
              "</table>"
              "<div id='pres' style='margin-top:10px'></div>"
              "</div>"));
#endif

  pageWrite(F("<p style='opacity:.7;margin:12px 0 0'>Do not power off during update. Device will reboot automatically.</p>"
              "</div>"));

  // ── JavaScript ─────────────────────────────────────────────────────────
  pageWrite(F("<script>var _cur='"));
  pageWrite(String(HA_SW_VERSION));
#if !defined(ESP8266)
  pageWrite(F("';var _esp32=true;"));
#else
  pageWrite(F("';var _esp32=false;"));
#endif

  // Semver compare + platform bin picker + auto version check on load
  pageWrite(F(
    "function _cmp(a,b){"
      "var pa=a.replace(/^v/,'').split('.').map(Number),"
          "pb=b.replace(/^v/,'').split('.').map(Number);"
      "for(var i=0;i<3;i++){var d=(pa[i]||0)-(pb[i]||0);if(d)return d;}return 0;"
    "}"
    "function _bin(assets){"
      "var b=assets.filter(function(a){return a.name.toLowerCase().endsWith('.bin');});"
      "if(!b.length)return null;"
      "var p=b.filter(function(a){"
        "var n=a.name.toLowerCase();"
        "return _esp32?(n.indexOf('esp32')>=0&&n.indexOf('8266')<0):(n.indexOf('8266')>=0);"
      "});"
      "return p.length?p[0]:b[0];"
    "}"
    "(function(){"
      "var st=document.getElementById('verStatus');"
      "fetch('https://api.github.com/repos/hollako/Dynet-MQTT-Home-Assistant-Gateway/releases/latest',{cache:'no-store'})"
      ".then(function(r){return r.json();})"
      ".then(function(d){"
        "var tag=(d.tag_name||'').replace(/^v/,'');"
        "if(!tag){st.innerHTML='<span style=\"color:#aaa\">Version check failed</span>';return;}"
        // Always populate the right column
        "document.getElementById('latestVer').textContent='v'+tag;"
        "var asset=_bin(d.assets||[]);"
        "if(asset){"
          "var isNewer=_cmp(tag,_cur.replace(/^v/,''))>0;"
          "var html='<a class=\"btn\" href=\"'+asset.browser_download_url+'\" target=\"_blank\" rel=\"noopener\" style=\"font-size:13px\">Download latest .bin</a>';"
          "if(_esp32&&isNewer)html+=' <button id=\"instBtn\" class=\"btn\" style=\"font-size:13px\" data-u=\"'+asset.browser_download_url+'\" data-t=\"'+tag+'\" onclick=\"_inst(this.dataset.u,this.dataset.t)\">&#8679; Install</button>';"
          "document.getElementById('latestBtns').innerHTML=html;"
        "}"
        "document.getElementById('latestCol').style.display='';"
        // Status line
        "var isNewer=_cmp(tag,_cur.replace(/^v/,''))>0;"
        "st.innerHTML=isNewer"
          "?\"<span style='color:#f59e0b;font-weight:600;font-size:16px'>&#9650; New version available - click Download to get the firmware, Choose the file and click Flash</span>\""
          ":'<span style=\"color:#19c37d;font-weight:600;font-size:16px\">&#10003; Firmware is up to date</span>';"
      "})"
      ".catch(function(){st.innerHTML='<span style=\"color:#aaa\">Version check failed</span>';});"
    "})();"
    // XHR upload with progress bar
    "document.getElementById('fwForm').addEventListener('submit',function(e){"
      "e.preventDefault();"
      "var f=document.getElementById('fwFile').files[0];if(!f)return;"
      "var xhr=new XMLHttpRequest();"
      "document.getElementById('uploadProgress').style.display='block';"
      "document.getElementById('uploadBtn').disabled=true;"
      "xhr.upload.onprogress=function(e){"
        "if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);"
          "document.getElementById('uploadBar').value=p;"
          "document.getElementById('uploadPct').textContent=p+'%';}"
      "};"
      "xhr.onload=function(){"
        "if(xhr.status===200){"
          "document.getElementById('uploadBar').value=100;"
          "document.getElementById('uploadPct').textContent='100%';"
          "document.getElementById('uploadStatus').innerHTML='<span style=\"color:green\">Flash complete! Rebooting...</span>';"
          "setTimeout(function(){location.href='/fw';},15000);"
        "}else{"
          "document.getElementById('uploadStatus').innerHTML='<span style=\"color:red\">Error '+xhr.status+'</span>';"
          "document.getElementById('uploadBtn').disabled=false;}"
      "};"
      "xhr.onerror=function(){"
        "document.getElementById('uploadStatus').innerHTML='<span style=\"color:red\">Upload failed</span>';"
        "document.getElementById('uploadBtn').disabled=false;};"
      "var fd=new FormData();fd.append('fw',f);"
      "xhr.open('POST','/fw');xhr.send(fd);"
    "});"
  ));

#if !defined(ESP8266)
  // ESP32: remote install via set-pending + SSE progress
  pageWrite(F(
    "function _inst(url,tag){"
      "var btn=document.getElementById('instBtn');if(btn)btn.disabled=true;"
      "document.getElementById('prg').style.display='block';"
      "document.getElementById('p1').textContent='>>> Connecting...';"
      "fetch('/fw/set-pending',{method:'POST',"
        "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "body:'url='+encodeURIComponent(url)+'&tag='+encodeURIComponent(tag)})"
      ".then(function(){doUpd();})"
      ".catch(function(){document.getElementById('pres').innerHTML='<span style=\"color:red\">Error</span>';});"
    "}"
    "var lastPhase='';"
    "function doUpd(){"
      "fetch('/fw/update',{method:'POST'})"
      ".then(function(r){"
        "var rd=r.body.getReader(),dc=new TextDecoder(),buf='';"
        "function pump(){"
          "rd.read().then(function(x){"
            "if(x.done){"
              "if(lastPhase==='downloading'){"
                "document.getElementById('p2').textContent='[..] Flashing (please wait)...';"
                "document.getElementById('pres').innerHTML='<p>Writing firmware. Will reboot.<br><b>Redirecting in 90s.</b></p>';"
                "setTimeout(function(){location.href='/fw';},90000);"
              "}"
              "return;"
            "}"
            "buf+=dc.decode(x.value,{stream:true});"
            "var parts=buf.split('\\n\\n');buf=parts.pop();"
            "parts.forEach(function(p){"
              "if(p.slice(0,6)==='data: '){try{onEvt(JSON.parse(p.slice(6)));}catch(e){}}"
            "});"
            "pump();"
          "});"
        "}"
        "pump();"
      "})"
      ".catch(function(e){"
        "document.getElementById('pres').innerHTML='<span style=\"color:red\">Connection error: '+e+'</span>';"
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
        "document.getElementById('pres').innerHTML='<span style=\"color:red\"><b>Failed:</b> '+d.error+'</span>';"
      "}"
    "}"
  ));
#endif

  pageWrite(F("</script>"));
  pageEnd();
}

static void handleFwCheckUpdate() {
  // Free as much heap as possible before opening a TLS connection.
  // BearSSL + MFLN needs ~5 KB contiguous; MQTT TCP + log buffer compete for the same pool.
  if (mqtt.connected()) {
    mqtt.disconnect();
    delay(80);   // let LwIP release the TCP PCB
    yield();
  }
  logs_clear();  // releases up to 2 KB of log String heap
  yield();

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
                          15, 1200);
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
  if (server.hasArg("wifi_ssid")  && server.arg("wifi_ssid").length())  setStr(cfg.wifi_ssid,  sizeof(cfg.wifi_ssid),  server.arg("wifi_ssid"));
  if (server.hasArg("wifi_pass"))                                        setStr(cfg.wifi_pass,  sizeof(cfg.wifi_pass),  server.arg("wifi_pass"));   // optional — allow clearing
  if (server.hasArg("wifi_ssid2"))                                       setStr(cfg.wifi_ssid2, sizeof(cfg.wifi_ssid2), server.arg("wifi_ssid2")); // optional fallback
  if (server.hasArg("wifi_pass2"))                                       setStr(cfg.wifi_pass2, sizeof(cfg.wifi_pass2), server.arg("wifi_pass2")); // optional fallback
  if (server.hasArg("mqtt_server") && server.arg("mqtt_server").length()) setStr(cfg.mqtt_server, sizeof(cfg.mqtt_server), server.arg("mqtt_server"));
  if (server.hasArg("mqtt_port")   && server.arg("mqtt_port").length())  cfg.mqtt_port = server.arg("mqtt_port").toInt();
  if (server.hasArg("mqtt_user"))   setStr(cfg.mqtt_user, sizeof(cfg.mqtt_user), server.arg("mqtt_user")); // optional — allow clearing
  if (server.hasArg("mqtt_pass"))   setStr(cfg.mqtt_pass, sizeof(cfg.mqtt_pass), server.arg("mqtt_pass")); // optional — allow clearing
  if (server.hasArg("ap_ssid")     && server.arg("ap_ssid").length())    apSsid = server.arg("ap_ssid");
  if (server.hasArg("ap_pass"))     apPass = server.arg("ap_pass");                                        // optional — allow clearing
  cfg.ha_discovery  = server.hasArg("ha_discovery");
  cfg.log_web       = server.hasArg("log_web");
  if (!cfg.log_web) logs_clear();  // immediately free heap when disabled
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
                    15, 5000);
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

  sendRebootingPage("Restore complete","Settings applied. The device will come back online shortly.",15,5000);
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
  secureClient.setInsecure(); // consistent with configureGithubTls() — public binary, no pinning needed
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
    server.on("/fw/set-pending", HTTP_POST, handleFwSetPending);
    server.on("/fw/check", HTTP_POST, handleFwCheckUpdate);
    server.on("/fw/update", HTTP_POST, handleFwDoUpdate);

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
       "<label>SSID</label><input name='wifi_ssid' type='text' required value=''>"
       "<label>Password</label>"
       "<div style='display:flex;gap:6px;align-items:center'>"
         "<input id='wpPass' name='wifi_pass' type='password' value='' style='flex:1'>"
         "<button type='button' onclick=\"var i=document.getElementById('wpPass');i.type=i.type==='password'?'text':'password';this.textContent=i.type==='password'?'Show':'Hide';\" style='padding:8px 12px;border:1px solid #ccc;border-radius:8px;cursor:pointer;background:#fafafa;white-space:nowrap'>Show</button>"
       "</div>"
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
  if (server.hasArg("wifi_ssid") && server.arg("wifi_ssid").length()) setStr(cfg.wifi_ssid, sizeof(cfg.wifi_ssid), server.arg("wifi_ssid"));
  if (server.hasArg("wifi_pass")) setStr(cfg.wifi_pass, sizeof(cfg.wifi_pass), server.arg("wifi_pass")); // optional — allow empty (open network)
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
    15,    // countdown seconds
    5000   // wait before first /status probe
  );
  if (server.client()) { server.client().flush(); }
  delay(150);
  scheduleReboot(2200);
}

// -------------- Import Dynalite XML page --------------
static void handleImportGet() {
  pageBegin("Import XML");
  pageWrite(F("<h1>Import Dynalite Project XML</h1>"));
  pageWrite(F(
    "<div class='card'>"
      "<p>Select your Dynalite LogicalExport XML file. The browser parses it locally — "
         "only the extracted data is sent to the device. Area names, channel names, preset names "
         "and area types (Lighting/Curtain/HVAC) will be imported.</p>"
      "<div class='row' style='gap:10px;align-items:center;flex-wrap:wrap'>"
        "<input type='file' id='xf' accept='.xml' onchange='parseXml(this)'>"
        "<span id='xstat' class='muted'></span>"
      "</div>"
      "<div id='preview' style='margin-top:14px'></div>"
      "<div id='importWrap' style='display:none;margin-top:12px;display:flex;gap:10px;align-items:center;flex-wrap:wrap'>"
        "<button class='btn' id='importBtn' onclick='doImport()'>&#x2B07; Import</button>"
      "</div>"
      "<div id='progress' style='margin-top:10px'></div>"
    "</div>"
  ));
  pageWrite(F("<script>"
  "var parsedAreas=[];"

  // ── Selection helpers ────────────────────────────────────────────────────
  "function toggleAll(cb){"
    "document.querySelectorAll('.asel').forEach(function(c){c.checked=cb.checked;});"
    "updateBtn();"
  "}"
  "function updateBtn(){"
    "var n=document.querySelectorAll('.asel:checked').length;"
    "var b=document.getElementById('importBtn');"
    "if(b) b.textContent='⬇ Import '+n+' Area'+(n!==1?'s':'');"
  "}"

  // ── XML parser ───────────────────────────────────────────────────────────
  "function parseXml(inp){"
    "var f=inp.files[0]; if(!f) return;"
    "document.getElementById('xstat').textContent='Parsing…';"
    "document.getElementById('preview').innerHTML='';"
    "document.getElementById('importWrap').style.display='none';"
    "var rd=new FileReader();"
    "rd.onload=function(e){"
      "try{"
        "var doc=(new DOMParser()).parseFromString(e.target.result,'text/xml');"
        "var parseErr=doc.querySelector('parsererror');"
        "if(parseErr){document.getElementById('xstat').textContent='XML parse error';return;}"
        "var aNodes=doc.getElementsByTagName('Area');"
        "parsedAreas=[];"
        "for(var i=0;i<aNodes.length;i++){"
          "var a=aNodes[i];"
          "var aId=parseInt(a.getAttribute('id')||'0');"
          "if(aId<1||aId>255) continue;"
          "var aName=a.getAttribute('name')||('Area '+aId);"
          "var cat=(a.getAttribute('category')||'').toLowerCase();"
          "var type=(cat==='lighting')?0:(cat==='custom')?1:(cat==='hvac')?2:0;"
          "var pc=parseInt(a.getAttribute('preset_count')||'4');"
          "if(pc<1) pc=4;"
          "if(type===0&&pc>16) pc=16;"

          // channels — direct children only
          "var channels=[];"
          "var kids=a.childNodes;"
          "for(var j=0;j<kids.length;j++){"
            "var n=kids[j];"
            "if(n.nodeName==='Channel'){"
              "var cId=parseInt(n.getAttribute('id')||'0');"
              "var cNm=n.getAttribute('name')||'';"
              "if(cId>=1&&cNm) channels.push({id:cId,name:cNm});"
            "}"
          "}"

          // presets — direct children only
          "var presets=[];"
          "for(var j=0;j<kids.length;j++){"
            "var n=kids[j];"
            "if(n.nodeName==='Preset'){"
              "var pId=parseInt(n.getAttribute('id')||'0');"
              "var pNm=n.getAttribute('name')||'';"
              "if(pId>=1&&pNm) presets.push({id:pId,name:pNm});"
            "}"
          "}"

          // Curtain: detect open/close/stop presets
          "var openP=0,closeP=0,stopP=0;"
          "if(type===1){"
            "for(var j=0;j<presets.length;j++){"
              "var pn=presets[j].name.toUpperCase();"
              "if(pn==='OPEN'||pn==='UP') openP=presets[j].id;"
              "else if(pn==='CLOSE'||pn==='DOWN') closeP=presets[j].id;"
              "else if(pn==='STOP') stopP=presets[j].id;"
            "}"
            // Positional fallback: Dynalite default is CLOSE=1,STOP=2,OPEN=3
            "if(!openP&&!closeP&&!stopP&&presets.length>=3){"
              "closeP=presets[0].id; stopP=presets[1].id; openP=presets[2].id;"
            "}"
          "}"

          "parsedAreas.push({area:aId,name:aName,type:type,"
            "preset_count:pc,channels:channels,presets:presets,"
            "open:openP,close:closeP,stop:stopP});"
        "}"
        "parsedAreas.sort(function(a,b){return a.area-b.area;});"

        // Summary line
        "var nL=parsedAreas.filter(function(a){return a.type===0;}).length;"
        "var nC=parsedAreas.filter(function(a){return a.type===1;}).length;"
        "var nH=parsedAreas.filter(function(a){return a.type===2;}).length;"
        "var nCh=parsedAreas.reduce(function(s,a){return s+a.channels.length;},0);"
        "document.getElementById('xstat').textContent=parsedAreas.length+' areas found';"

        // Preview table with per-row checkboxes
        "var h='<p><b>'+parsedAreas.length+' areas</b>: '+nL+' Lighting, '+nC+' Curtain, '+nH+' HVAC — '+nCh+' channels total.</p>';"
        "h+=\"<div style='max-height:320px;overflow-y:auto'><table style='width:100%;border-collapse:collapse;font-size:13px'>\";"
        "h+='<thead><tr>"
          "<th style=\\'padding:4px 6px;border-bottom:1px solid var(--border)\\'>"
            "<input type=\\'checkbox\\' id=\\'chkAll\\' checked title=\\'Select all / none\\' onchange=\\'toggleAll(this)\\'>"
          "</th>"
          "<th style=\\'text-align:left;border-bottom:1px solid var(--border);padding:4px\\'>Area</th>"
          "<th style=\\'text-align:left;border-bottom:1px solid var(--border);padding:4px\\'>Name</th>"
          "<th style=\\'text-align:left;border-bottom:1px solid var(--border);padding:4px\\'>Type</th>"
          "<th style=\\'text-align:right;border-bottom:1px solid var(--border);padding:4px\\'>Ch</th>"
          "<th style=\\'text-align:right;border-bottom:1px solid var(--border);padding:4px\\'>Presets</th>"
        "</tr></thead><tbody>';"
        "var tnames=['Lights','Curtain','HVAC'];"
        "for(var i=0;i<parsedAreas.length;i++){"
          "var a=parsedAreas[i];"
          "h+='<tr>';"
          "h+='<td style=\\'padding:3px 6px\\'><input type=\\'checkbox\\' class=\\'asel\\' value=\\''+i+'\\'  checked onchange=\\'updateBtn()\\'></td>';"
          "h+='<td style=\\'padding:3px 4px\\'>'+a.area+'</td>';"
          "h+='<td style=\\'padding:3px 4px\\'>'+a.name+'</td>';"
          "h+='<td style=\\'padding:3px 4px\\'>'+(tnames[a.type]||'?')+'</td>';"
          "h+='<td style=\\'padding:3px 4px;text-align:right\\'>'+a.channels.length+'</td>';"
          "h+='<td style=\\'padding:3px 4px;text-align:right\\'>'+a.presets.length+'</td></tr>';"
        "}"
        "h+='</tbody></table></div>';"
        "document.getElementById('preview').innerHTML=h;"
        "if(parsedAreas.length){"
          "document.getElementById('importWrap').style.display='flex';"
          "updateBtn();"
        "}"
      "}catch(ex){"
        "document.getElementById('xstat').textContent='Error: '+ex;"
      "}"
    "};"
    "rd.readAsText(f);"
  "}"

  // ── Import driver ────────────────────────────────────────────────────────
  "function doImport(){"
    // Build list from checked rows only
    "var sel=parsedAreas.filter(function(_,i){"
      "var cb=document.querySelector('.asel[value=\"'+i+'\"]');"
      "return cb&&cb.checked;"
    "});"
    "if(!sel.length){alert('No areas selected.');return;}"
    "document.getElementById('importWrap').style.display='none';"
    "var prog=document.getElementById('progress');"
    "prog.innerHTML='<p>Starting import…</p>';"
    "fetch('/api/import_start',{method:'POST'})"
    ".then(function(){"
      "var idx=0,ok=0,fail=0;"
      "function next(){"
        "if(idx>=sel.length){"
          "prog.innerHTML='<p>Finishing…</p>';"
          "fetch('/api/import_done',{method:'POST'})"
          ".then(function(r){return r.json();})"
          ".then(function(){"
            "prog.innerHTML='<p><b style=\"color:green\">✓ Import complete!</b> '+"
              "ok+' areas imported, '+fail+' failed. '+"
              "'<a class=\"btn\" href=\"/\" style=\"padding:4px 14px\">Go to Home</a></p>';"
          "})"
          ".catch(function(){"
            "prog.innerHTML+='<p style=\"color:red\">Warning: import_done request failed.</p>';"
          "});"
          "return;"
        "}"
        "var a=sel[idx];"
        "prog.innerHTML='<p>Importing <b>'+a.name+'</b> (Area '+a.area+') — '+(idx+1)+' / '+sel.length+'</p>';"
        "fetch('/api/import_area',{"
          "method:'POST',"
          "headers:{'Content-Type':'application/json'},"
          "body:JSON.stringify(a)"
        "})"
        ".then(function(r){return r.json();})"
        ".then(function(j){if(j.ok)ok++;else fail++;idx++;setTimeout(next,30);})"
        ".catch(function(){fail++;idx++;setTimeout(next,80);});"
      "}"
      "next();"
    "})"
    ".catch(function(e){"
      "prog.innerHTML='<p style=\"color:red\">Failed to start import: '+e+'</p>';"
      "document.getElementById('importWrap').style.display='flex';"
    "});"
  "}"
  "</script>"
  ));
  pageEnd();
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
  server.on("/reboot", HTTP_GET, [](){ sendRebootingPage("Reboot requested","",15,2000); scheduleReboot(800); });

  // Serve company logo from LittleFS (place PNG at data/logo.png and upload filesystem)
  server.on("/logo.png", HTTP_GET, [](){
    if (!LittleFS.exists("/logo.png")) { server.send(404); return; }
    File f = LittleFS.open("/logo.png", "r");
    server.sendHeader("Cache-Control", "no-store");
    server.streamFile(f, "image/png");
    f.close();
  });

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
    areasSweepPass    = 0;          // 1 pass only — user can press again if needed
    areasSweepNextAt  = millis() + 50;
    LOGF("[DyNet] sweep START (1 pass) requested from WebUI\n");
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // Update preset count — per-area, saves to entities and republishes HA discovery for that area
  server.on("/api/set_preset_count", HTTP_POST, [](){
    using namespace DynetEntities;
    if (!server.hasArg("n") || !server.hasArg("area")) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    uint8_t area = (uint8_t)server.arg("area").toInt();
    int     nRaw = server.arg("n").toInt();
    int ai = em.findArea(area);
    if (ai < 0) { server.send(404, "application/json", "{\"ok\":false,\"error\":\"area not found\"}"); return; }
    // Enforce cap for light areas
    int maxN = (em.areaAt(ai).areaType == AREA_LIGHTS) ? MAX_LIGHT_PRESETS : 128;
    uint8_t n = (uint8_t)constrain(nRaw, 1, maxN);
    em.areaAtMut(ai).presetCount = n;
    saveEntities();
    publishHADiscoveryForArea(area);  // republish only this area's preset select
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // Set preset name for a light area
  server.on("/api/set_preset_name", HTTP_POST, [](){
    using namespace DynetEntities;
    if (!server.hasArg("area") || !server.hasArg("preset") || !server.hasArg("name")) {
      server.send(400, "application/json", "{\"ok\":false}"); return;
    }
    uint8_t area   = (uint8_t)server.arg("area").toInt();
    uint8_t preset = (uint8_t)server.arg("preset").toInt();
    String  name   = server.arg("name");
    if (preset < 1 || preset > MAX_LIGHT_PRESETS) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"preset out of range\"}"); return;
    }
    em.setPresetName(area, preset, name.c_str());
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

  // HVAC setpoint step (0.5 or 1.0 °C)
  server.on("/api/hvac/set_step", HTTP_POST, [](){
    using namespace DynetEntities;
    if (!server.hasArg("area") || !server.hasArg("step")) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    uint8_t area = (uint8_t)server.arg("area").toInt();
    float   step = server.arg("step").toFloat();
    step = (step >= 1.0f) ? 1.0f : 0.5f;   // only two valid values
    int ai = em.findArea(area);
    if (ai < 0 || !em.areaAt(ai).hvac) { server.send(404, "application/json", "{\"ok\":false}"); return; }
    em.areaAtMut(ai).hvac->setptStep = step;
    saveEntities();
    publishHADiscoveryForArea(area);   // re-publish climate entity with new temp_step
    server.send(200, "application/json", "{\"ok\":true}");
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
      int rssi = (int)WiFi.RSSI();
      doc["rssi"]     = rssi;
      doc["rssi_pct"] = constrain(2 * (rssi + 100), 0, 100);
    } else { doc["sta_ip"] = ""; doc["sta_ssid"] = ""; doc["rssi"] = 0; doc["rssi_pct"] = 0; }
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

  // ---- Dynalite XML Import ----
  server.on("/api/area_detail", HTTP_GET, handleAreaDetail);
  server.on("/import", HTTP_GET, handleImportGet);

  // Begin bulk import: suppress per-item MQTT publish + LittleFS save
  server.on("/api/import_start", HTTP_POST, [](){
    DynetEntities::em.setLoading(true);
    server.sendHeader("Cache-Control","no-store");
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // Apply one area's data (JSON body)
  server.on("/api/import_area", HTTP_POST, [](){
    using namespace DynetEntities;
    String body = server.arg("plain");
    if (body.isEmpty()) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}");
      return;
    }
    DynamicJsonDocument doc(3072);
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"json parse\"}");
      return;
    }
    uint8_t area = doc["area"] | 0;
    if (area < 1) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad area\"}");
      return;
    }
    const char* aName = doc["name"] | "";
    uint8_t     aType = doc["type"] | 0;
    uint8_t     pCount = doc["preset_count"] | 4;

    // Create / touch area (loading=true suppresses publish+save inside touchArea)
    int ai = em.touchArea(area);
    if (ai < 0) {
      server.send(200, "application/json", "{\"ok\":false,\"error\":\"area table full\"}");
      return;
    }
    auto& as = em.areaAtMut(ai);

    // --- Name ---
    if (aName && aName[0]) {
      strncpy(as.name, aName, sizeof(as.name) - 1);
      as.name[sizeof(as.name) - 1] = '\0';
    }

    // --- Type + allocation ---
    AreaType newType = (aType == 1) ? AREA_CURTAIN : (aType == 2) ? AREA_HVAC : AREA_LIGHTS;
    if (newType != as.areaType) {
      // Free old type resources
      if (as.areaType == AREA_CURTAIN && newType != AREA_CURTAIN) {
        if (as.curtains) { delete[] as.curtains; as.curtains = nullptr; }
      }
      if (as.areaType == AREA_HVAC && newType != AREA_HVAC) {
        if (as.hvac) { delete as.hvac; as.hvac = nullptr; }
      }
      as.areaType = newType;
      // Allocate new type resources
      if (newType == AREA_CURTAIN && !as.curtains) {
        as.curtains = new (std::nothrow) AreaCurtainEntry[MAX_CURTAINS_PER_AREA];
        if (as.curtains) {
          for (int k = 0; k < MAX_CURTAINS_PER_AREA; k++) as.curtains[k] = AreaCurtainEntry{};
        }
      }
      if (newType == AREA_HVAC && !as.hvac) {
        as.hvac = new (std::nothrow) HvacConfig{};
        if (as.hvac && (!as.hasSetpt || isnan(as.setptC))) {
          as.hasSetpt = true; as.setptC = 22.0f;
        }
      }
    }

    // --- Lights: preset count + preset names + channels ---
    if (newType == AREA_LIGHTS) {
      if (pCount < 1) pCount = 4;
      if (pCount > MAX_LIGHT_PRESETS) pCount = MAX_LIGHT_PRESETS;
      as.presetCount = pCount;

      // Preset names
      for (JsonObject pr : doc["presets"].as<JsonArray>()) {
        uint8_t pid = pr["id"] | 0;
        const char* pnm = pr["name"] | "";
        if (pid >= 1 && pid <= MAX_LIGHT_PRESETS && pnm && pnm[0]) {
          if (!as.presets) {
            as.presets = new (std::nothrow) AreaPresetNames{};
          }
          if (as.presets) {
            strncpy(as.presets->n[pid - 1], pnm, sizeof(as.presets->n[pid - 1]) - 1);
            as.presets->n[pid - 1][sizeof(as.presets->n[pid - 1]) - 1] = '\0';
          }
        }
      }

      // Channels (id is 1-based in XML → 0-based channel0 for DyNet)
      for (JsonObject ch : doc["channels"].as<JsonArray>()) {
        uint8_t chId = ch["id"] | 0;
        const char* chnm = ch["name"] | "";
        if (chId >= 1) {
          int ci = em.touchChannel(area, chId - 1);  // loading=true: no publish+save
          if (ci >= 0 && chnm && chnm[0]) {
            strncpy(em.channelAtMut(ci).name, chnm, sizeof(em.channelAtMut(ci).name) - 1);
            em.channelAtMut(ci).name[sizeof(em.channelAtMut(ci).name) - 1] = '\0';
          }
        }
      }
    }

    // --- Curtain: populate first curtain entry ---
    if (newType == AREA_CURTAIN && as.curtains) {
      uint8_t openP  = doc["open"]  | 3;
      uint8_t closeP = doc["close"] | 1;
      uint8_t stopP  = doc["stop"]  | 2;
      // Reset slot 0 with imported data
      auto& ce = as.curtains[0];
      ce = AreaCurtainEntry{};
      ce.used = true;
      strncpy(ce.name, aName && aName[0] ? aName : "Curtain 1", sizeof(ce.name) - 1);
      ce.name[sizeof(ce.name) - 1] = '\0';
      ce.openPreset  = openP  ? openP  : 3;
      ce.closePreset = closeP ? closeP : 1;
      ce.stopPreset  = stopP  ? stopP  : 2;
    }

    server.sendHeader("Cache-Control","no-store");
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // End bulk import: save once + publish HA discovery for all areas
  server.on("/api/import_done", HTTP_POST, [](){
    using namespace DynetEntities;
    em.setLoading(false);
    saveEntities();
    // Publish HA discovery for every area
    for (int i = 0; i < em.areasCount(); i++) {
      publishHADiscoveryForArea(em.areaAt(i).area);
      yield();
    }
    server.sendHeader("Cache-Control","no-store");
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.begin();
}
