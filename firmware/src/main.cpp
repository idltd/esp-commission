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

// hex digit value: '0'-'9' -> 0-9, 'A'-'F' -> 10-15
static uint8_t hv(char c) { return (uint8_t)(c >= 'A' ? c - 'A' + 10 : c - '0'); }

static void connect_wifi(const String &ssid, const String &pass, const String &name) {
    Serial.printf("Connecting to \"%s\"…\n", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);  // re-arm DHCP so setHostname takes effect
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
    if (MDNS.begin(name.c_str())) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("mDNS: %s.local\n", name.c_str());
    }
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
    char suffix[5];
    {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        snprintf(suffix, 5, "%02X%02X", mac[4], mac[5]);
    }

    // 2-digit PIN (0-99) expanded to 4 digits for display and server verification.
    // Expansion: a=tens, b=units, c=(a+b)%10, d=(a*3+b*7)%10 -> display [a,c,b,d]
    int n = (int)(esp_random() % 100);
    int a = n / 10, b = n % 10;
    int c = (a + b) % 10;
    int d = (a * 3 + b * 7) % 10;
    uint8_t pin[4] = { (uint8_t)a, (uint8_t)c, (uint8_t)b, (uint8_t)d };

    Serial.printf("SSID: ESP-%s  PIN: %d%d%d%d\n", suffix, a, c, b, d);

    // Pack 4 bytes: suffix nibbles (4-bit hex) | PIN BCD | CRC8
    uint8_t payload[4];
    payload[0] = (hv(suffix[0]) << 4) | hv(suffix[1]);
    payload[1] = (hv(suffix[2]) << 4) | hv(suffix[3]);
    payload[2] = ((uint8_t)a << 4) | (uint8_t)b;
    payload[3] = payload[0] ^ payload[1] ^ payload[2];

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
        while (millis() - t < 2500) {
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
