# LED → Phone Camera: Comms Parameter POC

## Goal
Find the fastest reliable bit rate and simplest encoding an ESP32 LED can use to
transmit data to a phone camera via a browser PWA.

## What we do not know yet
- Fastest reliable bit rate for a generic phone at 30fps
- Whether simple on/off keying is sufficient or Manchester is needed
- Minimum pulse width the camera reliably captures

## Test setup
- **Transmitter**: small bright spot (~50px circle) on a dark screen — simulates an LED
- **Receiver**: phone camera via getUserMedia, sampling centre region

## What to vary
| Parameter | Values to test |
|---|---|
| Bit rate | 1, 2, 5, 10 bps |
| Encoding | Simple on/off, Manchester |

## What to measure
For each combination: what fraction of pulses does the phone actually capture.

## Test sequence
1. Transmitter sends a known repeating pulse pattern at a fixed rate
2. Receiver counts pulses seen vs pulses expected
3. Report: "at X bps, Y% captured"
4. Repeat for each rate

## Output
A recommended bit rate and encoding to use in the actual firmware and PWA.
Success = 95%+ pulse capture at the chosen rate across a few phone models.

## Pages
- `test-send.html` — configurable pulse generator (small dot, selectable rate)
- `test-receive.html` — camera reader, reports capture % per rate, no protocol decoding
