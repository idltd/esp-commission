'use strict';

// PWM optical decoder — record-then-decode.
//
// Firmware constants (blink.cpp):
//   PULSE_SHORT = 150 ms  (bit 0)
//   PULSE_LONG  = 350 ms  (bit 1)
//   BIT_GAP_MS  = 200 ms  (LOW gap after every bit)
//   PRE_MS      = 1000 ms (preamble HIGH)
//   PRE_GAP_MS  = 300 ms  (LOW gap after preamble, before data)
//   POST_MS     = 500 ms  (trailing LOW before next cycle)
//
// Frame: 40 bits (5 bytes, MSB first)
//   byte 0: suffix hex nibbles 0-1  (4 bits each, 0-15)
//   byte 1: suffix hex nibbles 2-3
//   byte 2: PIN BCD digits 0,1  (high nibble=digit0, low nibble=digit1)
//   byte 3: PIN BCD digits 2,3  (high nibble=digit2, low nibble=digit3)
//   byte 4: CRC8 = byte0 ^ byte1 ^ byte2 ^ byte3
//
// Decode strategy:
//   1. Measure HIGH pulse widths from buffer. <250ms=0, >=250ms=1.
//      Each bit carries a confidence score (distance from threshold).
//   2. Accumulate per-bit votes across multiple frame passes.
//   3. On CRC fail: try voted majority, then 1- and 2-bit corrections
//      on the least-confident bits.

const PULSE_SHORT = 150;   // ms  firmware PULSE_SHORT
const PULSE_LONG  = 350;   // ms  firmware PULSE_LONG
const THRESHOLD   = (PULSE_SHORT + PULSE_LONG) / 2;  // 250 ms
const PRE_MIN     = 600;   // ms  min HIGH for preamble (> PULSE_LONG, unambiguous)
const BITS_FRAME  = 40;
const BUF_MS      = 25000; // ms  rolling buffer
const SCAN_INT    = 800;   // ms  scan cadence
const READY_MS    = 20000; // ms  expected max time for one full cycle in buffer
const HEX         = '0123456789ABCDEF';

export class BlinkDecoder {
  constructor({ onDecoded, onProgress, onError, onDebug } = {}) {
    this._dec  = onDecoded  || (() => {});
    this._prog = onProgress || (() => {});
    this._err  = onError    || (() => {});
    this._dbg  = onDebug    || (() => {});
    this._buf  = [];
    this._t0   = null;
    this._lastScan  = 0;
    this._votes     = new Int16Array(BITS_FRAME);  // per-bit vote accumulators
    this._voteCount = 0;                           // frames accumulated
  }

  feed(bright, ts) {
    if (this._t0 === null) this._t0 = ts;
    this._buf.push({ bright, ts });

    const cutoff = ts - BUF_MS;
    while (this._buf.length > 0 && this._buf[0].ts < cutoff)
      this._buf.shift();

    const span = ts - this._t0;
    this._prog('recording', Math.min(100, Math.round(span / READY_MS * 100)));

    if (ts - this._lastScan >= SCAN_INT) {
      this._lastScan = ts;
      this._scan();
    }
  }

  _scan() {
    const preambles = this._findPreambles();
    this._dbg(`scan: ${preambles.length} preamble(s), buf=${this._buf.length}`);
    if (!preambles.length) return;
    this._prog('scanning', 0);

    for (const tf of preambles) {
      const result = this._decodePulses(tf);
      if (!result) { this._dbg(`tf=${tf|0} insufficient data`); continue; }

      const { bits, confidence } = result;

      // Accumulate votes for cross-reading majority
      this._voteCount++;
      for (let i = 0; i < BITS_FRAME; i++)
        this._votes[i] += bits[i] ? 1 : -1;

      // 1. Primary decode
      if (this._tryDecode(bits, 'primary')) return;

      // 2. Voted majority (requires >=2 frames)
      if (this._voteCount >= 2) {
        const voted = Array.from(this._votes, v => v > 0 ? 1 : 0);
        if (this._tryDecode(voted, `voted(${this._voteCount})`)) return;
      }

      // 3. Trial corrections on least-confident bits
      const ranked = confidence
        .map((c, i) => ({ i, c }))
        .sort((a, b) => a.c - b.c);

      // 1-bit flips (top 8 least confident)
      for (let k = 0; k < Math.min(8, ranked.length); k++) {
        const f = [...bits]; f[ranked[k].i] ^= 1;
        if (this._tryDecode(f, `1flip[${ranked[k].i}]`)) return;
      }

      // 2-bit flips (top 4 pairs)
      for (let k = 0; k < Math.min(4, ranked.length); k++) {
        for (let j = k + 1; j < Math.min(4, ranked.length); j++) {
          const f = [...bits]; f[ranked[k].i] ^= 1; f[ranked[j].i] ^= 1;
          if (this._tryDecode(f, `2flip[${ranked[k].i},${ranked[j].i}]`)) return;
        }
      }
    }
  }

  _findPreambles() {
    const buf = this._buf, out = [];
    let hiStart = null;
    for (let i = 1; i < buf.length; i++) {
      const p = buf[i - 1], c = buf[i];
      if (!p.bright && c.bright) {
        hiStart = c.ts;
      } else if (p.bright && !c.bright) {
        if (hiStart !== null && c.ts - hiStart >= PRE_MIN)
          out.push((p.ts + c.ts) / 2);
        hiStart = null;
      }
    }
    return out;
  }

  // Walk the buffer from tf, measuring successive HIGH pulse widths.
  // Returns { bits[32], confidence[32] } or null if buffer runs out.
  _decodePulses(tf) {
    const buf = this._buf;
    const bits = [], confidence = [];

    // Advance cursor past tf
    let cur = 0;
    while (cur < buf.length && buf[cur].ts <= tf) cur++;

    for (let n = 0; n < BITS_FRAME; n++) {
      // Rising edge (dark → bright)
      let ri = -1;
      for (let i = Math.max(1, cur); i < buf.length; i++) {
        if (!buf[i - 1].bright && buf[i].bright) { ri = i; break; }
      }
      if (ri < 0) return null;

      // Falling edge (bright → dark)
      let fi = -1;
      for (let i = ri + 1; i < buf.length; i++) {
        if (buf[i - 1].bright && !buf[i].bright) { fi = i; break; }
      }
      if (fi < 0) return null;

      const rise = (buf[ri - 1].ts + buf[ri].ts) / 2;
      const fall = (buf[fi - 1].ts + buf[fi].ts) / 2;
      const w    = fall - rise;
      const bit  = w >= THRESHOLD ? 1 : 0;
      const conf = Math.abs(w - THRESHOLD);

      bits.push(bit);
      confidence.push(conf);
      this._dbg(`bit[${n}]=${bit} w=${w|0}ms conf=${conf|0}`);

      cur = fi;
    }

    return { bits, confidence };
  }

  _tryDecode(bits, label) {
    const H = v => v.toString(16).padStart(2, '0').toUpperCase();
    const bytes = Array.from({ length: 5 }, (_, i) =>
      bits.slice(i * 8, i * 8 + 8).reduce((v, b, j) => v | (b << (7 - j)), 0)
    );
    const [b0, b1, b2, b3, b4] = bytes;

    const crc = b0 ^ b1 ^ b2 ^ b3;
    if (crc !== b4) {
      this._err(`bad_crc ${H(crc)}!=${H(b4)} [${label}]`);
      return false;
    }

    // Suffix: four 4-bit hex nibbles
    const suffix = [b0 >> 4, b0 & 0xF, b1 >> 4, b1 & 0xF].map(n => HEX[n]).join('');

    // PIN: four BCD digits, each nibble must be 0-9
    const d = [(b2 >> 4) & 0xF, b2 & 0xF, (b3 >> 4) & 0xF, b3 & 0xF];
    if (d.some(n => n > 9)) {
      this._err(`bad_pin nibbles ${d} [${label}]`);
      return false;
    }

    const pin = d.join('');

    this._dbg(`OK suffix=${suffix} pin=${pin} [${label}]`);
    this._votes.fill(0); this._voteCount = 0;
    this._dec({ suffix, pin });
    return true;
  }
}
