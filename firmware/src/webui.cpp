#include "webui.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <esp_system.h>

static WebServer _web(80);
static char      _hostname[48];
static char      _ip[16];

static const char BUILD_DATE[] = __DATE__ " " __TIME__;

static const char *resetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "Power on";
        case ESP_RST_SW:        return "Software";
        case ESP_RST_PANIC:     return "Panic";
        case ESP_RST_INT_WDT:   return "Int watchdog";
        case ESP_RST_TASK_WDT:  return "Task watchdog";
        case ESP_RST_WDT:       return "Watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep sleep";
        case ESP_RST_BROWNOUT:  return "Brownout";
        default:                return "Unknown";
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

static const char PAGE_TMPL[] = R"HTML(<!DOCTYPE html><html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%%H</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#000;color:#ccc;font-family:-apple-system,sans-serif;padding:14px;max-width:480px;margin:0 auto}
h1{color:#fff;font-size:1.1em;margin-bottom:1px}
.ip{color:#555;font-size:.8em;font-family:monospace;margin-bottom:14px}
h2{color:#333;font-size:.65em;text-transform:uppercase;letter-spacing:.08em;margin:12px 0 4px;padding-bottom:3px;border-bottom:1px solid #111}
.grid{display:grid;grid-template-columns:1fr 1fr}
.item{padding:3px 0;border-bottom:1px solid #0d0d0d}
.item.wide{grid-column:1/-1}
.k{display:block;color:#555;font-size:.7em;text-transform:uppercase;letter-spacing:.04em}
.v{font-family:monospace;font-size:.82em}
.g{color:#3f3}.w{color:#fa0}.r{color:#f55}
#fw-lbl{color:#3f3;cursor:pointer;font-family:monospace;font-size:.82em}
#fw-btn{width:100%;margin-top:8px;padding:10px;background:#0d2b0d;border:1px solid #2a5a2a;border-radius:8px;color:#3f3;font-size:.9em;cursor:pointer}
#fw-btn:disabled{opacity:.35;cursor:default}
#fw-msg{font-size:.78em;color:#555;margin-top:6px;min-height:1em;text-align:center}
#ts{color:#222;font-size:.7em;text-align:right;margin-top:10px}
</style>
</head>
<body>
<h1>%%H</h1><div class="ip">%%I</div>

<h2>Network</h2>
<div class="grid">
<div class="item"><span class="k">SSID</span><span class="v" id="ssid">—</span></div>
<div class="item"><span class="k">Signal</span><span class="v" id="rssi">—</span></div>
<div class="item"><span class="k">Channel</span><span class="v" id="ch">—</span></div>
<div class="item"><span class="k">Gateway</span><span class="v" id="gw">—</span></div>
<div class="item wide"><span class="k">MAC</span><span class="v" id="mac">—</span></div>
</div>

<h2>System</h2>
<div class="grid">
<div class="item"><span class="k">Uptime</span><span class="v" id="up">—</span></div>
<div class="item"><span class="k">CPU</span><span class="v" id="cpu">—</span></div>
<div class="item"><span class="k">Temperature</span><span class="v" id="tmp">—</span></div>
<div class="item"><span class="k">Reset</span><span class="v" id="rst">—</span></div>
</div>

<h2>Memory</h2>
<div class="grid">
<div class="item"><span class="k">Free heap</span><span class="v" id="heap">—</span></div>
<div class="item"><span class="k">Min heap</span><span class="v" id="hmin">—</span></div>
<div class="item"><span class="k">Sketch used</span><span class="v" id="sk">—</span></div>
<div class="item"><span class="k">Sketch free</span><span class="v" id="skf">—</span></div>
</div>

<h2>Firmware</h2>
<div class="grid">
<div class="item"><span class="k">Chip</span><span class="v" id="chip">—</span></div>
<div class="item"><span class="k">SDK</span><span class="v" id="sdk">—</span></div>
<div class="item wide"><span class="k">Build</span><span class="v">%%B</span></div>
</div>

<h2>Update</h2>
<div class="grid">
<div class="item wide" style="border:none;display:flex;justify-content:space-between;align-items:center">
<span class="k" style="margin:0">Firmware file</span>
<span><label id="fw-lbl" for="fw-file">Choose .bin…</label><span id="fw-name" style="color:#444;font-size:.75em;font-family:monospace"></span></span>
</div>
</div>
<input type="file" id="fw-file" accept=".bin" style="display:none">
<button id="fw-btn" disabled>Upload Firmware</button>
<div id="fw-msg"></div>

<div id="ts">—</div>
<script>
function rc(r){return r>-60?'g':r>-75?'w':'r'}
function kb(n){return (n/1024).toFixed(1)+' KB'}
function upd(){
  fetch('/status').then(function(r){return r.json()}).then(function(d){
    document.getElementById('ssid').textContent=d.ssid;
    var re=document.getElementById('rssi');re.textContent=d.rssi+' dBm';re.className='v '+rc(d.rssi);
    document.getElementById('ch').textContent=d.channel;
    document.getElementById('gw').textContent=d.gateway;
    document.getElementById('mac').textContent=d.mac;
    document.getElementById('up').textContent=d.uptime;
    document.getElementById('cpu').textContent=d.cpu_mhz+' MHz';
    document.getElementById('tmp').textContent=d.temp_c+' °C';
    document.getElementById('rst').textContent=d.reset_reason;
    document.getElementById('heap').textContent=kb(d.free_heap);
    document.getElementById('hmin').textContent=kb(d.min_heap);
    document.getElementById('sk').textContent=kb(d.sketch_used);
    document.getElementById('skf').textContent=kb(d.sketch_free);
    document.getElementById('chip').textContent=d.chip+' rev'+d.chip_rev;
    document.getElementById('sdk').textContent=d.sdk;
    document.getElementById('ts').textContent='Updated '+new Date().toLocaleTimeString();
  }).catch(function(){})
}
upd();setInterval(upd,10000);

var fwFile=null;
document.getElementById('fw-file').addEventListener('change',function(e){
  fwFile=e.target.files[0];
  document.getElementById('fw-lbl').textContent=fwFile?'Change…':'Choose .bin…';
  document.getElementById('fw-name').textContent=fwFile?' '+fwFile.name+' ('+(fwFile.size/1024).toFixed(0)+' KB)':'';
  document.getElementById('fw-btn').disabled=!fwFile;
  document.getElementById('fw-msg').textContent='';
});
document.getElementById('fw-btn').addEventListener('click',function(){
  if(!fwFile)return;
  var fd=new FormData();fd.append('firmware',fwFile,fwFile.name);
  var xhr=new XMLHttpRequest();
  xhr.upload.onprogress=function(e){
    if(e.lengthComputable)document.getElementById('fw-msg').textContent='Uploading… '+Math.round(e.loaded/e.total*100)+'%';
  };
  xhr.onload=function(){
    var ok=xhr.status===200;
    document.getElementById('fw-msg').style.color=ok?'#3f3':'#f55';
    document.getElementById('fw-msg').textContent=ok?'Done — rebooting…':'Error: '+xhr.responseText;
    if(!ok)document.getElementById('fw-btn').disabled=false;
  };
  xhr.onerror=function(){
    document.getElementById('fw-msg').style.color='#f55';
    document.getElementById('fw-msg').textContent='Upload failed — check connection';
    document.getElementById('fw-btn').disabled=false;
  };
  xhr.open('POST','/update');xhr.send(fd);
  document.getElementById('fw-btn').disabled=true;
  document.getElementById('fw-msg').style.color='#555';
  document.getElementById('fw-msg').textContent='Starting…';
});
</script>
</body></html>)HTML";

static void handle_root() {
    String page = PAGE_TMPL;
    page.replace("%%H", _hostname);
    page.replace("%%I", _ip);
    page.replace("%%B", BUILD_DATE);
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

static void handle_update_done() {
    bool ok = !Update.hasError();
    _web.send(ok ? 200 : 500, "text/plain", ok ? "OK" : Update.errorString());
    if (ok) { delay(300); ESP.restart(); }
}

static void handle_update_upload() {
    HTTPUpload &up = _web.upload();
    if (up.status == UPLOAD_FILE_START) {
        Serial.printf("OTA upload: %s\n", up.filename.c_str());
        Update.begin(UPDATE_SIZE_UNKNOWN);
    } else if (up.status == UPLOAD_FILE_WRITE) {
        Update.write(up.buf, up.currentSize);
    } else if (up.status == UPLOAD_FILE_END) {
        Update.end(true);
        Serial.printf("OTA done: %u bytes\n", up.totalSize);
    }
}

void webui_start(const char *hostname, const char *ip) {
    strncpy(_hostname, hostname, sizeof(_hostname) - 1);
    strncpy(_ip,       ip,       sizeof(_ip)       - 1);
    _web.on("/",        HTTP_GET,  handle_root);
    _web.on("/status",  HTTP_GET,  handle_status);
    _web.on("/update",  HTTP_POST, handle_update_done, handle_update_upload);
    _web.onNotFound(handle_root);
    _web.begin();
}

void webui_handle() {
    _web.handleClient();
}
