#pragma once
#include <stdint.h>

// Start open SoftAP (SSID = "ESP-XXXX") and HTTP server.
// suffix is 4 chars [A-Z0-9]; pin is 4 digits [0-9] verified on /adopt.
void server_start(const char suffix[4], const uint8_t pin[4]);

void server_handle();
void server_stop();
bool server_adopted();
