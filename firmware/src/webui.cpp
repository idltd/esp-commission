#include "webui.h"
#include "led.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_system.h>

static WebServer _web(80);
static char      _hostname[48];
static char      _ip[16];
static bool      _led_state = false;

static const char BUILD_DATE[] = __DATE__ " " __TIME__;

static const char *resetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "Power on";
        case ESP_RST_SW:        return "Software reset";
        case ESP_RST_PANIC:     return "Panic";
        case ESP_RST_INT_WDT:   return "Watchdog";
        case ESP_RST_BROWNOUT:  return "Brownout";
        default:                return "Other";
    }
}

static String fmtUptime() {
    unsigned long s = millis() / 1000;
    unsigned long m = s / 60; s %= 60;
    unsigned long h = m / 60; m %= 60;
    unsigned long d = h / 24; h %= 24;
    String r;
    if (d)           r += String(d) + "d ";
    if (h || d)      r += String(h) + "h ";
    if (m || h || d) r += String(m) + "m ";
    r += String(s) + "s";
    return r;
}

// ---- page ---------------------------------------------------------------

static const char PAGE[] = R"HTML(<!DOCTYPE html><html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Device — __HOST__</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:#0a0a0a;color:#ddd;font-family:-apple-system,sans-serif;
          font-size:15px;line-height:1.5;padding:0 0 32px}
header{background:#000;border-bottom:1px solid #1a1a1a;padding:16px;
       display:flex;align-items:baseline;gap:12px;flex-wrap:wrap}
header h1{color:#fff;font-size:1.15em;font-weight:600}
header .sub{color:#555;font-size:.82em;font-family:monospace}
.card{background:#111;border:1px solid #1e1e1e;border-radius:10px;
      margin:14px 14px 0;padding:16px}
.card h2{color:#fff;font-size:.95em;font-weight:600;margin-bottom:12px;
         display:flex;align-items:center;gap:8px}
.tag{background:#1a2a1a;border:1px solid #2a4a2a;color:#5f5;
     font-size:.7em;font-weight:400;padding:1px 7px;border-radius:10px;
     letter-spacing:.03em}
.hint{color:#555;font-size:.82em;margin-bottom:12px;line-height:1.6}
label{display:block;color:#666;font-size:.75em;text-transform:uppercase;
      letter-spacing:.05em;margin:12px 0 4px}
label:first-child{margin-top:0}
input[type=text],input[type=password],input[type=number]{
  width:100%;padding:10px 12px;background:#1a1a1a;border:1px solid #2a2a2a;
  border-radius:7px;color:#fff;font-size:.95em;outline:none;font-family:inherit}
input:focus{border-color:#3a3a3a}
.tog-row{display:flex;gap:6px;margin:2px 0}
.tog{padding:8px 20px;border-radius:6px;border:1px solid #2a2a2a;background:#1a1a1a;
     color:#666;font-size:.88em;cursor:pointer;font-family:inherit}
.tog.on{border-color:#3a3a3a;background:#222;color:#fff}
.inline{display:flex;align-items:center;gap:10px}
.inline input{flex:1}
.unit{color:#555;font-size:.88em;white-space:nowrap}
.btn{display:block;width:100%;padding:11px;margin-top:14px;
     background:#0d2b0d;border:1px solid #2a5a2a;border-radius:8px;
     color:#3f3;font-size:.95em;cursor:pointer;font-family:inherit;text-align:center}
.btn:disabled{opacity:.3;cursor:default}
.btn.danger{background:#2b0d0d;border-color:#5a2a2a;color:#f55}
.msg{min-height:18px;margin-top:8px;font-size:.82em;color:#777;text-align:center}
.msg.ok{color:#3f3}.msg.err{color:#f55}
.kv{display:grid;grid-template-columns:auto 1fr;gap:3px 14px;font-size:.85em;
    margin-bottom:12px}
.kv .k{color:#555}.kv .v{color:#ccc;font-family:monospace}
.sig{display:inline-block;width:8px;height:8px;border-radius:50%;
     background:#3f3;margin-right:4px;vertical-align:middle}
.sig.w{background:#fa0}.sig.b{background:#f55}
details summary{cursor:pointer;color:#555;font-size:.82em;
                text-transform:uppercase;letter-spacing:.06em;
                list-style:none;padding:2px 0;user-select:none}
details summary::before{content:'▶  ';font-size:.7em;opacity:.5}
details[open] summary::before{content:'▼  '}
details .inner{margin-top:12px}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:6px 14px;margin-top:6px}
.stat .sk{color:#555;font-size:.72em;text-transform:uppercase;letter-spacing:.04em}
.stat .sv{font-family:monospace;font-size:.88em;color:#ccc}
.prog-wrap{background:#1a1a1a;border-radius:4px;height:6px;margin-top:10px;overflow:hidden;display:none}
.prog-bar{height:100%;background:#3f3;border-radius:4px;width:0;transition:width .2s}
hr{border:none;border-top:1px solid #1a1a1a;margin:14px 0}
</style>
</head>
<body>

<header>
  <h1 id="hname">__HOST__</h1>
  <span class="sub" id="hinfo">__IP__</span>
</header>

<!-- ================================================================
     APPLICATION SECTION
     Replace this card with your own device controls.
     Endpoints: GET /app  POST /app  POST /led
     NVS namespace: "appconfig"
     ================================================================ -->
<div class="card">
  <h2>Application <span class="tag">demo template</span></h2>
  <p class="hint">
    Replace this section with your device's controls.<br>
    This demo shows: a text setting, live LED toggle, and a numeric config value.
  </p>

  <label>Device label</label>
  <input type="text" id="app-label" placeholder="e.g. Living Room Sensor" maxlength="40">

  <label>Onboard LED</label>
  <div class="tog-row">
    <button class="tog" id="led-on"  onclick="setLed(true)">On</button>
    <button class="tog on" id="led-off" onclick="setLed(false)">Off</button>
  </div>

  <label>Report interval</label>
  <div class="inline">
    <input type="number" id="app-interval" min="1" max="3600" value="30" style="max-width:100px">
    <span class="unit">seconds</span>
  </div>

  <button class="btn" id="btn-app" onclick="saveApp()">Save application settings</button>
  <div class="msg" id="app-msg"></div>
</div>
<!-- END APPLICATION SECTION -->

<!-- Network -->
<div class="card">
  <h2>Network</h2>
  <div class="kv" id="net-kv">
    <span class="k">WiFi</span><span class="v" id="n-ssid">—</span>
    <span class="k">Signal</span><span class="v" id="n-rssi">—</span>
    <span class="k">IP</span><span class="v">__IP__</span>
    <span class="k">Gateway</span><span class="v" id="n-gw">—</span>
    <span class="k">mDNS</span><span class="v">__HOST__.local</span>
  </div>

  <details>
    <summary>Change WiFi network</summary>
    <div class="inner">
      <label>New SSID</label>
      <input type="text" id="new-ssid" autocomplete="off">
      <label>Password</label>
      <input type="password" id="new-pass" autocomplete="new-password">
      <button class="btn" onclick="changeNet()">Save &amp; reconnect</button>
      <div class="msg" id="net-msg"></div>
    </div>
  </details>

  <hr>

  <details>
    <summary>Rename device</summary>
    <div class="inner">
      <label>Hostname (letters, digits, hyphens)</label>
      <input type="text" id="new-name" value="__HOST__" maxlength="32" pattern="[a-z0-9\-]+">
      <button class="btn" onclick="rename()">Save &amp; reboot</button>
      <div class="msg" id="name-msg"></div>
    </div>
  </details>
</div>

<!-- Firmware update -->
<div class="card">
  <h2>Firmware update</h2>
  <div class="kv">
    <span class="k">Build</span><span class="v">__BUILD__</span>
    <span class="k">Hostname</span><span class="v">__HOST__</span>
  </div>
  <label for="fw-file">Firmware file (.bin)</label>
  <div style="display:flex;align-items:center;gap:10px;margin-bottom:4px">
    <label for="fw-file" style="all:unset;padding:8px 14px;background:#1a1a1a;
      border:1px solid #2a2a2a;border-radius:6px;color:#ccc;font-size:.85em;
      cursor:pointer;white-space:nowrap;text-transform:none;letter-spacing:0;margin:0">
      Choose .bin…
    </label>
    <span id="fw-name" style="color:#555;font-size:.8em;font-family:monospace"></span>
  </div>
  <input type="file" id="fw-file" accept=".bin" style="display:none" onchange="fwPicked(this)">
  <button class="btn" id="fw-btn" disabled onclick="uploadFw()">Upload firmware</button>
  <div class="prog-wrap" id="fw-prog"><div class="prog-bar" id="fw-bar"></div></div>
  <div class="msg" id="fw-msg"></div>
</div>

<!-- Status (collapsed by default) -->
<div class="card">
  <details>
    <summary>Device status</summary>
    <div class="inner grid2" id="stat-grid">
      <div class="stat"><div class="sk">Uptime</div><div class="sv" id="s-up">—</div></div>
      <div class="stat"><div class="sk">CPU</div><div class="sv" id="s-cpu">—</div></div>
      <div class="stat"><div class="sk">Temperature</div><div class="sv" id="s-tmp">—</div></div>
      <div class="stat"><div class="sk">Reset reason</div><div class="sv" id="s-rst">—</div></div>
      <div class="stat"><div class="sk">Free heap</div><div class="sv" id="s-heap">—</div></div>
      <div class="stat"><div class="sk">Min heap</div><div class="sv" id="s-hmin">—</div></div>
      <div class="stat"><div class="sk">Sketch used</div><div class="sv" id="s-sk">—</div></div>
      <div class="stat"><div class="sk">Sketch free</div><div class="sv" id="s-skf">—</div></div>
      <div class="stat"><div class="sk">Chip</div><div class="sv" id="s-chip">—</div></div>
      <div class="stat"><div class="sk">SDK</div><div class="sv" id="s-sdk">—</div></div>
    </div>
  </details>
</div>

<script>
// ---- status polling ----
function kb(n){return(n/1024).toFixed(1)+' KB';}
function rssiClass(r){return r>-60?'g':r>-75?'w':'b';}
function pollStatus(){
  fetch('/status').then(function(r){return r.json();}).then(function(d){
    var r=d.rssi;
    document.getElementById('n-ssid').innerHTML='<span class="sig '+rssiClass(r)+'"></span>'+d.ssid;
    document.getElementById('n-rssi').textContent=r+' dBm';
    document.getElementById('n-gw').textContent=d.gateway;
    document.getElementById('s-up').textContent=d.uptime;
    document.getElementById('s-cpu').textContent=d.cpu_mhz+' MHz';
    document.getElementById('s-tmp').textContent=d.temp_c+' °C';
    document.getElementById('s-rst').textContent=d.reset_reason;
    document.getElementById('s-heap').textContent=kb(d.free_heap);
    document.getElementById('s-hmin').textContent=kb(d.min_heap);
    document.getElementById('s-sk').textContent=kb(d.sketch_used);
    document.getElementById('s-skf').textContent=kb(d.sketch_free);
    document.getElementById('s-chip').textContent=d.chip+' r'+d.chip_rev;
    document.getElementById('s-sdk').textContent=d.sdk;
  }).catch(function(){});
}
pollStatus();setInterval(pollStatus,15000);

// ---- app section ----
fetch('/app').then(function(r){return r.json();}).then(function(d){
  document.getElementById('app-label').value=d.label||'';
  document.getElementById('app-interval').value=d.interval||30;
  setLedUI(d.led||false);
}).catch(function(){});

function setMsg(id,txt,ok){
  var e=document.getElementById(id);e.textContent=txt;
  e.className='msg '+(ok===true?'ok':ok===false?'err':'');
}

function saveApp(){
  var body={
    label:document.getElementById('app-label').value,
    interval:parseInt(document.getElementById('app-interval').value)||30,
    led:_ledState
  };
  fetch('/app',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
    .then(function(r){setMsg('app-msg',r.ok?'Saved':'Error '+r.status,r.ok);})
    .catch(function(e){setMsg('app-msg','Error: '+e.message,false);});
}

var _ledState=false;
function setLedUI(on){
  _ledState=on;
  document.getElementById('led-on').classList.toggle('on',on);
  document.getElementById('led-off').classList.toggle('on',!on);
}
function setLed(on){
  setLedUI(on);
  fetch('/led',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({state:on})}).catch(function(){});
}

// ---- network change ----
function changeNet(){
  var ssid=document.getElementById('new-ssid').value.trim();
  var pass=document.getElementById('new-pass').value;
  if(!ssid){setMsg('net-msg','Enter an SSID',false);return;}
  setMsg('net-msg','Saving — device will reconnect…');
  fetch('/network',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({ssid:ssid,pass:pass})})
    .then(function(r){setMsg('net-msg',r.ok?'Done — reconnecting now':'Error '+r.status,r.ok);})
    .catch(function(e){setMsg('net-msg','Error: '+e.message,false);});
}

// ---- rename ----
function rename(){
  var name=document.getElementById('new-name').value.trim().toLowerCase().replace(/[^a-z0-9\-]/g,'-');
  if(!name){setMsg('name-msg','Invalid name',false);return;}
  setMsg('name-msg','Saving — rebooting…');
  fetch('/rename',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({name:name})})
    .then(function(r){setMsg('name-msg',r.ok?'Rebooting…':'Error '+r.status,r.ok);})
    .catch(function(e){setMsg('name-msg','Error: '+e.message,false);});
}

// ---- OTA ----
var _fwFile=null;
function fwPicked(inp){
  _fwFile=inp.files[0];
  document.getElementById('fw-name').textContent=_fwFile?_fwFile.name+' ('+((_fwFile.size/1024).toFixed(0))+' KB)':'';
  document.getElementById('fw-btn').disabled=!_fwFile;
  setMsg('fw-msg','');
}
function uploadFw(){
  if(!_fwFile)return;
  var fd=new FormData();fd.append('firmware',_fwFile,_fwFile.name);
  var xhr=new XMLHttpRequest();
  var prog=document.getElementById('fw-prog'),bar=document.getElementById('fw-bar');
  prog.style.display='block';
  xhr.upload.onprogress=function(e){
    if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);bar.style.width=p+'%';setMsg('fw-msg','Uploading… '+p+'%');}
  };
  xhr.onload=function(){
    var ok=xhr.status===200;
    prog.style.display='none';
    setMsg('fw-msg',ok?'Done — rebooting…':'Error: '+xhr.responseText,ok);
    document.getElementById('fw-btn').disabled=!ok;
  };
  xhr.onerror=function(){prog.style.display='none';setMsg('fw-msg','Upload failed',false);document.getElementById('fw-btn').disabled=false;};
  xhr.open('POST','/update');xhr.send(fd);
  document.getElementById('fw-btn').disabled=true;
  setMsg('fw-msg','Starting…');
}
</script>
</body></html>)HTML";

// ---- handlers -----------------------------------------------------------

static void handle_root() {
    String page = PAGE;
    page.replace("__HOST__",  _hostname);
    page.replace("__IP__",    _ip);
    page.replace("__BUILD__", BUILD_DATE);
    _web.send(200, "text/html; charset=utf-8", page);
}

static void handle_status() {
    String j = "{";
    j += "\"ssid\":\""         + WiFi.SSID()                     + "\",";
    j += "\"rssi\":"           + String(WiFi.RSSI())              + ",";
    j += "\"channel\":"        + String(WiFi.channel())           + ",";
    j += "\"gateway\":\""      + WiFi.gatewayIP().toString()      + "\",";
    j += "\"mac\":\""          + WiFi.macAddress()                + "\",";
    j += "\"uptime\":\""       + fmtUptime()                      + "\",";
    j += "\"cpu_mhz\":"        + String(getCpuFrequencyMhz())     + ",";
    j += "\"temp_c\":"         + String(temperatureRead(), 1)     + ",";
    j += "\"reset_reason\":\""; j += resetReason(); j += "\",";
    j += "\"free_heap\":"      + String(ESP.getFreeHeap())        + ",";
    j += "\"min_heap\":"       + String(ESP.getMinFreeHeap())     + ",";
    j += "\"sketch_used\":"    + String(ESP.getSketchSize())      + ",";
    j += "\"sketch_free\":"    + String(ESP.getFreeSketchSpace()) + ",";
    j += "\"chip\":\""         + String(ESP.getChipModel())       + "\",";
    j += "\"chip_rev\":"       + String(ESP.getChipRevision())    + ",";
    j += "\"sdk\":\""          + String(ESP.getSdkVersion())      + "\"";
    j += "}";
    _web.send(200, "application/json", j);
}

// ---- application section ------------------------------------------------

static void handle_app_get() {
    Preferences p;
    p.begin("appconfig", true);
    String label    = p.getString("label",    "");
    int    interval = p.getInt   ("interval", 30);
    bool   led      = p.getBool  ("led",      false);
    p.end();
    char buf[160];
    snprintf(buf, sizeof(buf),
        "{\"label\":\"%s\",\"interval\":%d,\"led\":%s}",
        label.c_str(), interval, led ? "true" : "false");
    _web.send(200, "application/json", buf);
}

static void handle_app_post() {
    String body = _web.arg("plain");
    if (body.isEmpty()) { _web.send(400); return; }
    const char *p = body.c_str();

    auto getStr = [&](const char *key) -> String {
        const char *s = strstr(p, key); if (!s) return "";
        s = strchr(s, ':'); if (!s) return "";
        s++; while (*s == ' ' || *s == '"') s++;
        const char *e = s; while (*e && *e != '"') e++;
        return String(s, e - s);
    };
    auto getInt = [&](const char *key, int def) -> int {
        const char *s = strstr(p, key); if (!s) return def;
        s = strchr(s, ':'); if (!s) return def;
        return atoi(s + 1);
    };
    auto getBool = [&](const char *key) -> bool {
        const char *s = strstr(p, key); if (!s) return false;
        s = strchr(s, ':'); if (!s) return false;
        while (*s == ':' || *s == ' ') s++;
        return strncmp(s, "true", 4) == 0;
    };

    String label    = getStr ("\"label\"");
    int    interval = getInt ("\"interval\"", 30);
    bool   led      = getBool("\"led\"");

    Preferences pr;
    pr.begin("appconfig", false);
    pr.putString("label",    label);
    pr.putInt   ("interval", interval);
    pr.putBool  ("led",      led);
    pr.end();

    // Apply LED immediately
    _led_state = led;
    led_set(led);

    Serial.printf("app: label='%s' interval=%d led=%s\n",
        label.c_str(), interval, led ? "on" : "off");

    _web.send(200, "application/json", "{\"ok\":true}");
}

static void handle_led() {
    String body = _web.arg("plain");
    const char *p = body.c_str();
    const char *s = strstr(p, "\"state\"");
    if (s) {
        s = strchr(s, ':'); if (s) { while (*s == ':' || *s == ' ') s++; }
        _led_state = s && strncmp(s, "true", 4) == 0;
        led_set(_led_state);
        Serial.printf("LED: %s\n", _led_state ? "on" : "off");
    }
    _web.send(200, "application/json", "{\"ok\":true}");
}

// ---- network reconfiguration --------------------------------------------

static void handle_network() {
    String body = _web.arg("plain");
    if (body.isEmpty()) { _web.send(400); return; }
    const char *p = body.c_str();

    auto getStr = [&](const char *key) -> String {
        const char *s = strstr(p, key); if (!s) return "";
        s = strchr(s, ':'); if (!s) return "";
        s++; while (*s == ' ' || *s == '"') s++;
        const char *e = s; while (*e && *e != '"') e++;
        return String(s, e - s);
    };

    String ssid = getStr("\"ssid\"");
    String pass = getStr("\"pass\"");
    if (ssid.isEmpty()) { _web.send(400, "text/plain", "missing ssid"); return; }

    Preferences pr;
    pr.begin("onboarding", false);
    pr.putString("wifi_ssid", ssid);
    pr.putString("wifi_pass", pass);
    pr.end();

    Serial.printf("network: changing to '%s' — rebooting\n", ssid.c_str());
    _web.send(200, "application/json", "{\"ok\":true}");
    delay(300);
    ESP.restart();
}

// ---- rename -------------------------------------------------------------

static void handle_rename() {
    String body = _web.arg("plain");
    const char *p = body.c_str();
    const char *s = strstr(p, "\"name\"");
    if (!s) { _web.send(400, "text/plain", "missing name"); return; }
    s = strchr(s, ':'); if (!s) { _web.send(400); return; }
    s++; while (*s == ' ' || *s == '"') s++;
    const char *e = s; while (*e && *e != '"') e++;
    String name(s, e - s);
    if (name.isEmpty()) { _web.send(400, "text/plain", "empty name"); return; }

    Preferences pr;
    pr.begin("onboarding", false);
    pr.putString("devname", name);
    pr.end();

    Serial.printf("rename: new name '%s' — rebooting\n", name.c_str());
    _web.send(200, "application/json", "{\"ok\":true}");
    delay(300);
    ESP.restart();
}

// ---- OTA ----------------------------------------------------------------

static void handle_update_done() {
    bool ok = !Update.hasError();
    _web.send(ok ? 200 : 500, "text/plain", ok ? "OK" : Update.errorString());
    if (ok) { delay(300); ESP.restart(); }
}

static void handle_update_upload() {
    HTTPUpload &up = _web.upload();
    if      (up.status == UPLOAD_FILE_START) { Serial.printf("OTA: %s\n", up.filename.c_str()); Update.begin(UPDATE_SIZE_UNKNOWN); }
    else if (up.status == UPLOAD_FILE_WRITE) { Update.write(up.buf, up.currentSize); }
    else if (up.status == UPLOAD_FILE_END)   { Update.end(true); Serial.printf("OTA done: %u bytes\n", up.totalSize); }
}

// ---- public -------------------------------------------------------------

void webui_start(const char *hostname, const char *ip) {
    strncpy(_hostname, hostname, sizeof(_hostname) - 1);
    strncpy(_ip,       ip,       sizeof(_ip)       - 1);

    // Restore LED state from saved app config
    Preferences p;
    p.begin("appconfig", true);
    _led_state = p.getBool("led", false);
    p.end();
    led_set(_led_state);

    _web.on("/",        HTTP_GET,  handle_root);
    _web.on("/status",  HTTP_GET,  handle_status);
    _web.on("/app",     HTTP_GET,  handle_app_get);
    _web.on("/app",     HTTP_POST, handle_app_post);
    _web.on("/led",     HTTP_POST, handle_led);
    _web.on("/network", HTTP_POST, handle_network);
    _web.on("/rename",  HTTP_POST, handle_rename);
    _web.on("/update",  HTTP_POST, handle_update_done, handle_update_upload);
    _web.onNotFound(handle_root);
    _web.begin();
}

void webui_handle() {
    _web.handleClient();
}
