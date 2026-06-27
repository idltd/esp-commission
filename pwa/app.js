'use strict';

import { BlinkDecoder } from './decoder.js';

let fingerprint = null;
let stream      = null;

const isSecure = location.protocol === 'https:'
  || location.hostname === 'localhost'
  || location.hostname === '127.0.0.1';
if (!isSecure) show('screen-https');

const isInstalled = window.matchMedia('(display-mode: standalone)').matches
  || window.navigator.standalone === true;

if (!isInstalled && isSecure) {
  const prompt  = document.getElementById('install-prompt');
  const btnInst = document.getElementById('btn-install');
  const btnStart = document.getElementById('btn-start');
  const iosHint = document.getElementById('ios-hint');

  prompt.style.display = 'block';
  btnStart.style.marginTop = '8px';
  btnStart.textContent = 'Continue anyway';
  btnStart.classList.remove('primary');

  const isIOS = /iphone|ipad|ipod/i.test(navigator.userAgent);
  if (isIOS) {
    btnInst.style.display = 'none';
    iosHint.style.display = 'block';
  }

  btnInst.addEventListener('click', async () => {
    const evt = window._installEvt;
    if (!evt) return;
    evt.prompt();
    const { outcome } = await evt.userChoice;
    window._installEvt = null;
    if (outcome === 'accepted') prompt.style.display = 'none';
  });
}

function show(id) {
  document.querySelectorAll('.screen').forEach(s => s.classList.remove('active'));
  document.getElementById(id).classList.add('active');
}

// --- Scan screen ---
async function startScan() {
  show('screen-scan');

  const vid    = document.getElementById('vid');
  const preCvs = document.getElementById('preview');
  const pctx   = preCvs.getContext('2d');
  const strCvs = document.getElementById('strip');
  const sctx   = strCvs.getContext('2d');

  try {
    stream = await navigator.mediaDevices.getUserMedia({
      video: { facingMode: { ideal: 'environment' }, width: { ideal: 1280 } }
    });
    vid.srcObject = stream;
    await vid.play();
  } catch (e) {
    showError('Camera access denied: ' + e.message);
    return;
  }

  const thBuf = [];
  const TH_WIN = 90;
  const sigBuf = [];
  const bar     = document.getElementById('scan-bar');
  const counter = document.getElementById('bit-counter');
  const label   = document.getElementById('bit-label');
  const fpsEl   = document.getElementById('cam-fps');

  // Camera FPS tracking via vid.currentTime (changes once per decoded camera frame)
  let lastVidTime    = -1;
  let camFrameCount  = 0;
  let fpsWindowStart = 0;

  function onProgress(phase, val) {
    if (phase === 'recording') {
      counter.textContent = val + '%';
      label.textContent   = 'Recording signal…';
      bar.style.width     = val + '%';
    } else if (phase === 'scanning') {
      counter.textContent = '—';
      label.textContent   = 'Decoding…';
      bar.style.width     = '95%';
    } else {
      counter.textContent = '—';
      label.textContent   = 'Point at the blinking LED';
      bar.style.width     = '0%';
    }
  }

  function dbgLog(msg) {
    const el = document.getElementById('debug-log');
    if (!el) return;
    const line = new Date().toLocaleTimeString() + '  ' + msg;
    el.textContent = line + (el.textContent ? '\n' + el.textContent : '');
    // keep at most 40 lines
    const lines = el.textContent.split('\n');
    if (lines.length > 40) el.textContent = lines.slice(0, 40).join('\n');
  }

  const decoder = new BlinkDecoder({
    onDecoded({ suffix, pin }) {
      label.textContent = 'Read complete ✓';
      bar.style.width   = '100%';
      fingerprint = { suffix, pin };
      stopCamera();
      setTimeout(showConnect, 600);
    },
    onProgress,
    onError(msg)  { dbgLog('ERR  ' + msg); },
    onDebug(msg)  { dbgLog('dbg  ' + msg); }
  });

  function resize() {
    preCvs.width  = preCvs.offsetWidth;
    preCvs.height = preCvs.offsetHeight;
    strCvs.width  = strCvs.offsetWidth;
    strCvs.height = strCvs.offsetHeight;
    sigBuf.length = 0;
  }
  window.addEventListener('resize', resize);
  resize();

  function tick(ts) {
    if (!stream) return;
    requestAnimationFrame(tick);

    const vw = vid.videoWidth, vh = vid.videoHeight;
    if (!vw) return;

    // Track camera FPS (vid.currentTime advances once per decoded camera frame)
    if (vid.currentTime !== lastVidTime) {
      camFrameCount++;
      lastVidTime = vid.currentTime;
    }
    if (ts - fpsWindowStart >= 1000) {
      if (fpsEl) fpsEl.textContent = camFrameCount + ' fps';
      camFrameCount  = 0;
      fpsWindowStart = ts;
    }

    const cw = preCvs.width, ch = preCvs.height;
    const sc = Math.max(cw / vw, ch / vh);
    pctx.drawImage(vid, (cw - vw * sc) / 2, (ch - vh * sc) / 2, vw * sc, vh * sc);

    const sz = Math.max(20, Math.min(cw, ch) * 0.15 | 0);
    const bx = (cw - sz) >> 1, by = (ch - sz) >> 1;
    const px = pctx.getImageData(bx, by, sz, sz).data;
    let s = 0;
    for (let i = 0; i < px.length; i += 4)
      s += 0.299 * px[i] + 0.587 * px[i + 1] + 0.114 * px[i + 2];
    const brightness = s / (sz * sz);

    thBuf.push(brightness);
    if (thBuf.length > TH_WIN) thBuf.shift();
    const mn = Math.min(...thBuf), mx = Math.max(...thBuf);
    const thresh = (mn + mx) / 2;
    const valid  = (mx - mn) > 15;
    const bright = valid && brightness > thresh;

    pctx.strokeStyle = valid ? (bright ? '#3f3' : '#555') : '#333';
    pctx.lineWidth   = 3;
    pctx.strokeRect(bx, by, sz, sz);

    sigBuf.push(bright);
    if (sigBuf.length > strCvs.width) sigBuf.shift();
    sctx.fillStyle = '#000';
    sctx.fillRect(0, 0, strCvs.width, strCvs.height);
    const sh = strCvs.height;
    for (let i = 0; i < sigBuf.length; i++) {
      sctx.fillStyle = sigBuf[i] ? '#3f3' : '#1a1a1a';
      sctx.fillRect(i, 3, 1, sh - 6);
    }

    decoder.feed(bright, ts);
  }

  requestAnimationFrame(tick);
}

function stopCamera() {
  if (stream) { stream.getTracks().forEach(t => t.stop()); stream = null; }
}

// --- Connect screen ---
function showConnect() {
  const suffix   = fingerprint.suffix;
  const hostname = 'esp32-' + suffix.toLowerCase();
  document.getElementById('ssid-label').textContent = 'ESP-' + suffix;
  document.getElementById('pin-label').textContent  = fingerprint.pin;
  const link = document.getElementById('device-link');
  link.textContent = hostname + '.local';
  link.href        = 'http://' + hostname + '.local';
  navigator.clipboard.writeText(fingerprint.pin).catch(() => {});
  show('screen-connect');
}

function showError(msg) {
  document.getElementById('error-msg').textContent = msg;
  show('screen-error');
}

// --- Button wiring ---
document.getElementById('btn-start').addEventListener('click', startScan);
document.getElementById('btn-done').addEventListener('click',  () => { fingerprint = null; show('screen-home'); });
document.getElementById('btn-retry').addEventListener('click', () => { fingerprint = null; show('screen-home'); });

document.getElementById('btn-copy-debug').addEventListener('click', () => {
  const text = document.getElementById('debug-log').textContent;
  navigator.clipboard.writeText(text).catch(() => {});
});

if ('serviceWorker' in navigator)
  navigator.serviceWorker.register('./sw.js').catch(() => {});
