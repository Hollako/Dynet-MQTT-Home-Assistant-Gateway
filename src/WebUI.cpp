#include "Globals.h"
#include "ConfigStore.h"
#include "MqttManager.h"
#include "DynetBus.h"
#include "EntityManager.h"

// === HTTP server type & Update include ======================================
#if defined(ESP8266)
  #include <ESP8266WebServer.h>
  using HttpServer = ESP8266WebServer;
  #include <Updater.h>            // (alias of Update.h on ESP8266 cores)
  #define UPDATE_ABORT() do { Update.end(); } while(0)
#else
  #include <WebServer.h>
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

// Expose the route registrar (Call this from registerWebRoutes)
void registerFwRoutes();


extern DynetBus dynet;
namespace DynetEntities { extern EntityManager em; }

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
      ".grid2{display:grid;grid-template-columns:repeat(2,minmax(280px,1fr));gap:12px}"
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
      "</div>")
  );
}
static inline void pageWrite(const __FlashStringHelper* s){ server.sendContent(s); }
static inline void pageWrite(const String& s){ server.sendContent(s); }
static inline void pageEnd(){ server.sendContent(F("</div></body></html>")); }

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
  pageWrite(F(
        "<div class='card'>"
          "<div class='section-title'>DyNet • Send Setpoint (test)</div>"
          "<div class='form-inline' style='gap:10px;flex-wrap:wrap'>"
            "<label>Area</label>"
            "<input class='in' id='sp_area' type='number' min='1' max='255' value='2' style='width:90px'>"
            "<label>Setpoint (°C)</label>"
            "<input class='in' id='sp_temp' type='number' step='0.5' min='10' max='35' value='22.0' style='width:120px'>"
            "<button class='btn action' onclick='sendSP()'>Send</button>"
            "<span id='sp_status' class='muted'></span>"
          "</div>"
        "</div>"
      ));
      pageWrite(F(
        "<script>"
        "function sendSP(){"
        " const a=Number(document.getElementById('sp_area').value)||1;"
        " const t=Number(document.getElementById('sp_temp').value)||22;"
        " const s=document.getElementById('sp_status');"
        " s.textContent='Sending…';"
        " fetch('/api/area_req',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area='+a+'&do=set_setpoint&val='+t})"
        "  .then(r=>r.json()).then(j=>{s.textContent=j.ok?'Sent ✓':'Failed';}).catch(()=>{s.textContent='Error';});"
        "}"
        "</script>"
      ));
      
  pageWrite(F("<h1>Discovered Areas & Channels</h1>"));

  // If nothing yet, hint
  if (em.channelsCount() == 0 && em.areasCount() == 0) {
    pageWrite(F("<div class='card'>No DyNet traffic discovered yet. "
                "Operate your panels or press buttons to let the gateway learn Areas/Channels. "
                "You can also request levels from the Config page.</div>"));
  }

  // Render per-area cards
  for (int ai = 0; ai < em.areasCount(); ai++) {
  const AreaState& as = em.areaAt(ai);

  pageWrite(F("<div class='card'>"));
    // header
    pageWrite(F("<div class='row space'><div class='row' style='gap:10px'>"));
      pageWrite(F("<div class='title'>"));
        pageWrite(String("Area "));
        pageWrite(String(as.area));
      pageWrite(F("</div>"));

      // --- Preset pill (always rendered) + stable ID for live updates
      pageWrite(F("<span class='pill' id='preset_A"));
        pageWrite(String(as.area));
      pageWrite(F("'>"));
        pageWrite(String("Preset: "));
        pageWrite((as.preset0==0xFF) ? String("unknown") : String((int)as.preset0 + 1));
      pageWrite(F("</span>"));

      // --- Temp pill: render hidden if not present, give it a stable ID
      pageWrite(F("<span class='pill' id='temp_A"));
        pageWrite(String(as.area));
      if (as.hasTemp) {
        pageWrite(F("'>Temp: "));
        pageWrite(String(as.tempC,1));
        pageWrite(F(" °C</span>"));
      } else {
        pageWrite(F("' style='display:none'>Temp: – °C</span>"));
      }

      // --- Setpoint pill: render hidden if not present, give it a stable ID
      pageWrite(F("<span class='pill' id='setpt_A"));
        pageWrite(String(as.area));
      if (as.hasSetpt) {
        pageWrite(F("'>Setpoint: "));
        pageWrite(String(as.setptC,1));
        pageWrite(F(" °C</span>"));
      } else {
        pageWrite(F("' style='display:none'>Setpoint: – °C</span>"));
      }

    pageWrite(F("</div>"));
    pageWrite(F("</div>"));
    // area actions
    pageWrite(F("<div class='row'>"
                  "<form method='POST' action='/area/save_preset'>"
                    "<input type='hidden' name='area' value='"));
      pageWrite(String(as.area));
    pageWrite(F("'>"
                    "<button class='btn prog' type='submit'>Save Current Preset</button>"
                  "</form>"
                  "<button class='btn action' onclick=\"fetch('/api/area_req',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area="));
      pageWrite(String(as.area));
    pageWrite(F("&do=req_preset'})\">Request Preset</button>"
              "<button class='btn action' onclick=\"fetch('/api/area_req',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area="));
      pageWrite(String(as.area));
    pageWrite(F("&do=req_levels'})\">Request All Channel Levels</button>"
              "</div>"));
    // channels grid
    pageWrite(F("<div style='margin-top:10px' class='grid2'>"));

      for (int ci = 0; ci < em.channelsCount(); ci++) {
        const ChannelState& cs = em.channelAt(ci);
        if (cs.area != as.area || !cs.present) continue;

        pageWrite(F("<div class='card' style='padding:12px'>"));
          pageWrite(F("<div class='row space'>"));
            pageWrite(F("<div class='title'>"));
              pageWrite(String("Channel "));
              pageWrite(String((int)cs.channel0 + 1));
            pageWrite(F("</div>"));
            pageWrite(F("<span class='pill'>"));
              pageWrite(String("Lvl: "));
              pageWrite(String((int)cs.levelPct));
              pageWrite(F("%</span>"));
            pageWrite(F("<span class='pill'>"));
              pageWrite(String(cs.isOn ? "ON" : "OFF"));
            pageWrite(F("</span>"));
          pageWrite(F("</div>"));

          // type selector
          pageWrite(F("<form class='row' method='POST' action='/api/type' style='gap:8px;margin-top:8px'>"
                      "<input type='hidden' name='area' value='"));
            pageWrite(String(cs.area));
          pageWrite(F("'><input type='hidden' name='ch' value='"));
            pageWrite(String(cs.channel0));
          pageWrite(F("'>"
                      "<label>Type</label>"
                      "<select class='in' name='type'>"));
            auto typeOpt = [&](uint8_t v,const char* lbl){
              String sel = (cs.type==v) ? " selected" : "";
              pageWrite(String("<option value='")+v+"'" + sel + ">" + lbl + "</option>");
            };
            typeOpt(LIGHT_DIMMABLE,"Light (Dimmable)");
            typeOpt(LIGHT_ONOFF,   "Light (On/Off)");
            typeOpt(SWITCH_ONOFF,  "Switch (On/Off)");
          pageWrite(F("</select><button class='btn' type='submit'>Save</button></form>"));

          // controls
          pageWrite(F("<div class='row' style='gap:8px;margin-top:8px'>"));
            auto btn = [&](const char* cap, const String& body){
              pageWrite(F("<button class='btn action' onclick=\"fetch('/api/cmd',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'area="));
              pageWrite(String(cs.area));
              pageWrite(F("&ch="));
              pageWrite(String(cs.channel0));
              pageWrite(F("&cmd="));
              pageWrite(body);
              pageWrite(F("'})\">"));
              pageWrite(cap);
              pageWrite(F("</button>"));
            };
            btn("ON","ON");
            btn("OFF","OFF");
            btn("25%","SET=25");
            btn("50%","SET=50");
            btn("75%","SET=75");
            btn("100%","SET=100");
            btn("Request","REQ");
          pageWrite(F("</div>"));

        pageWrite(F("</div>")); // channel card
      }

    pageWrite(F("</div>")); // grid
  pageWrite(F("</div>"));   // area card
  }

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
    // --- Live UI updater for Preset/Temp/Setpoint pills
    "function updAreas(list){"
      "if(!Array.isArray(list)) return;"
      "for(const a of list){"
        "const id=String(a.area);"
        // preset (+1, unknown=255)
        "const p=document.getElementById('preset_A'+id);"
        "if(p && typeof a.preset0!=='undefined'){"
          "p.textContent='Preset: '+(a.preset0===255?'unknown':(a.preset0+1));"
        "}"
        // temperature
        "const t=document.getElementById('temp_A'+id);"
        "if(t){"
          "if(a.hasTemp && typeof a.tempC==='number'){"
            "t.style.display='';"
            "t.textContent='Temp: '+a.tempC.toFixed(1)+' °C';"
          "}else{"
            "t.style.display='none';"
          "}"
        "}"
        // setpoint
        "const s=document.getElementById('setpt_A'+id);"
        "if(s){"
          "if(a.hasSetpt && typeof a.setptC==='number'){"
            "s.style.display='';"
            "s.textContent='Setpoint: '+a.setptC.toFixed(1)+' °C';"
          "}else{"
            "s.style.display='none';"
          "}"
        "}"
      "}"
    "}"
    "function pollAreas(){"
      "fetch('/areas_status',{cache:'no-store'})"
        ".then(r=>r.json())"
        ".then(j=>updAreas(j.areas))"
        ".catch(()=>{});"
    "}"
    "window.addEventListener('DOMContentLoaded',()=>{"
      "setInterval(pollAreas,1500);"
      "pollAreas();"
    "});"
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


static String fwVersionString() {
#if defined(ESP8266)
  return String(ESP.getFullVersion());
#else
  return String(ESP.getSdkVersion());
#endif
}

static void handleFwGet() {
  pageBegin("Firmware Update");

  pageWrite(F("<div class='card'>"
              "<div class='title'>Manual Firmware Update</div>"));
  pageWrite(F("<p>Current: "));
  pageWrite(fwVersionString());
  pageWrite(F("</p>"
              "<form method='POST' action='/fw' enctype='multipart/form-data' class='form-sec'>"
              "<div class='field'>"
                "<label>Firmware .bin</label>"
                "<input type='file' name='fw' accept='.bin' required>"
              "</div>"
              "<div class='inline'>"
                "<button type='submit'>Upload & Update</button>"
                "<a class='btn' href='/'>Cancel</a>"
              "</div>"
              "</form>"
              "<p style='opacity:.7'>Do not power off during update. Device will reboot automatically.</p>"
              "</div>"));

  pageEnd();
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

// ---------- firmware upgrade from Web ----------
  void registerFwRoutes() {
    // GET: show form
    server.on("/fw", HTTP_GET, handleFwGet);

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

  // Persisted entity types (load once server is up)
  //tryLoadEntitiesFromFS();
  loadConfig();
  loadEntities();
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

    if      (cmd == "ON")  dynet.sendFadeToLevel_1s(area, ch, 100, 0x02);
    else if (cmd == "OFF") dynet.sendFadeToLevel_1s(area, ch,   0, 0x02);
    else if (cmd == "REQ") dynet.sendRequestChannelLevel(area, ch);
    else if (cmd.startsWith("SET=")) {
      int pct = constrain(cmd.substring(4).toInt(), 0, 100);
      dynet.sendFadeToLevel_1s(area, ch, pct, 0x02);
    } else { server.send(400, "application/json", "{\"ok\":false}"); return; }

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
  }
  String out; serializeJson(d, out);
  server.sendHeader("Cache-Control","no-store");
  server.send(200, "application/json", out);
});

  // One-shot poll: force a single dynetPollAreas() sweep
  server.on("/api/poll_all", HTTP_POST, [](){
    areasSweepActive = true;
    areasSweepArea   = 2;                 // always restart from Area 1
    areasSweepNextAt = millis() + 50;     // first tick shortly
    LOGF("[DyNet] sweep START requested from WebUI\n");
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
