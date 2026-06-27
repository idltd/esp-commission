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
static char       _ip[16];

static const char PAGE[] = R"HTML(
<!DOCTYPE html><html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Blink Calibrate</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
html,body{height:100%;background:#000;color:#eee;font-family:monospace;
          display:flex;flex-direction:column;overflow:hidden}
#cam-wrap{flex:1;position:relative;min-height:0}
#preview{width:100%;height:100%;display:block}
#vid{position:absolute;opacity:0;pointer-events:none;width:1px;height:1px}
#cam-msg{position:absolute;inset:0;background:rgba(0,0,0,.9);
         display:flex;flex-direction:column;align-items:center;
         justify-content:center;gap:16px;cursor:pointer;text-align:center;padding:24px}
#cam-msg.gone{display:none}
.cm-h{color:#fff;font-size:1.2em}
.cm-n{color:#888;font-size:.82em;line-height:1.8}
.cm-n b{color:#ccc}
#strip{flex-shrink:0;height:20px;width:100%;display:block}
#qual{flex-shrink:0;display:flex;align-items:center;gap:10px;padding:9px 12px;
      border-top:1px solid #1c1c1c;min-height:44px}
#qdot{width:13px;height:13px;border-radius:50%;background:#333;flex-shrink:0}
#qtext{font-size:.88em;line-height:1.4;color:#999}
#qtext b{color:#fff;font-size:1.05em}
#mrow{flex-shrink:0;display:grid;grid-template-columns:repeat(4,1fr);
      border-top:1px solid #1c1c1c}
.mc{padding:6px 6px;border-right:1px solid #111}
.mc:last-child{border-right:none}
.ml{color:#555;font-size:9px;text-transform:uppercase;margin-bottom:3px;letter-spacing:.04em}
.mv{font-size:1em;font-weight:bold;color:#888}
.mv.g{color:#3f3}.mv.w{color:#f84}.mv.b{color:#f55}
#hw{flex-shrink:0;border-top:1px solid #1c1c1c;padding:6px 0 0;background:#030303}
#hist{display:block;width:100%;height:64px}
#hax{display:flex;justify-content:space-between;padding:1px 6px 4px;font-size:9px;color:#555}
#cal{flex-shrink:0;border-top:1px solid #1c1c1c;padding:10px 12px 14px}
.ctitle{color:#555;font-size:9px;text-transform:uppercase;letter-spacing:.05em;margin-bottom:9px}
#rrow{display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:10px}
.ri{display:flex;flex-direction:column;align-items:center;min-width:54px}
.rv{color:#3f3;font-size:1.05em;font-weight:bold}
.rl{color:#555;font-size:9px;margin-top:2px}
#btn-app{padding:8px 16px;border:1px solid #3f3;border-radius:6px;
         background:#091509;color:#3f3;font-size:12px;cursor:pointer;
         font-family:monospace;flex-shrink:0}
#btn-app:disabled{border-color:#333;color:#444;background:#000;cursor:default}
#btn-app:not(:disabled):active{background:#143014}
#btn-tog{background:none;border:1px solid #282828;border-radius:5px;
         padding:6px 12px;color:#666;font-size:11px;cursor:pointer;font-family:monospace}
.hidden{display:none!important}
#manual{margin-top:9px}
.row{display:flex;align-items:center;gap:8px;margin-bottom:7px;font-size:11px}
.row label{width:50px;color:#666;text-align:right;flex-shrink:0}
.row input[type=range]{flex:1;accent-color:#3f3}
.val{width:52px;color:#3f3;text-align:right;flex-shrink:0}
.pats{display:flex;gap:5px;margin-top:2px;margin-bottom:6px}
.pat{flex:1;padding:6px 2px;border:1px solid #222;border-radius:5px;
     background:#0a0a0a;color:#555;font-size:10px;cursor:pointer;text-align:center}
.pat.on{border-color:#666;color:#ddd;background:#181818}
#st2{font-size:10px;color:#666;min-height:14px;text-align:center;margin-top:3px}
</style>
</head>
<body>

<div id="cam-wrap">
  <canvas id="preview"></canvas>
  <video id="vid" autoplay playsinline muted></video>
  <div id="cam-msg">
    <div class="cm-h">&#9654; Tap to start camera</div>
    <div class="cm-n">
      Point at the ESP's LED. Sample zone = centre box.<br><br>
      <b>One-time setup in Chrome:</b><br>
      <b>chrome://flags</b> &rarr; Insecure origins treated as secure<br>
      &rarr; add <b>http://__IP__</b> &rarr; Relaunch
    </div>
  </div>
</div>

<canvas id="strip"></canvas>

<div id="qual">
  <div id="qdot"></div>
  <div id="qtext">Tap camera to begin calibration</div>
</div>

<div id="mrow">
  <div class="mc"><div class="ml">cam fps</div><div class="mv" id="mfps">&#8212;</div></div>
  <div class="mc"><div class="ml">SHORT peak</div><div class="mv" id="msp">&#8212;</div></div>
  <div class="mc"><div class="ml">LONG peak</div><div class="mv" id="mlp">&#8212;</div></div>
  <div class="mc"><div class="ml">separation</div><div class="mv" id="msep">&#8212;</div></div>
</div>

<div id="hw">
  <canvas id="hist"></canvas>
  <div id="hax"><span>0</span><span>100</span><span>200</span><span>300</span><span>400</span><span>500ms</span></div>
</div>

<div id="cal">
  <div class="ctitle">Calibration &mdash; recommended for this camera</div>
  <div id="rrow">
    <div class="ri"><div class="rv" id="rc-s">&#8212;</div><div class="rl">SHORT</div></div>
    <div class="ri"><div class="rv" id="rc-l">&#8212;</div><div class="rl">LONG</div></div>
    <div class="ri"><div class="rv" id="rc-g">&#8212;</div><div class="rl">GAP</div></div>
    <div class="ri"><div class="rv" id="rc-t">&#8212;</div><div class="rl">THRESH</div></div>
    <div class="ri"><div class="rv" id="rc-b" style="color:#ff9">&#8212;</div><div class="rl">est bps</div></div>
    <button id="btn-app" disabled>Apply to ESP</button>
    <button id="btn-tog">Manual &#9660;</button>
  </div>
  <div id="manual" class="hidden">
    <div class="row"><label>SHORT</label><input id="ps" type="range" min="40" max="300" value="150"><span class="val" id="vps">150ms</span></div>
    <div class="row"><label>LONG</label> <input id="pl" type="range" min="80" max="600" value="350"><span class="val" id="vpl">350ms</span></div>
    <div class="row"><label>GAP</label>  <input id="pg" type="range" min="30" max="300" value="200"><span class="val" id="vpg">200ms</span></div>
  </div>
  <div class="pats">
    <button class="pat on" data-p="0">Alternating</button>
    <button class="pat"    data-p="1">All SHORT</button>
    <button class="pat"    data-p="2">All LONG</button>
  </div>
  <div id="st2">&#8212;</div>
</div>

<script>
'use strict';
var vid=document.getElementById('vid'),
    pre=document.getElementById('preview'),pctx=pre.getContext('2d'),
    str=document.getElementById('strip'),sctx=str.getContext('2d'),
    hc=document.getElementById('hist'),hctx=hc.getContext('2d');
var thBuf=[],TW=90,sigBuf=[],trans=[],pws=[],prev=null,es=null;
var fpsQ=[],mfps=0,lvt=-1;
var recS=0,recL=0,recG=0;

function mv(id,v,c){var e=document.getElementById(id);e.textContent=v;e.className='mv'+(c?' '+c:'');}

function analyse(){
  var BN=50,BW=10,TH=250,bins=new Array(BN).fill(0);
  for(var i=0;i<pws.length;i++){var b=Math.min(Math.floor(pws[i]/BW),BN-1);bins[b]++;}
  var mc=1;for(var i=0;i<BN;i++)if(bins[i]>mc)mc=bins[i];
  var W=hc.width,H=hc.height,bw=W/BN;
  if(W&&H){
    hctx.clearRect(0,0,W,H);
    hctx.strokeStyle='#3a3a3a';hctx.lineWidth=1;hctx.setLineDash([2,3]);
    hctx.beginPath();hctx.moveTo((TH/BW)*bw,0);hctx.lineTo((TH/BW)*bw,H);hctx.stroke();
    hctx.setLineDash([]);
    for(var i=0;i<BN;i++){if(!bins[i])continue;var h=(bins[i]/mc)*H;
      hctx.fillStyle=i*BW<TH?'#833':'#383';hctx.fillRect(i*bw,H-h,Math.max(bw-1,1),h);}
  }
  var sP=-1,sM=0,lP=-1,lM=0;
  for(var i=0;i<BN;i++){var ms=i*BW;
    if(ms<TH&&bins[i]>sM){sM=bins[i];sP=ms+BW/2;}
    if(ms>=TH&&bins[i]>lM){lM=bins[i];lP=ms+BW/2;}}
  mv('msp',sP>=0?Math.round(sP)+'ms':'—',sP>=0?'g':'');
  mv('mlp',lP>=0?Math.round(lP)+'ms':'—',lP>=0?'g':'');
  var n=pws.length,dot=document.getElementById('qdot'),qt=document.getElementById('qtext');
  if(sP>=0&&lP>=0){
    var sep=lP-sP;
    mv('msep',Math.round(sep)+'ms',sep>150?'g':sep>80?'w':'b');
    var bpsEst=(1000/((sP+200+lP+200)/2)).toFixed(1);
    if(n<20){
      dot.style.background='#f84';qt.innerHTML='<b>Calibrating&hellip;</b> '+n+' pulses captured';
    } else if(sep>150){
      dot.style.background='#3f3';
      qt.innerHTML='<b>STRONG</b> &ensp;S&approx;'+Math.round(sP)+'ms &ensp;L&approx;'+Math.round(lP)+'ms &ensp;sep '+Math.round(sep)+'ms &#10003; &ensp;<span style="color:#888">'+bpsEst+' bps</span>';
    } else if(sep>80){
      dot.style.background='#f84';
      qt.innerHTML='<b>MARGINAL</b> &ensp;sep '+Math.round(sep)+'ms &mdash; tap Apply to use recommended timings';
    } else {
      dot.style.background='#f55';
      qt.innerHTML='<b>WEAK</b> &ensp;sep only '+Math.round(sep)+'ms &mdash; pulses indistinguishable';
    }
    if(n>=10){
      var thresh=(sP+lP)/2,atRisk=0,fMs=mfps>0?1000/mfps:33;
      for(var i=0;i<pws.length;i++)if(Math.abs(pws[i]-thresh)<fMs)atRisk++;
      var pct=Math.round(100*atRisk/n);
      document.getElementById('st2').textContent=
        pct+'% of '+n+' pulses within 1 frame of threshold (target: 0%)';
    }
  } else if(n>3){
    dot.style.background='#f84';
    qt.innerHTML='<b>Detecting&hellip;</b> only '+(sP>=0?'SHORT':'LONG')+' pulses visible so far';
  }
}

function updRec(){
  if(mfps<=0)return;
  var fMs=1000/mfps;
  recS=Math.max(100,Math.ceil(4*fMs/10)*10);
  recL=Math.round(recS*2.4/10)*10;
  recG=Math.max(60,Math.ceil(2*fMs/10)*10);
  var recT=Math.round((recS+recL)/20)*10;
  document.getElementById('rc-s').textContent=recS+'ms';
  document.getElementById('rc-l').textContent=recL+'ms';
  document.getElementById('rc-g').textContent=recG+'ms';
  document.getElementById('rc-t').textContent=recT+'ms';
  var recBps=(2000/(recS+recL+2*recG)).toFixed(2);
  document.getElementById('rc-b').textContent=recBps;
  document.getElementById('btn-app').disabled=false;
}

function sample(ts){
  var vw=vid.videoWidth,vh=vid.videoHeight;if(!vw||!vh)return;
  if(vid.currentTime!==lvt){
    lvt=vid.currentTime;fpsQ.push(ts);
    while(fpsQ.length&&fpsQ[0]<ts-5000)fpsQ.shift();
    if(fpsQ.length>=5){mfps=(fpsQ.length-1)/((ts-fpsQ[0])/1000);updRec();}
  }
  var cw=pre.width,ch=pre.height,sc=Math.max(cw/vw,ch/vh);
  pctx.drawImage(vid,(cw-vw*sc)/2,(ch-vh*sc)/2,vw*sc,vh*sc);
  var sz=Math.max(20,Math.min(cw,ch)*.15|0),bx=(cw-sz)>>1,by=(ch-sz)>>1;
  var px=pctx.getImageData(bx,by,sz,sz).data,s=0;
  for(var i=0;i<px.length;i+=4)s+=.299*px[i]+.587*px[i+1]+.114*px[i+2];
  var b=s/(sz*sz);
  thBuf.push(b);if(thBuf.length>TW)thBuf.shift();
  var mn=thBuf[0],mx=thBuf[0];
  for(var i=1;i<thBuf.length;i++){if(thBuf[i]<mn)mn=thBuf[i];if(thBuf[i]>mx)mx=thBuf[i];}
  var valid=(mx-mn)>15,bright=valid&&b>(mn+mx)/2;
  pctx.strokeStyle=bright?'#3f3':valid?'#666':'#333';pctx.lineWidth=2;
  pctx.strokeRect(bx,by,sz,sz);
  if(valid&&prev!==null){
    if(bright&&!prev){es=ts;trans.push(ts);}
    else if(!bright&&prev&&es!==null){var w=ts-es;if(w>20&&w<700){pws.push(w);if(pws.length>300)pws.shift();}es=null;}
  }
  prev=valid?bright:prev;
  while(trans.length&&trans[0]<ts-3000)trans.shift();
  sigBuf.push(bright);if(sigBuf.length>str.width)sigBuf.shift();
  sctx.fillStyle='#000';sctx.fillRect(0,0,str.width,str.height);
  for(var i=0;i<sigBuf.length;i++){sctx.fillStyle=sigBuf[i]?'#3f3':'#111';sctx.fillRect(i,3,1,str.height-6);}
  mv('mfps',mfps>0?mfps.toFixed(1):'—',mfps>=28?'g':mfps>=20?'w':'');
  if(pws.length>=5)analyse();
  else{
    var dot=document.getElementById('qdot'),qt=document.getElementById('qtext');
    dot.style.background=valid?'#f84':'#333';
    qt.innerHTML=valid?'<b>Signal detected</b> — collecting pulses&hellip;':'<b>No signal</b> — point camera at LED';
  }
}

function resize(){
  pre.width=pre.offsetWidth;pre.height=pre.offsetHeight;
  str.width=str.offsetWidth;str.height=str.offsetHeight;
  hc.width=hc.offsetWidth;hc.height=hc.offsetHeight;sigBuf.length=0;
}
window.addEventListener('resize',resize);resize();

document.getElementById('cam-msg').addEventListener('click',async function(){
  this.querySelector('.cm-h').textContent='Opening camera…';
  try{
    var s=await navigator.mediaDevices.getUserMedia({video:{facingMode:{ideal:'environment'},width:{ideal:1280}}});
    vid.srcObject=s;await new Promise(function(r){vid.onloadedmetadata=r;});vid.play();
    this.classList.add('gone');(function t(ts){sample(ts);requestAnimationFrame(t);})(0);
  }catch(e){this.querySelector('.cm-h').textContent='⚠ '+e.message;}
});

// ---- LED control ----
var S=150,L=350,G=200,PAT=0,tmr=null;
function sched(){clearTimeout(tmr);tmr=setTimeout(applyLed,400);}
function applyLed(){
  fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({pulse_short:S,pulse_long:L,bit_gap:G,pre_ms:1000,pre_gap:300,post_ms:500,pattern:PAT})
  }).then(function(r){document.getElementById('st2').style.color=r.ok?'#3f3':'#f55';})
   .catch(function(){document.getElementById('st2').style.color='#f55';});
}

document.getElementById('btn-app').addEventListener('click',function(){
  if(!recS)return;
  S=recS;L=recL;G=recG;
  document.getElementById('ps').value=S;document.getElementById('vps').textContent=S+'ms';
  document.getElementById('pl').value=L;document.getElementById('vpl').textContent=L+'ms';
  document.getElementById('pg').value=G;document.getElementById('vpg').textContent=G+'ms';
  var b=this;b.textContent='Applied ✓';setTimeout(function(){b.textContent='Apply to ESP';},1500);
  applyLed();
});

document.getElementById('btn-tog').addEventListener('click',function(){
  var m=document.getElementById('manual'),open=!m.classList.contains('hidden');
  m.classList.toggle('hidden',open);this.textContent=open?'Manual ▾':'Manual ▴';
});

[['ps','vps',function(v){S=v;}],['pl','vpl',function(v){L=v;}],['pg','vpg',function(v){G=v;}]].forEach(function(t){
  document.getElementById(t[0]).addEventListener('input',function(){
    document.getElementById(t[1]).textContent=this.value+'ms';t[2](+this.value);sched();
  });
});

document.querySelector('.pats').addEventListener('click',function(e){
  var b=e.target.closest('.pat');if(!b)return;
  document.querySelectorAll('.pat').forEach(function(x){x.classList.remove('on');});
  b.classList.add('on');PAT=+b.dataset.p;sched();
});

fetch('/config').then(function(r){return r.json();}).then(function(c){
  S=c.pulse_short;L=c.pulse_long;G=c.bit_gap;PAT=c.pattern;
  document.getElementById('ps').value=S;document.getElementById('vps').textContent=S+'ms';
  document.getElementById('pl').value=L;document.getElementById('vpl').textContent=L+'ms';
  document.getElementById('pg').value=G;document.getElementById('vpg').textContent=G+'ms';
  document.querySelectorAll('.pat').forEach(function(b){b.classList.toggle('on',+b.dataset.p===PAT);});
}).catch(function(){});
</script>
</body></html>
)HTML";

static void handle_root() {
    String page = PAGE;
    page.replace("__SSID__", _ssid);
    page.replace("__IP__",   _ip);
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

    Serial.printf("cfg: short=%d long=%d gap=%d pat=%d\n",
        cfg.pulse_short, cfg.pulse_long, cfg.bit_gap, cfg.pattern);

    _srv.send(200, "application/json", "{\"ok\":true}");
}

void server_start(const char *ssid) {
    strncpy(_ssid, ssid, sizeof(_ssid) - 1);
    strncpy(_ip, WiFi.softAPIP().toString().c_str(), sizeof(_ip) - 1);

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
