#include <Arduino.h>
#include <WiFi.h>
#include "led.h"
#include "blink.h"
#include "server.h"

void setup() {
    Serial.begin(115200);
    led_init();
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    uint8_t mac[6];
    WiFi.macAddress(mac);
    char ssid[20];
    snprintf(ssid, sizeof(ssid), "ESP-BLINK-%02X%02X", mac[4], mac[5]);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid);
    Serial.printf("SoftAP: %s  IP: %s\n", ssid, WiFi.softAPIP().toString().c_str());

    server_start(ssid);
    blink_start();
    Serial.printf("Blinking — short=%d long=%d gap=%d pat=%d\n",
        g_cfg.pulse_short, g_cfg.pulse_long, g_cfg.bit_gap, g_cfg.pattern);
}

void loop() {
    server_handle();
}
