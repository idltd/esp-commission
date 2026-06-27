# ESP32 Optical Onboarding System — Project Kickoff

> **Historical — original design, not what was built.**
> The X25519 / key-hash approach described here was abandoned during implementation.
> See `docs/kickoff-v2.md` for what was actually built and why it diverged.

## Overview

A secure, user-friendly onboarding system for ESP32-based IoT sensors targeting non-technical home users. The core insight: use the ESP32's LED as a one-way optical channel to bootstrap trust — the device blinks a fingerprint of its public key, a PWA reads it via phone camera, and this is used to verify the device identity before handing over WiFi credentials. No app store. No pre-shared secrets. No factory-printed QR codes.

---

## Repository Structure

```
/
├── docs/
│   └── KICKOFF.md          ← this file
├── firmware/               ← ESP-IDF project
│   ├── main/
│   │   └── main.c
│   ├── CMakeLists.txt
│   └── sdkconfig.defaults
└── pwa/                    ← Progressive Web App
    ├── index.html
    ├── manifest.json
    ├── sw.js               ← service worker (offline capable)
    └── src/
        ├── app.js
        ├── blink-decoder.js
        └── onboarding.js
```

---

## System Flow

```
[FIRST BOOT]
ESP32 powers up
→ Check NVS for existing keypair
→ If none: generate X25519 keypair, store in NVS
→ Begin blinking fingerprint on LED (see Blink Protocol)
→ Start WiFi SoftAP (open, no password) SSID: "ESP32-Setup"
→ Start HTTP server on 192.168.4.1

[USER ONBOARDS]
User opens PWA → taps "Add Device"
→ PWA opens camera, decodes LED blink → extracts fingerprint
→ PWA instructs user to join "ESP32-Setup" network
  (warns: "your phone will ask about no internet — tap Yes")
→ PWA fetches GET http://192.168.4.1/pubkey
→ PWA verifies: SHA-256(pubkey)[0..5] === fingerprint from LED
  → mismatch: abort with error
  → match: show "Found a device — name it:"
→ User enters a name (or accepts default)
→ PWA posts encrypted payload to POST http://192.168.4.1/adopt
→ ESP decrypts, extracts WiFi SSID + password + device name
→ ESP stores name in NVS, connects to home WiFi, closes SoftAP
→ PWA user rejoins home WiFi
→ PWA confirms device is online at {name}.local

[SUBSEQUENT BOOTS]
ESP32 powers up
→ Loads keypair + name from NVS
→ Connects to stored WiFi network
→ Announces via mDNS as {name}.local
→ Awaits OTA updates or sensor polling
```

---

## Blink Protocol

The LED blinks a 6-byte (48-bit) fingerprint: the first 6 bytes of SHA-256 of the device's X25519 public key.

**Parameters:**
- Bit rate: 10 bps (100ms per bit) — reliable at 30fps phone camera
- Encoding: Manchester encoding (self-clocking; 0 = low→high transition, 1 = high→low)
- Frame structure:

```
[PREAMBLE] [START] [48 bits payload] [8 bits checksum] [END]

Preamble:  1000ms solid ON  (camera sync)
Start:     10101010          (1 byte alternating, sync marker)
Payload:   48 bits           (6 bytes fingerprint, MSB first)
Checksum:  8 bits            (XOR of all 6 payload bytes)
End:       1000ms solid OFF
```

- Total transmission time: ~1s preamble + ~16 bits sync + 56 bits data = ~8 seconds
- Loop continuously until SoftAP closes (adoption complete)

**PWA decode approach:**
- `getUserMedia` video stream, process frames via canvas
- Sample centre region of frame, threshold to binary
- Detect preamble (sustained bright), then clock in bits
- Validate checksum before accepting fingerprint
- Target 3+ successful identical reads before proceeding (noise rejection)

---

## Firmware (ESP-IDF)

**Key components:**

### NVS (Non-Volatile Storage)
```c
// Namespace: "onboarding"
// Keys:
//   "privkey"  → 32 bytes X25519 private key
//   "pubkey"   → 32 bytes X25519 public key
//   "adopted"  → uint8, 0 or 1
//   "devname"  → string, device name
//   "wifi_ssid"→ string
//   "wifi_pass"→ string
```

### Keypair Generation
- Use `mbedtls_entropy` + `mbedtls_ctr_drbg` for entropy
- Generate X25519 keypair via mbedTLS
- Persist to NVS on first boot only

### LED Blink Task
- FreeRTOS task, runs on core 0
- Implements the blink protocol above
- Default LED pin: GPIO2 (built-in on most ESP32 devkits)
- For passive breakout boards: use GPIO pin defined in `menuconfig` (default GPIO2)
- Task is deleted once adoption completes

### HTTP Endpoints
```
GET  /pubkey   → returns raw 32-byte public key as hex string
                 always available while SoftAP is up

POST /adopt    → accepts encrypted JSON payload
                 locks out after first attempt (successful or not)
                 returns 403 {"error":"already_attempted"} thereafter
```

### Adoption Lockout
```c
static bool adoption_attempted = false;
// Set to true on first POST to /adopt, regardless of outcome
// Resets only on reboot (not persisted to NVS)
```

### Encrypted Payload (PWA → ESP)
PWA encrypts with ESP's X25519 public key using ECIES (or NaCl box):
```json
{
  "wifi_ssid": "HomeNetwork",
  "wifi_password": "secret",
  "device_name": "garden"
}
```
ESP decrypts with its private key, validates JSON, stores to NVS.

### WiFi + mDNS (post-adoption)
```c
mdns_init();
mdns_hostname_set(device_name);       // → garden.local
mdns_instance_name_set(device_name);
```
- Re-announce on reconnect
- Fall back name: `esp32-{MAC[3:]}` e.g. `esp32-a1b2c3`

### OTA
- Use `esp_https_ota`
- Poll endpoint configurable via NVS post-adoption
- Trust boundary: home WiFi network membership (no code signing in v1)

---

## PWA

**Requirements:**
- Works offline after first load (service worker caches all assets)
- No app store — installable PWA via `manifest.json`
- Camera access via `getUserMedia`
- Runs entirely in browser during onboarding (no server calls needed)
- HTTPS required for camera access — host on any static host (GitHub Pages etc.)

**Screens / flow:**

1. **Home** — "Add Device" button
2. **Scan** — camera view, live decode, progress indicator ("Reading device...")
3. **Network switch prompt** — inline instruction card with the warning message (see UX Copy)
4. **Verify + Name** — shows fingerprint confirmed, input for device name, default pre-filled
5. **Commissioning** — spinner while ESP connects to WiFi
6. **Done** — "{name}.local is online" confirmation

**Error states:**
- Fingerprint mismatch → "Could not verify device identity. Start again."
- 403 from /adopt → see UX copy below
- Device not found after adoption → "Taking longer than expected — check the device has a light showing"

---

## UX Copy (exact strings)

### Network switch warning (shown before user joins SoftAP):
> To add this device, your phone will briefly connect to it directly — you'll lose internet access for a few seconds while this happens.
>
> If your phone asks whether to stay connected to a network without internet, tap Yes (or Stay Connected).
>
> You'll be back online automatically once the device has been added.

### Already attempted error (403 from /adopt):
> The new device has already been through one unsuccessful setup attempt.
>
> To try again, turn the new device off and on again.

---

## Passive Breakout Board (LED-less ESP32 modules)

For ESP32 modules without a programmable LED:
- Simple PCB: ESP32 socket + LED + resistor on a known GPIO
- Default GPIO: 2 (consistent with devkit default, configurable via `menuconfig`)
- Board may supply power via USB or onboard regulator
- No firmware changes required — just configure LED GPIO at build time

---

## Security Model

| Threat | Mitigation |
|---|---|
| MITM on SoftAP | WiFi creds encrypted to ESP pubkey; attacker cannot decrypt |
| Rogue device spoofing | Optical fingerprint must match pubkey — requires physical presence |
| Late joiner on SoftAP | /adopt locks out after first attempt |
| Replay attack | Single-use endpoint per boot |
| Physical tampering | Out of scope for v1 (efuse/secure boot deferred) |

Trust boundary post-adoption: **home WiFi network membership.** This is consistent with the model used by mainstream consumer IoT devices (Sonos, Philips Hue, etc.) and appropriate for the home user threat model.

---

## Deferred / Future

- Secure boot + flash encryption (efuse) — complexity not warranted for v1
- Firmware image signing for OTA
- Cloud device registry / remote management
- Multi-hub / mesh networking
- Device revocation flow

---

## Key Dependencies

**Firmware:**
- ESP-IDF v5.x
- mbedTLS (bundled with ESP-IDF) — keypair generation, ECIES encryption

**PWA:**
- Vanilla JS preferred (minimal dependencies)
- `tweetnacl.js` or `libsodium.js` — for ECIES/box encryption on PWA side
- No framework required — keep it simple and offline-capable

---

## Open Questions for Implementation

1. Confirm ECIES vs NaCl box for the adoption payload encryption — NaCl box (`nacl.box`) is simpler and has good JS + C library support on both sides
2. LED GPIO pin — confirm GPIO2 as project default or make prominent in `menuconfig`
3. PWA hosting — GitHub Pages is simplest for v1; needs HTTPS for camera API
