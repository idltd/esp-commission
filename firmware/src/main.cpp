#include <Arduino.h>
#include <ArduinoOTA.h>
#include <esp_random.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "led.h"
#include "blink.h"
#include "server.h"
#include "webui.h"

static bool _connected = false;

static const char CHARSET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

static void connect_wifi(const String &ssid, const String &pass, const String &name) {
    Serial.printf("Connecting to \"%s\"…\n", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(name.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts++ < 40) {
        delay(500);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi failed — clearing credentials and rebooting");
        Preferences prefs;
        prefs.begin("onboarding", false);
        prefs.clear();
        prefs.end();
        delay(500);
        ESP.restart();
    }

    Serial.printf("Connected: %s\n", WiFi.localIP().toString().c_str());
    if (MDNS.begin(name.c_str()))
        Serial.printf("mDNS: %s.local\n", name.c_str());
}

static void start_connected_mode(const String &name) {
    ArduinoOTA.setHostname(name.c_str());
    ArduinoOTA.onStart([]()              { Serial.println("OTA start"); });
    ArduinoOTA.onEnd([]()               { Serial.println("OTA done");  });
    ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA error %u\n", e); });
    ArduinoOTA.begin();
    Serial.printf("OTA ready — push to %s.local\n", name.c_str());

    webui_start(name.c_str(), WiFi.localIP().toString().c_str());
}

static void check_reset_button() {
    static bool     lastBtn    = HIGH;
    static int      tapCount   = 0;
    static uint32_t firstTapMs = 0;
    static uint32_t lastEdgeMs = 0;

    bool     btn = digitalRead(BUTTON_PIN);
    uint32_t now = millis();

    if (btn != lastBtn && (now - lastEdgeMs) > 50) {
        lastEdgeMs = now;
        lastBtn    = btn;

        if (btn == LOW) {
            if (tapCount == 0) firstTapMs = now;
            tapCount++;

            if (tapCount >= 3 && (now - firstTapMs) < 2000) {
                Serial.println("Triple-tap: factory reset");
                Preferences prefs;
                prefs.begin("onboarding", false);
                prefs.clear();
                prefs.end();
                delay(100);
                ESP.restart();
            }
        }
    }

    if (tapCount > 0 && (now - firstTapMs) > 2000)
        tapCount = 0;
}

void setup() {
    Serial.begin(115200);
    led_init();
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    Preferences prefs;
    prefs.begin("onboarding", true);
    bool   adopted = prefs.getUChar("adopted", 0) == 1;
    String ssid    = prefs.getString("wifi_ssid", "");
    String pass    = prefs.getString("wifi_pass",  "");
    String name    = prefs.getString("devname",    "device");
    prefs.end();

    if (adopted && ssid.length() > 0) {
        Serial.printf("Adopted as \"%s\" — connecting to %s\n", name.c_str(), ssid.c_str());
        connect_wifi(ssid, pass, name);
        start_connected_mode(name);
        _connected = true;
        return;
    }

    // Not yet adopted.
    // Suffix is MAC-derived (persistent, unique per device); PIN is fresh each boot.
    WiFi.mode(WIFI_AP_STA);
    char    suffix[5];
    uint8_t pin[4];
    {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        snprintf(suffix, 5, "%02X%02X", mac[4], mac[5]);
    }
    for (int i = 0; i < 4; i++) pin[i] = esp_random() % 10;

    Serial.printf("SSID: ESP-%s  PIN: %d%d%d%d\n",
                  suffix, pin[0], pin[1], pin[2], pin[3]);

    // Pack into 5 bytes: 4×6-bit suffix indices + 4×4-bit PIN digits.
    uint8_t s[4];
    for (int i = 0; i < 4; i++) {
        const char *cp = strchr(CHARSET, suffix[i]);
        s[i] = cp ? (uint8_t)(cp - CHARSET) : 0;
    }
    uint8_t payload[5];
    payload[0] = (s[0] << 2) | (s[1] >> 4);
    payload[1] = ((s[1] & 0xF) << 4) | (s[2] >> 2);
    payload[2] = ((s[2] & 0x3) << 6) | s[3];
    payload[3] = (pin[0] << 4) | pin[1];
    payload[4] = (pin[2] << 4) | pin[3];

    blink_start(payload);
    server_start(suffix, pin);
}

void loop() {
    check_reset_button();

    if (_connected) {
        ArduinoOTA.handle();
        webui_handle();
        return;
    }

    if (server_adopted()) {
        blink_stop();

        // Keep the AP and commissioning page alive so the user can read the
        // "Done!" message before the connection drops.
        uint32_t t = millis();
        while (millis() - t < 1500) {
            server_handle();
            delay(10);
        }

        Preferences prefs;
        prefs.begin("onboarding", true);
        String ssid = prefs.getString("wifi_ssid", "");
        String pass = prefs.getString("wifi_pass",  "");
        String name = prefs.getString("devname",    "device");
        prefs.end();

        Serial.printf("Adoption complete — connecting to \"%s\"\n", ssid.c_str());
        WiFi.softAPdisconnect(true);
        connect_wifi(ssid, pass, name);

        server_stop();
        start_connected_mode(name);
        _connected = true;
        return;
    }

    server_handle();
}
