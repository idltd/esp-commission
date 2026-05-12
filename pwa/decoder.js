'use strict';

// Manchester decoder for the ESP32 blink protocol.
//
// Protocol: 5 bps Manchester (1 = H→L mid-bit, 0 = L→H mid-bit)
// Frame: preamble(1s ON) | 0xAA(8b) | payload(40b) | XOR-checksum(8b) | end(500ms OFF)
//
// new BlinkDecoder({ bps, onDecoded({ suffix, pin, raw }), onProgress(phase, bitsReceived) })
//   bps defaults to 5
//   phase: 'idle' | 'preamble' | 'receiving'

const PREAMBLE_MIN = 8;
const CHARSET = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789';

export class BlinkDecoder {
  constructor({ bps = 5, onDecoded, onProgress, onError }) {
    this.bps        = bps;
    this.onDecoded  = onDecoded;
    this.onProgress = onProgress || (() => {});
    this.onError    = onError    || (() => {});
    this._reset();
  }

  setBps(bps) { this.bps = bps; this._reset(); }

  get _bitMs()  { return 1000 / this.bps; }
  get _halfMs() { return  500 / this.bps; }

  _reset() {
    const wasReceiving = this.state === 'RECEIVING';
    this.state      = 'IDLE';
    this.streak     = 0;
    this.lastHighTs = 0;
    this.syncMid    = 0;
    this.bits       = [];
    this.lastBitIdx = -1;
    if (wasReceiving) this.onProgress('idle', 0);
  }

  _setState(s) {
    this.state = s;
    this.onProgress(s.toLowerCase(), this.bits.length);
  }

  feed(bright, ts) {
    switch (this.state) {

      case 'IDLE':
        if (bright) {
          this.streak++;
          this.lastHighTs = ts;
          if (this.streak >= PREAMBLE_MIN) this._setState('PREAMBLE');
        } else {
          if (this.streak > 0) this.onProgress('idle', 0);
          this.streak = 0;
        }
        break;

      case 'PREAMBLE':
        if (bright) {
          this.lastHighTs = ts;
        } else {
          this.syncMid    = (this.lastHighTs + ts) / 2;
          this.bits       = [1];
          this.lastBitIdx = 0;
          this._setState('RECEIVING');
        }
        break;

      case 'RECEIVING': {
        const bitMs    = this._bitMs;
        const halfMs   = this._halfMs;
        const elapsed  = ts - (this.syncMid - halfMs / 2);
        if (elapsed < 0) break;

        const bitN     = Math.floor(elapsed / bitMs);
        const posInBit = elapsed % bitMs;

        if (bitN > this.lastBitIdx && posInBit < halfMs) {
          this.bits.push(bright ? 1 : 0);
          this.lastBitIdx = bitN;
          this.onProgress('receiving', this.bits.length);

          if (this.bits.length === 56) {
            this._validate();
            return;
          }
        }

        if (elapsed > 58 * bitMs) this._reset();
        break;
      }
    }
  }

  _validate() {
    const b = this.bits;

    let start = 0;
    for (let i = 0; i < 8; i++) start = (start << 1) | b[i];
    if (start !== 0xAA) {
      this.onError(`bad_sync 0x${start.toString(16).toUpperCase().padStart(2,'0')}`);
      this._reset(); return;
    }

    const raw = new Uint8Array(5);
    for (let byte = 0; byte < 5; byte++) {
      let v = 0;
      for (let i = 0; i < 8; i++) v = (v << 1) | b[8 + byte * 8 + i];
      raw[byte] = v;
    }

    let rxCs = 0;
    for (let i = 0; i < 8; i++) rxCs = (rxCs << 1) | b[48 + i];
    const cs = raw.reduce((a, v) => a ^ v, 0);

    if (cs !== rxCs) {
      const hex = Array.from(raw).map(v => v.toString(16).padStart(2,'0').toUpperCase()).join(' ');
      this.onError(`bad_cs calc:${cs.toString(16).toUpperCase().padStart(2,'0')} rx:${rxCs.toString(16).toUpperCase().padStart(2,'0')} [${hex}]`);
      this._reset(); return;
    }

    const s = [
      (raw[0] >> 2) & 0x3F,
      ((raw[0] & 0x3) << 4) | (raw[1] >> 4),
      ((raw[1] & 0xF) << 2) | (raw[2] >> 6),
      raw[2] & 0x3F
    ];
    const suffix = s.map(i => CHARSET[i] ?? '?').join('');
    const pin = [
      (raw[3] >> 4) & 0xF,
      raw[3] & 0xF,
      (raw[4] >> 4) & 0xF,
      raw[4] & 0xF
    ].join('');

    this.onDecoded({ suffix, pin, raw });
    this._reset();
  }
}
