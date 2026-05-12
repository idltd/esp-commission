'use strict';

import { BlinkDecoder } from './decoder.js';

let fingerprint = null;
let stream      = null;

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

  function onProgress(phase, bits) {
    if (phase === 'receiving') {
      counter.textContent = bits + ' / 56';
      label.textContent   = (56 - bits) + ' bits to go';
      bar.style.width     = Math.round((bits / 56) * 100) + '%';
    } else if (phase === 'preamble') {
      counter.textContent = '—';
      label.textContent   = 'Signal found — locking on…';
      bar.style.width     = '4%';
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
    onError(msg) { dbgLog(msg); }
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
  document.getElementById('ssid-label').textContent = 'ESP-' + fingerprint.suffix;
  document.getElementById('pin-label').textContent  = fingerprint.pin;
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

if ('serviceWorker' in navigator)
  navigator.serviceWorker.register('./sw.js').catch(() => {});
