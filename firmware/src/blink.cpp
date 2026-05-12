#include "blink.h"
#include "led.h"
#include <Arduino.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const int BPS     = 5;
static const int HALF_MS = 500 / BPS;

static TaskHandle_t _task = nullptr;
static uint8_t      _payload[5];

static void send_bit(bool bit) {
    led_set(bit);
    vTaskDelay(pdMS_TO_TICKS(HALF_MS));
    led_set(!bit);
    vTaskDelay(pdMS_TO_TICKS(HALF_MS));
}

static void send_byte(uint8_t b) {
    for (int i = 7; i >= 0; i--) send_bit((b >> i) & 1);
}

static void blink_task(void *) {
    while (true) {
        uint8_t cs = 0;
        for (int i = 0; i < 5; i++) cs ^= _payload[i];

        led_set(true);
        vTaskDelay(pdMS_TO_TICKS(1000));

        send_byte(0xAA);
        for (int i = 0; i < 5; i++) send_byte(_payload[i]);
        send_byte(cs);

        led_set(false);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void blink_start(const uint8_t payload[5]) {
    blink_stop();
    memcpy(_payload, payload, 5);
    xTaskCreate(blink_task, "blink", 2048, nullptr, 1, &_task);
}

void blink_stop() {
    if (_task) {
        vTaskDelete(_task);
        _task = nullptr;
    }
    led_set(false);
}
