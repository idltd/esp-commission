#pragma once

struct BlinkConfig {
    int pulse_short;  // ms  HIGH for bit 0
    int pulse_long;   // ms  HIGH for bit 1
    int bit_gap;      // ms  LOW after every bit
    int pre_ms;       // ms  preamble HIGH
    int pre_gap;      // ms  LOW after preamble
    int post_ms;      // ms  trailing LOW before next cycle
    int pattern;      // 0=alternating  1=all-0  2=all-1
};

extern BlinkConfig g_cfg;

void blink_start();
void blink_stop();
