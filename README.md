# ESP Commission

Zero-touch WiFi provisioning for ESP32 devices using a phone camera. The device
blinks its identity as an optical PWM signal; a browser-based PWA reads it,
connects to the device's AP, and hands over your WiFi credentials — verified
with a 4-digit PIN so only the right device gets your password.

No app store. No QR codes. No pre-shared secrets.

**Live commissioning PWA → https://idltd.github.io/esp-commission/**

---

## How it works

```
  ┌─────────────┐   blink (PWM optical)   ┌──────────────────┐
  │  ESP32      │ ───────────────────────► │  Phone camera    │
  │  (LED)      │                          │  (PWA decoder)   │
  └─────────────┘                          └──────────────────┘
        │                                          │
        │  SoftAP "ESP-XXYY" (open, 1 client max) │  POST /adopt
        │ ◄────────────────────────────────────────┘
        │  { wifi_ssid, wifi_password, pin }
        │
        ▼  verified + stored in NVS
  ┌─────────────┐
  │  Home WiFi  │  esp32-xxyy.local  ·  OTA  ·  device web UI
  └─────────────┘
```

### User flow

1. Open the PWA from your home screen and scan the blinking LED → PWA shows PIN
2. Connect your phone to the `ESP-XXYY` WiFi network
3. The captive portal opens automatically — enter your WiFi credentials and PIN
4. The device joins your network and shows `http://esp32-xxyy.local` as the next step

### Optical protocol

The LED blinks a 40-bit payload using PWM encoding — pulse width determines
the bit value, with a fixed LOW gap after every bit.

| Symbol   | HIGH duration | Meaning |
|----------|---------------|---------|
| Bit 0    | 150 ms        | zero |
| Bit 1    | 350 ms        | one |
| Gap      | 200 ms LOW    | after every bit |
| Preamble | 1000 ms HIGH + 300 ms LOW | start of frame |

**Payload — 5 bytes, MSB first:**

| Byte | Content |
|------|---------|
| 0    | Suffix nibbles 0–1 (first two hex chars of 4-char MAC-derived suffix) |
| 1    | Suffix nibbles 2–3 (last two hex chars) |
| 2    | PIN BCD digits 0,1 (high nibble = digit 0, low = digit 1) |
| 3    | PIN BCD digits 2,3 (high nibble = digit 2, low = digit 3) |
| 4    | CRC8 = byte0 ^ byte1 ^ byte2 ^ byte3 |

The PIN is four independently random digits (0000–9999). The device locks after
3 failed PIN attempts and requires a power-cycle to reset.

---

## Hardware targets

| PlatformIO env     | Board               | LED pin              | Button |
|--------------------|---------------------|----------------------|--------|
| `esp32dev`         | ESP32 dev module    | GPIO 2 (active-high) | GPIO 0 |
| `esp32dev-ota`     | ESP32 dev module    | GPIO 2 (active-high) | GPIO 0 |
| `c3-supermini`     | ESP32-C3 Super Mini | GPIO 8 (active-low)  | GPIO 9 |
| `c3-supermini-ota` | ESP32-C3 Super Mini | GPIO 8 (active-low)  | GPIO 9 |

The `-ota` envs use `upload_protocol = espota` — flash via USB first, then push OTA for subsequent updates.

Triple-tap the button within 2 seconds to factory-reset any device (clears NVS, reboots into commissioning mode).

---

## PWA decoder

The decoder buffers ~25 seconds of camera signal, then:

1. Finds preamble edges (HIGH pulses ≥ 600 ms)
2. Measures each subsequent HIGH pulse width — < 250 ms = 0, ≥ 250 ms = 1
3. Scores per-bit confidence as distance from the 250 ms threshold
4. Accumulates votes across multiple frame passes
5. On CRC fail: tries voted majority, then 1- and 2-bit flips on the least-confident bits

The PWA is a zero-dependency installable PWA — no app store, works offline after first load.
Camera access requires HTTPS, which GitHub Pages provides.

---

## Post-commissioning web UI

Once commissioned the device joins your WiFi and serves a web UI at
**`http://esp32-xxyy.local`** (mDNS, port 80). The UI is a demo template intended
to be replaced with your application's controls.

**Application section** (replace with your own code)  
Demo controls: device label (text), onboard LED toggle (live), report interval.
Config saved to NVS namespace `appconfig`, LED state restored on boot.

**Network section**  
Shows current SSID and signal strength. Collapsible forms to change WiFi
credentials or rename the device — both save to NVS and reboot.

**Firmware update**  
Browser-based OTA upload via HTTP multipart POST. Shows build date and upload
progress bar. Device reboots automatically after a successful flash.
NVS is untouched by OTA — commissioning credentials survive firmware updates.

**Device status** (collapsed)  
Uptime, CPU MHz, temperature, free/min heap, sketch size, chip model, SDK version.

### API endpoints (post-commissioning)

| Method | Path       | Description |
|--------|------------|-------------|
| GET    | `/`        | Web UI page |
| GET    | `/status`  | Device stats JSON |
| GET    | `/app`     | Application config JSON |
| POST   | `/app`     | Update application config |
| POST   | `/led`     | Set LED state `{"state":true}` |
| POST   | `/network` | Change WiFi credentials + reboot |
| POST   | `/rename`  | Rename device + reboot |
| POST   | `/update`  | OTA firmware upload (multipart) |

---

## Building and flashing

Requires [PlatformIO](https://platformio.org/).

```bash
# Flash via USB
pio run -e esp32dev       -t upload   # ESP32 dev module
pio run -e c3-supermini   -t upload   # ESP32-C3 Super Mini

# OTA push (device must already be on WiFi)
pio run -e esp32dev-ota   -t upload
pio run -e c3-supermini-ota -t upload
```

---

## Repository layout

```
firmware/                 PlatformIO project (Arduino framework)
  platformio.ini
  src/
    main.cpp              WiFi connect, adoption flow, OTA loop
    blink.cpp             Optical PWM transmitter (FreeRTOS task)
    server.cpp            Commissioning AP + captive portal + /adopt
    webui.cpp             Post-adoption web UI template + OTA handler

pwa/                      Progressive Web App (deployed to GitHub Pages)
  index.html
  app.js                  Scan flow, connect screen
  decoder.js              Record-then-decode PWM optical decoder
  sw.js                   Service worker — offline cache

poc/                      Optical speed research — camera calibration tool
  firmware/               ESP32-C3 firmware: blinks configurable PWM pattern
  send.html               Browser LED simulator (screen flashes PWM)
  receive.html            Camera reader with pulse-width histogram

docs/                     Design notes and protocol history
```

---

## Security notes

- AP is open (no WiFi password) but limited to one client at a time
- PIN has 10,000 possible values; 3-attempt lockout prevents brute force
- WiFi credentials are transmitted in plain text over the local AP link
- Commissioning window is short — the AP drops as soon as adoption succeeds
- OTA updates do not require re-commissioning (credentials survive in NVS)

---

## License

MIT
