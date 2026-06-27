# ESP Commission — Revised Kickoff (v2)

> The original design is preserved in `docs/kickoff.md`. This document describes what was actually built and why it diverged.

---

## Overview

A user-friendly onboarding system for ESP32-based IoT devices. The device blinks a short optical code on its LED; a phone camera PWA reads it, identifies the device, connects the user to the device's SoftAP, and presents a captive-portal setup page where the user enters their WiFi credentials and the PIN shown on screen. No app store. No factory-printed QR codes. No crypto.

The original design used X25519 public key cryptography to verify device identity and encrypt the adoption payload. That was replaced with a simpler PIN-based model — see [Design Decisions](#design-decisions).

---

## Repository Structure

```
/
├── docs/
│   ├── kickoff.md          ← original design (preserved)
│   └── kickoff-v2.md       ← this file (what was built)
├── firmware/
│   ├── platformio.ini
│   └── src/
│       ├── main.cpp        ← setup/loop, WiFi, OTA, factory reset
│       ├── blink.cpp/.h    ← LED PWM transmitter (FreeRTOS task)
│       ├── led.h           ← GPIO abstraction (active-high/low)
│       ├── server.cpp/.h   ← SoftAP HTTP server + captive portal
│       └── webui.cpp/.h    ← post-adoption status/OTA web UI
└── pwa/
    ├── index.html
    ├── app.js              ← screen flow, camera, AP connection
    ├── decoder.js          ← PWM optical decoder
    ├── manifest.json
    └── sw.js               ← service worker (offline cache)
```

---

## System Flow

```
[FIRST BOOT]
ESP32 powers up
→ Check NVS for "adopted" flag
→ If not adopted:
    - Derive 4-char suffix from last 2 bytes of MAC (e.g. "A1B2")
    - Generate random 2-digit PIN (0-99); expand to 4-digit display code
    - Start WiFi SoftAP (open, no password) SSID: "ESP-A1B2"
    - Start captive-portal HTTP server on 192.168.4.1
    - Start LED blink task (PWM encoding of suffix + PIN)

[USER ONBOARDS]
User opens PWA → taps "Add Device"
→ PWA opens camera, accumulates frames, decodes LED blink
    → extracts suffix (identifies which device) and 4-digit display PIN
→ PWA shows PIN to user and instructs: "Connect to WiFi: ESP-A1B2"
→ User joins SoftAP (phone auto-opens captive portal, or user taps link)
→ Captive portal page (served by ESP at 192.168.4.1):
    - Scans for nearby WiFi networks (async, shown in dropdown)
    - User picks home network, enters password
    - User enters 4-digit PIN shown by PWA
    - On submit: POST /adopt {wifi_ssid, wifi_password, pin}
→ ESP verifies PIN (3 attempts allowed), stores credentials to NVS, sets adopted=1
→ ESP responds 200 OK, closes SoftAP after 2.5 s grace period
→ ESP reboots into connected mode

[SUBSEQUENT BOOTS]
ESP32 powers up
→ Loads adopted=1, ssid, pass, devname from NVS
→ Connects to home WiFi
→ DHCP hostname + mDNS as {devname}.local  (e.g. esp32-a1b2.local)
→ ArduinoOTA listener active (push from PlatformIO or IDE)
→ Web UI at http://{devname}.local — shows status, IP, uptime, OTA trigger

[FACTORY RESET]
Triple-tap the BOOT button within 2 seconds
→ NVS cleared → reboot → back to first-boot commissioning mode
```

---

## Blink Protocol (PWM)

The LED blinks a 32-bit frame using PWM pulse-width encoding. Each bit is a HIGH pulse whose duration encodes the bit value, followed by a fixed LOW gap.

**Why PWM over Manchester:**
Manchester requires sampling the signal level at the midpoint of each bit period. At camera framerates (~12 fps, 83 ms per frame), a 100 ms bit period gives only ~1 sample per bit, making systematic single-bit errors unavoidable. PWM measures edge-to-edge pulse width, which is robust to non-uniform sampling intervals — the width is accurate as long as you catch both edges, regardless of how many frames fall in between.

**Timing constants (`blink.cpp`):**

| Symbol        | Value   | Meaning                          |
|---------------|---------|----------------------------------|
| `PULSE_SHORT` | 150 ms  | HIGH duration for bit 0          |
| `PULSE_LONG`  | 350 ms  | HIGH duration for bit 1          |
| `BIT_GAP_MS`  | 200 ms  | LOW gap after every bit          |
| `PRE_MS`      | 1000 ms | Preamble HIGH (frame sync)       |
| `PRE_GAP_MS`  | 300 ms  | LOW gap between preamble and data|
| `POST_MS`     | 500 ms  | Trailing LOW before next cycle   |

Decode threshold: 250 ms (midpoint between 150 and 350). Max timing error per bit: ±82 ms (one camera frame), leaving 18 ms margin each side. Full cycle: ~16 s.

**Frame structure — 32 bits (4 bytes, MSB first):**

```
byte 0:  suffix nibbles 0-1  (4 bits each, hex 0x0-0xF)
byte 1:  suffix nibbles 2-3
byte 2:  PIN BCD  high nibble = tens, low nibble = units  (0-99)
byte 3:  CRC8 = byte0 ^ byte1 ^ byte2
```

**PIN display expansion:**
The 2-digit raw PIN `n` (stored in one byte) is expanded to a 4-digit display code before showing to the user and before verifying on the server:

```
a = n / 10   (tens)
b = n % 10   (units)
display = [a, (a+b)%10, b, (a*3+b*7)%10]
```

Both the firmware (packing) and PWA (decoding) agree on this expansion. The server verifies all four digits. The expansion prevents a user from accidentally succeeding with a wrong-but-close PIN — every digit is coupled to both the tens and units.

---

## Firmware

**Framework:** Arduino via PlatformIO (not ESP-IDF). Arduino abstractions (`WiFi`, `WebServer`, `Preferences`, `ArduinoOTA`, `ESPmDNS`) are simpler to work with than raw ESP-IDF for this use case.

**Supported boards:**

| PlatformIO env      | Board           | LED pin | Active | Button |
|---------------------|-----------------|---------|--------|--------|
| `esp32dev`          | ESP32 DevKit    | GPIO2   | HIGH   | GPIO0  |
| `esp32dev-ota`      | ESP32 DevKit    | GPIO2   | HIGH   | GPIO0  |
| `c3-supermini`      | LOLIN C3 Mini   | GPIO8   | LOW    | GPIO9  |
| `c3-supermini-ota`  | LOLIN C3 Mini   | GPIO8   | LOW    | GPIO9  |

OTA environments use `upload_protocol = espota` — push directly from PlatformIO over the network once the device is adopted.

**NVS namespace: `"onboarding"`**

| Key         | Type    | Notes                        |
|-------------|---------|------------------------------|
| `adopted`   | uint8   | 0 = commissioning, 1 = done  |
| `wifi_ssid` | string  |                              |
| `wifi_pass` | string  |                              |
| `devname`   | string  | MAC-derived, e.g. `esp32-a1b2` |

**HTTP endpoints (SoftAP only):**

```
GET  /          → captive portal setup page (self-contained HTML)
GET  /networks  → JSON array of visible SSIDs (async WiFi scan)
POST /adopt     → {wifi_ssid, wifi_password, pin}
                   → 200 {ok:true} on success
                   → 403 {error:wrong_pin} / {error:locked} after 3 failures
```

After 3 failed PIN attempts the `/adopt` endpoint is locked for the current boot. Factory reset or reboot is required.

**Post-adoption:**
- ArduinoOTA active on port 3232
- Web UI at port 80: shows device name, IP, uptime, free heap; OTA upload form

---

## PWA Decoder (`decoder.js`)

Record-then-decode strategy:

1. Accumulate timestamped brightness samples from camera frames into a 25 s rolling buffer.
2. Every 800 ms, scan the buffer for preambles (HIGH pulses ≥ 600 ms).
3. From each preamble, walk forward measuring HIGH pulse widths. Classify each as 0 (< 250 ms) or 1 (≥ 250 ms). Record confidence = |width − 250|.
4. Verify CRC8 and PIN digit range. If valid, emit result.
5. If CRC fails, try in order:
   - Voted majority across accumulated frames (flips bits where more frames voted the other way).
   - 1-bit correction on the 8 least-confident bits.
   - 2-bit correction on the 4 least-confident pairs.

This layered approach handles occasional bad frames without requiring perfect reception on every pass.

**PWA flow (screens):**

1. **Home** — "Add Device" button
2. **Scan** — camera live view, recording progress bar (0-20 s), then "Scanning…"
3. **Result** — shows SSID to join (`ESP-A1B2`) and 4-digit PIN; "Open Setup Page" button
4. **Setup page** — the captive portal (served by the device, opened in browser)
5. After adoption: user returns to PWA home (device is now on home WiFi)

---

## Security Model

| Threat | Mitigation |
|---|---|
| Rogue device substitution | Physical presence required to read LED; SSID suffix matches blinked suffix |
| PIN brute-force over SoftAP | 3 attempts then locked; reboot required to retry |
| Eavesdropping on SoftAP | WiFi credentials sent in plaintext over open SoftAP — same model as Philips Hue, Sonos, etc. Acceptable for home WiFi threat model |
| Replay of /adopt request | Endpoint locks after first success; reboot clears state |
| Physical access after adoption | Triple-tap factory reset is intentional; device is typically in a fixed location |

**Trust boundary post-adoption: home WiFi network membership.** No credential encryption — this was a deliberate simplification from the original design (see below).

---

## Design Decisions

### Why not X25519 / encrypted adoption?

The original design generated an X25519 keypair on the device, blinked the SHA-256 fingerprint of the public key, and encrypted the WiFi credentials before sending them. This was abandoned because:

1. **Reliability first.** The Manchester encoding needed for a 48-bit payload at reliable camera-decoded bitrates was the hardest problem. Solving the encoding problem was the priority; adding crypto on top would have increased complexity before the base channel was proven.

2. **Simplified payload.** A 2-digit PIN fits in 1 byte (vs 6 bytes for a crypto fingerprint). Fewer bits = faster frame, fewer error opportunities, simpler decode.

3. **Equivalent home-network security.** All mainstream consumer IoT devices (Hue, Sonos, Nest, etc.) send WiFi credentials over an open SoftAP in plaintext. The physical presence requirement (you must see the LED blink) and the 3-attempt lockout provide equivalent or better security for the home threat model.

The crypto path remains viable as a future upgrade if the deployment context changes (e.g., devices in shared spaces, multi-tenant environments).

### Why Arduino over ESP-IDF?

`Preferences`, `WiFi`, `WebServer`, `ArduinoOTA`, and `ESPmDNS` from the Arduino ecosystem are well-tested and require far less boilerplate than their ESP-IDF equivalents. For a single-purpose commissioning firmware, the productivity gain outweighs the slight overhead.

### Why a MAC-derived device name?

The original design let the user pick a name during commissioning. In practice, user-facing naming is better done post-adoption (via the web UI or a future cloud management layer). The MAC suffix gives a stable, unique, human-readable default that survives factory resets.

---

## Dependencies

**Firmware (via PlatformIO):**
- `espressif32` platform, `arduino` framework
- `Arduino-ESP32` bundled libraries: `WiFi`, `WebServer`, `DNSServer`, `Preferences`, `ESPmDNS`, `ArduinoOTA`, `esp_random`
- No external libraries required

**PWA:**
- Vanilla JS, no framework
- `getUserMedia` camera API (requires HTTPS — host on GitHub Pages or any HTTPS static host)
- Service worker for offline capability

---

## Deferred / Future

- Encrypted adoption payload (X25519 + NaCl box) — original design, shelved for simplicity
- User-defined device name during commissioning
- Cloud device registry / remote management
- HTTPS OTA with firmware image signing
- Secure boot + flash encryption (efuse)
- Multi-hub / mesh networking
