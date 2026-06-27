#pragma once
#include <stdint.h>

// PWM optical encoding: short HIGH=0, long HIGH=1, fixed LOW gap between bits.
// payload is 5 bytes: [suffix_hi, suffix_lo, pin_bcd_hi, pin_bcd_lo, crc8]
void blink_start(const uint8_t payload[5]);
void blink_stop();
