#include "server.h"
#include <string.h>
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

static DNSServer  _dns;
static WebServer  _srv(80);

static char    _ap_ssid[10];     // "ESP-XXXX\0"
static uint8_t _pin[4];          // 4 digits verified on /adopt
static char    _default_name[32];
static bool    _adopted      = false;
static int     _pin_attempts = 0;

static const char SETUP_PAGE[] = R"HTML(
<!DOCTYPE html><html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Device Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#000;color:#ccc;font-family:-apple-system,sans-serif;
     padding:24px;max-width:380px;margin:0 auto;font-size:16px;line-height:1.5}
h2{color:#fff;margin-bottom:4px}
.ssid{font-family:monospace;font-size:1.1em;color:#3f3;margin-bottom:24px}
.step{margin-bottom:24px}
.step h3{color:#fff;font-size:1em;margin-bottom:8px}
.step p{color:#777;font-size:.88em;margin-bottom:10px}
hr{border:none;border-top:1px solid #1a1a1a;margin:0 0 24px}
label{display:block;color:#555;font-size:.78em;text-transform:uppercase;
      letter-spacing:.05em;margin:14px 0 4px}
input,select{width:100%;padding:12px;background:#111;border:1px solid #2a2a2a;
             border-radius:8px;color:#fff;font-size:1em;outline:none}
input:focus,select:focus{border-color:#444}
button{width:100%;padding:14px;margin-top:20px;background:#0d2b0d;
       border:1px solid #2a5a2a;border-radius:10px;color:#3f3;font-size:1em;cursor:pointer}
#msg{margin-top:12px;text-align:center;color:#888;min-height:1.4em;font-size:.9em}
.note{color:#333;font-size:.75em;margin-top:16px;text-align:center}
.warn{background:#1a0800;border:1px solid #5a2800;border-radius:8px;
      color:#f84;font-size:.88em;padding:10px 14px;margin-bottom:20px}
.warn.locked{background:#1a0000;border-color:#5a0000;color:#f55}
</style>
</head>
<body>
<h2>Device Setup</h2>
<div class="ssid">__SSID__</div>
__WARN__

<div class="step">
  <h3>1 &middot; Get your PIN</h3>
  <p>Open the <strong style="color:#aaa">ESP Commission</strong> app on your home screen, scan the blinking LED, and note the PIN shown.</p>
  <p style="color:#333;font-size:.78em;margin-top:8px">No app? Install from <strong>idltd.github.io/esp-commission</strong> &mdash; disconnect from this network first.</p>
</div>

<hr>

<div class="step">
  <h3>2 &middot; Enter your setup details</h3>
  <form id="f">
    <label>Home WiFi Network</label>
    <select id="ssid"><option value="">Loading&hellip;</option></select>
    <label>WiFi Password</label>
    <input type="password" id="pass" autocomplete="current-password">
    <label>PIN from app</label>
    <input type="text" id="pin" inputmode="numeric" pattern="[0-9]{4}" maxlength="4"
           placeholder="4 digits" autocomplete="off">
    <button type="submit" id="btn">Add Device</button>
  </form>
  <div id="msg">Scanning for networks&hellip;</div>
  <p class="note">Credentials sent directly to this device &mdash; not via the internet.</p>
</div>

<script>
var sel=document.getElementById('ssid');
var msg=document.getElementById('msg');
var btn=document.getElementById('btn');
fetch('/networks').then(function(r){return r.json();}).then(function(nets){
  if(!nets.length){msg.textContent='Still scanning…';setTimeout(function(){location.reload();},2000);return;}
  sel.innerHTML=nets.map(function(n){
    return '<option value="'+n.replace(/"/g,'&quot;')+'">'+n.replace(/&/g,'&amp;').replace(/</g,'&lt;')+'</option>';
  }).join('');
  msg.textContent='';
}).catch(function(){msg.textContent='Still scanning…';setTimeout(function(){location.reload();},2000);});
document.getElementById('f').addEventListener('submit',function(e){
  e.preventDefault();
  if(!sel.value){msg.textContent='Select a network first.';return;}
  var pin=document.getElementById('pin').value;
  if(!/^\d{4}$/.test(pin)){msg.textContent='Enter the 4-digit PIN from the app.';return;}
  btn.disabled=true;
  msg.textContent='Sending…';
  fetch('/adopt',{method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({wifi_ssid:sel.value,wifi_password:document.getElementById('pass').value,pin:pin})
  }).then(function(r){
    if(r.ok){
      msg.textContent='Done! Device is connecting…';
    }else{
      btn.disabled=false;
      r.text().then(function(t){msg.textContent='Error: '+t;});
    }
  }).catch(function(e){btn.disabled=false;msg.textContent='Error: '+e.message;});
});
</script>
</body></html>
)HTML";

static void handle_root() {
    String page = SETUP_PAGE;
    page.replace("__SSID__", _ap_ssid);
    String warn;
    if (_pin_attempts >= 3) {
        warn = "<div class=\"warn locked\">Device locked &mdash; too many failed attempts. Power-cycle to reset.</div>";
    } else if (_pin_attempts == 2) {
        warn = "<div class=\"warn\">2 failed PIN attempts recorded &mdash; 1 remaining. Check your surroundings.</div>";
    } else if (_pin_attempts == 1) {
        warn = "<div class=\"warn\">1 failed PIN attempt recorded. Check your surroundings.</div>";
    }
    page.replace("__WARN__", warn);
    _srv.send(200, "text/html; charset=utf-8", page);
}

static void handle_networks() {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
        _srv.send(200, "application/json", "[]");
        return;
    }
    if (n < 0) {
        WiFi.scanNetworks(/*async=*/true);
        _srv.send(200, "application/json", "[]");
        return;
    }
    String json = "[";
    for (int i = 0; i < n; i++) {
        if (i > 0) json += ",";
        json += "\"";
        String ssid = WiFi.SSID(i);
        for (unsigned int j = 0; j < ssid.length(); j++) {
            char c = ssid[j];
            if (c == '"' || c == '\\') json += '\\';
            if (c >= 0x20) json += c;
        }
        json += "\"";
    }
    json += "]";
    _srv.send(200, "application/json", json);
}

static void handle_adopt() {
    if (_pin_attempts >= 3) {
        _srv.send(403, "application/json", "{\"error\":\"locked\"}");
        return;
    }

    String body = _srv.arg("plain");
    if (body.isEmpty()) {
        _srv.send(400, "application/json", "{\"error\":\"empty_body\"}");
        return;
    }

    const char *p = body.c_str();
    auto extract = [&](const char *key) -> String {
        const char *s = strstr(p, key);
        if (!s) return "";
        s = strchr(s, ':'); if (!s) return "";
        s++;
        while (*s == ' ' || *s == '"') s++;
        const char *e = s;
        while (*e && *e != '"') e++;
        return String(s, e - s);
    };

    String ssid    = extract("\"wifi_ssid\"");
    String pass    = extract("\"wifi_password\"");
    String pin_str = extract("\"pin\"");

    if (ssid.isEmpty()) {
        _srv.send(400, "application/json", "{\"error\":\"missing_ssid\"}");
        return;
    }

    if (pin_str.length() != 4) {
        _srv.send(400, "application/json", "{\"error\":\"missing_pin\"}");
        return;
    }
    for (int i = 0; i < 4; i++) {
        if ((pin_str[i] - '0') != (int)_pin[i]) {
            _pin_attempts++;
            _srv.send(403, "application/json", "{\"error\":\"wrong_pin\"}");
            return;
        }
    }

    Preferences prefs;
    prefs.begin("onboarding", false);
    prefs.putString("wifi_ssid", ssid);
    prefs.putString("wifi_pass", pass);
    prefs.putString("devname",   _default_name);
    prefs.putUChar("adopted", 1);
    prefs.end();

    _srv.send(200, "application/json", "{\"ok\":true}");
    _pin_attempts = 3;  // prevent re-adoption
    _adopted      = true;
}

void server_start(const char suffix[4], const uint8_t pin[4]) {
    memcpy(_pin, pin, 4);
    snprintf(_ap_ssid, sizeof(_ap_ssid), "ESP-%c%c%c%c",
             suffix[0], suffix[1], suffix[2], suffix[3]);

    snprintf(_default_name, sizeof(_default_name), "esp32-%c%c%c%c",
             tolower(suffix[0]), tolower(suffix[1]),
             tolower(suffix[2]), tolower(suffix[3]));

    WiFi.mode(WIFI_AP_STA);
    WiFi.setHostname(_default_name);
    WiFi.softAP(_ap_ssid, nullptr, 1, false, 1);
    Serial.printf("SoftAP SSID: %s  (open)  IP: %s\n",
                  _ap_ssid, WiFi.softAPIP().toString().c_str());
    Serial.printf("Device name: %s\n", _default_name);

    WiFi.scanNetworks(/*async=*/true);

    _dns.start(53, "*", WiFi.softAPIP());

    _srv.on("/",         HTTP_GET,  handle_root);
    _srv.on("/networks", HTTP_GET,  handle_networks);
    _srv.on("/adopt",    HTTP_POST, handle_adopt);
    _srv.onNotFound(handle_root);
    _srv.begin();
}

void server_handle() {
    _dns.processNextRequest();
    _srv.handleClient();
}

void server_stop() {
    _dns.stop();
    _srv.stop();
}

bool server_adopted() { return _adopted; }
