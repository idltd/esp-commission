#include "blink.h"
#include "led.h"
#include <Arduino.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const int PULSE_SHORT  = 150;   // ms  HIGH duration for bit 0
static const int PULSE_LONG   = 350;   // ms  HIGH duration for bit 1
static const int BIT_GAP_MS   = 200;   // ms  LOW gap after every bit
static const int PRE_MS       = 1000;  // ms  preamble HIGH
static const int PRE_GAP_MS   = 300;   // ms  LOW gap between preamble and data
static const int POST_MS      = 500;   // ms  trailing LOW before next cycle

static TaskHandle_t _task = nullptr;
static uint8_t      _payload[4];

static void send_bit(bool bit) {
    led_set(true);
    vTaskDelay(pdMS_TO_TICKS(bit ? PULSE_LONG : PULSE_SHORT));
    led_set(false);
    vTaskDelay(pdMS_TO_TICKS(BIT_GAP_MS));
}

static void blink_task(void *) {
    while (true) {
        led_set(true);
        vTaskDelay(pdMS_TO_TICKS(PRE_MS));
        led_set(false);
        vTaskDelay(pdMS_TO_TICKS(PRE_GAP_MS));

        for (int i = 0; i < 4; i++)
            for (int b = 7; b >= 0; b--)
                send_bit((_payload[i] >> b) & 1);

        led_set(false);
        vTaskDelay(pdMS_TO_TICKS(POST_MS));
    }
}

void blink_start(const uint8_t payload[4]) {
    blink_stop();
    memcpy(_payload, payload, 4);
    xTaskCreate(blink_task, "blink", 2048, nullptr, 1, &_task);
}

void blink_stop() {
    if (_task) { vTaskDelete(_task); _task = nullptr; }
    led_set(false);
}
