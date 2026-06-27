#include "server.h"
#include "blink.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <string.h>

static DNSServer  _dns;
static WebServer  _srv(80);
static char       _ssid[20];

static const char PAGE[] = R"HTML(
<!DOCTYPE html><html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Blink Tester</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#000;color:#ccc;font-family:-apple-system,sans-serif;
     padding:20px;max-width:420px;margin:0 auto;font-size:15px;line-height:1.5}
h2{color:#fff;margin-bottom:4px}
.ssid{font-family:monospace;font-size:1em;color:#3f3;margin-bottom:20px}
.section{margin-bottom:20px}
.section h3{font-size:.75em;text-transform:uppercase;letter-spacing:.07em;
            color:#444;margin-bottom:10px;border-bottom:1px solid #1a1a1a;padding-bottom:4px}
.param{display:grid;grid-template-columns:110px 1fr 52px;align-items:center;
       gap:8px;margin-bottom:8px}
.param label{font-size:.82em;color:#888}
input[type=range]{width:100%;accent-color:#3f3}
.val{font-family:monospace;font-size:.85em;color:#3f3;text-align:right}
.radios{display:flex;flex-direction:column;gap:6px}
.radios label{display:flex;align-items:center;gap:8px;font-size:.9em;color:#aaa;cursor:pointer}
.cycle{font-size:.82em;color:#555;margin:14px 0 6px;font-family:monospace}
.cycle span{color:#3f3}
#status{font-size:.8em;color:#555;min-height:1.2em;transition:color .3s}
#status.ok{color:#3f3}
#status.err{color:#f55}
</style>
</head>
<body>
<h2>Blink Tester</h2>
<div class="ssid">__SSID__</div>

<div class="section">
<h3>Timing</h3>
<div class="param"><label>Pulse SHORT</label>
  <input type="range" id="pulse_short" min="50" max="600" value="150" oninput="upd(this)">
  <span class="val" id="v_pulse_short">150</span></div>
<div class="param"><label>Pulse LONG</label>
  <input type="range" id="pulse_long"  min="50" max="600" value="350" oninput="upd(this)">
  <span class="val" id="v_pulse_long">350</span></div>
<div class="param"><label>Bit gap</label>
  <input type="range" id="bit_gap"     min="50" max="500" value="200" oninput="upd(this)">
  <span class="val" id="v_bit_gap">200</span></div>
<div class="param"><label>Preamble</label>
  <input type="range" id="pre_ms"      min="100" max="2000" value="1000" oninput="upd(this)">
  <span class="val" id="v_pre_ms">1000</span></div>
<div class="param"><label>Pre gap</label>
  <input type="range" id="pre_gap"     min="50" max="500" value="300" oninput="upd(this)">
  <span class="val" id="v_pre_gap">300</span></div>
<div class="param"><label>Post gap</label>
  <input type="range" id="post_ms"     min="100" max="1000" value="500" oninput="upd(this)">
  <span class="val" id="v_post_ms">500</span></div>
</div>

<div class="section">
<h3>Pattern</h3>
<div class="radios">
  <label><input type="radio" name="pat" value="0" checked onchange="upd(this)"> Alternating 0101&hellip;</label>
  <label><input type="radio" name="pat" value="1"          onchange="upd(this)"> All zeros (short pulses)</label>
  <label><input type="radio" name="pat" value="2"          onchange="upd(this)"> All ones (long pulses)</label>
</div>
</div>

<div class="cycle">Frame: <span id="cycle">—</span> &nbsp;|&nbsp; Threshold: <span id="thresh">—</span></div>
<div id="status">Loading…</div>

<script>
var timer=null;
var BITS=32;

function get(id){return +document.getElementById(id).value;}
function pat(){return +document.querySelector('input[name=pat]:checked').value;}

function upd(){
  ['pulse_short','pulse_long','bit_gap','pre_ms','pre_gap','post_ms'].forEach(function(k){
    var v=get(k); document.getElementById('v_'+k).textContent=v;
  });
  var ps=get('pulse_short'),pl=get('pulse_long'),bg=get('bit_gap');
  var pre=get('pre_ms'),preg=get('pre_gap'),post=get('post_ms'),p=pat();
  var b0=p===2?0:p===1?BITS:BITS/2, b1=p===1?0:p===2?BITS:BITS/2;
  var ms=pre+preg+b0*(ps+bg)+b1*(pl+bg)+post;
  document.getElementById('cycle').textContent=(ms/1000).toFixed(1)+' s';
  document.getElementById('thresh').textContent=Math.round((ps+pl)/2)+' ms';
  schedule();
}

function schedule(){
  clearTimeout(timer);
  timer=setTimeout(apply,400);
}

function apply(){
  var cfg={
    pulse_short:get('pulse_short'),pulse_long:get('pulse_long'),
    bit_gap:get('bit_gap'),pre_ms:get('pre_ms'),
    pre_gap:get('pre_gap'),post_ms:get('post_ms'),pattern:pat()
  };
  var st=document.getElementById('status');
  st.className=''; st.textContent='Applying…';
  fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify(cfg)
  }).then(function(r){
    st.className=r.ok?'ok':'err';
    st.textContent=r.ok?'Applied ✓':'Error '+r.status;
  }).catch(function(e){st.className='err';st.textContent='Error: '+e.message;});
}

fetch('/config').then(function(r){return r.json();}).then(function(c){
  ['pulse_short','pulse_long','bit_gap','pre_ms','pre_gap','post_ms'].forEach(function(k){
    document.getElementById(k).value=c[k];
  });
  document.querySelectorAll('input[name=pat]').forEach(function(r){
    if(+r.value===c.pattern) r.checked=true;
  });
  upd();
  document.getElementById('status').textContent='';
}).catch(function(){document.getElementById('status').textContent='';upd();});
</script>
</body></html>
)HTML";

static void handle_root() {
    String page = PAGE;
    page.replace("__SSID__", _ssid);
    _srv.send(200, "text/html; charset=utf-8", page);
}

static void handle_config_get() {
    char buf[160];
    snprintf(buf, sizeof(buf),
        "{\"pulse_short\":%d,\"pulse_long\":%d,\"bit_gap\":%d,"
        "\"pre_ms\":%d,\"pre_gap\":%d,\"post_ms\":%d,\"pattern\":%d}",
        g_cfg.pulse_short, g_cfg.pulse_long, g_cfg.bit_gap,
        g_cfg.pre_ms, g_cfg.pre_gap, g_cfg.post_ms, g_cfg.pattern);
    _srv.send(200, "application/json", buf);
}

static void handle_config_post() {
    String body = _srv.arg("plain");
    if (body.isEmpty()) { _srv.send(400, "application/json", "{\"error\":\"empty\"}"); return; }

    const char *p = body.c_str();
    auto getInt = [&](const char *key, int fallback) -> int {
        const char *s = strstr(p, key);
        if (!s) return fallback;
        s = strchr(s, ':');
        if (!s) return fallback;
        return atoi(s + 1);
    };

    BlinkConfig cfg;
    cfg.pulse_short = getInt("\"pulse_short\"", g_cfg.pulse_short);
    cfg.pulse_long  = getInt("\"pulse_long\"",  g_cfg.pulse_long);
    cfg.bit_gap     = getInt("\"bit_gap\"",     g_cfg.bit_gap);
    cfg.pre_ms      = getInt("\"pre_ms\"",      g_cfg.pre_ms);
    cfg.pre_gap     = getInt("\"pre_gap\"",     g_cfg.pre_gap);
    cfg.post_ms     = getInt("\"post_ms\"",     g_cfg.post_ms);
    cfg.pattern     = getInt("\"pattern\"",     g_cfg.pattern);

    // clamp to sane values
    auto clamp = [](int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; };
    cfg.pulse_short = clamp(cfg.pulse_short, 10, 2000);
    cfg.pulse_long  = clamp(cfg.pulse_long,  10, 2000);
    cfg.bit_gap     = clamp(cfg.bit_gap,     10, 2000);
    cfg.pre_ms      = clamp(cfg.pre_ms,      50, 5000);
    cfg.pre_gap     = clamp(cfg.pre_gap,     10, 2000);
    cfg.post_ms     = clamp(cfg.post_ms,     10, 2000);
    cfg.pattern     = clamp(cfg.pattern,      0,    2);

    g_cfg = cfg;
    blink_start();

    Serial.printf("cfg: short=%d long=%d gap=%d pre=%d preg=%d post=%d pat=%d\n",
        cfg.pulse_short, cfg.pulse_long, cfg.bit_gap,
        cfg.pre_ms, cfg.pre_gap, cfg.post_ms, cfg.pattern);

    _srv.send(200, "application/json", "{\"ok\":true}");
}

void server_start(const char *ssid) {
    strncpy(_ssid, ssid, sizeof(_ssid) - 1);

    _dns.start(53, "*", WiFi.softAPIP());

    _srv.on("/",       HTTP_GET,  handle_root);
    _srv.on("/config", HTTP_GET,  handle_config_get);
    _srv.on("/config", HTTP_POST, handle_config_post);
    _srv.onNotFound(handle_root);
    _srv.begin();
}

void server_handle() {
    _dns.processNextRequest();
    _srv.handleClient();
}
