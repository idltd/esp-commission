#pragma once
#include <stdint.h>

// PWM optical encoding: short HIGH=0, long HIGH=1, fixed LOW gap between bits.
// payload is 4 bytes: [suffix_hi, suffix_lo, pin_bcd, crc8]
void blink_start(const uint8_t payload[4]);
void blink_stop();
