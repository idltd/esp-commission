# ESP Commission

Zero-touch WiFi provisioning for ESP32 devices using a phone camera. The device
blinks its identity as an optical PWM signal; a browser-based PWA reads it,
connects to the device's AP, and hands over your WiFi credentials — verified
with a PIN so only the right device gets your password.

No app store. No QR codes. No pre-shared secrets.

**Live commissioning tool → https://idltd.github.io/esp-commission/**

---

## How it works

```
  ┌─────────────┐   blink (PWM optical)   ┌──────────────────┐
  │  ESP32      │ ───────────────────────► │  Phone camera    │
  │  (LED)      │                          │  (PWA decoder)   │
  └─────────────┘                          └──────────────────┘
        │                                          │
        │  SoftAP "ESP-XXYY"                       │  POST /adopt
        │ ◄────────────────────────────────────────┘
        │  { wifi_ssid, wifi_password, pin }
        │
        ▼  verified + stored in NVS
  ┌─────────────┐
  │  Home WiFi  │  esp32-xxyy.local  ·  OTA  ·  status web UI
  └─────────────┘
```

### Optical protocol

The LED blinks a 32-bit payload using PWM encoding — pulse width determines
the bit value, with a fixed LOW gap between every bit.

| Symbol   | HIGH duration | Meaning              |
|----------|--------------|----------------------|
| Bit 0    | 150 ms       | zero                 |
| Bit 1    | 350 ms       | one                  |
| Gap      | 200 ms LOW   | between every bit    |
| Preamble | 1000 ms HIGH + 300 ms LOW | start of frame |

**Payload — 4 bytes, MSB first:**

| Byte | Content |
|------|---------|
| 0    | MAC suffix nibbles \[3\]\[2\] |
| 1    | MAC suffix nibbles \[1\]\[0\] |
| 2    | PIN BCD — high nibble = tens, low = units (0–99) |
| 3    | CRC8 = byte0 ^ byte1 ^ byte2 |

The 2-digit PIN is displayed as a 4-digit code to make it easier to read from
across a room: `[a, (a+b)%10, b, (a×3+b×7)%10]` where a = tens, b = units.

---

## Hardware targets

| PlatformIO env    | Board                   | LED pin              | Button |
|-------------------|-------------------------|----------------------|--------|
| `esp32dev`        | ESP32 dev module        | GPIO 2               | GPIO 0 |
| `c3-supermini`    | ESP32-C3 Super Mini     | GPIO 8 (active-low)  | GPIO 9 |
| `c3-supermini-ota`| ESP32-C3 Super Mini     | OTA upload only      | —      |

Triple-tap the button within 2 seconds to factory-reset any device.

---

## PWA decoder

The decoder buffers ~20 seconds of camera signal, then:

1. Finds preamble edges (HIGH pulses ≥ 600 ms)
2. Measures each subsequent HIGH pulse width — < 250 ms = 0, ≥ 250 ms = 1
3. Scores per-bit confidence as distance from the 250 ms threshold
4. Accumulates votes across multiple frame passes
5. On CRC fail: tries voted majority, then 1- and 2-bit flips on the
   least-confident bits

The PWA is a zero-dependency installable PWA — no app store, works offline
after first load. Camera access requires HTTPS, which GitHub Pages provides.

---

## Post-adoption

Once commissioned the device:

- Joins your WiFi as **`esp32-xxyy.local`** (last 2 MAC bytes, lowercase hex)
- Registers via mDNS (HTTP on port 80)
- Accepts OTA firmware pushes via ArduinoOTA
- Serves a status page at its IP: network info, system stats, memory, and
  a firmware upload field for manual OTA

---

## Building and flashing

Requires [PlatformIO](https://platformio.org/).

```bash
# Flash via USB
pio run -e c3-supermini -t upload

# OTA push (device must already be on the network)
pio run -e c3-supermini-ota -t upload
```

---

## Repository layout

```
firmware/           PlatformIO project (Arduino framework, ESP-IDF)
  platformio.ini
  src/
    main.cpp        WiFi connect, adoption flow, OTA loop
    blink.cpp       Optical PWM transmitter (FreeRTOS task)
    server.cpp      Commissioning AP + /adopt endpoint
    webui.cpp       Post-adoption status page + HTTP OTA handler

pwa/                Progressive Web App (deployed to GitHub Pages)
  index.html
  app.js            Scan flow, connect screen
  decoder.js        Record-then-decode PWM optical decoder
  sw.js             Service worker — offline cache

docs/               Design notes and protocol spec
```

---

## License

MIT
