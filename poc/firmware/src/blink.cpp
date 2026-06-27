#include "blink.h"
#include "led.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

BlinkConfig g_cfg = { 150, 350, 200, 1000, 300, 500, 0 };

static TaskHandle_t _task = nullptr;

static void send_bit(bool bit) {
    led_set(true);
    vTaskDelay(pdMS_TO_TICKS(bit ? g_cfg.pulse_long : g_cfg.pulse_short));
    led_set(false);
    vTaskDelay(pdMS_TO_TICKS(g_cfg.bit_gap));
}

static void blink_task(void*) {
    while (true) {
        led_set(true);
        vTaskDelay(pdMS_TO_TICKS(g_cfg.pre_ms));
        led_set(false);
        vTaskDelay(pdMS_TO_TICKS(g_cfg.pre_gap));

        for (int i = 0; i < 32; i++) {
            bool bit = (g_cfg.pattern == 2) ? true
                     : (g_cfg.pattern == 1) ? false
                     : (i % 2 == 0);
            send_bit(bit);
        }

        led_set(false);
        vTaskDelay(pdMS_TO_TICKS(g_cfg.post_ms));
    }
}

void blink_start() {
    blink_stop();
    xTaskCreate(blink_task, "blink", 2048, nullptr, 1, &_task);
}

void blink_stop() {
    if (_task) { vTaskDelete(_task); _task = nullptr; }
    led_set(false);
}
