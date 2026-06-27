#pragma once
#include <Arduino.h>

#ifndef LED_ACTIVE_LOW
  #define LED_ACTIVE_LOW 0
#endif

inline void led_init() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_ACTIVE_LOW ? HIGH : LOW);
}

inline void led_set(bool on) {
  digitalWrite(LED_PIN, (on ^ (bool)LED_ACTIVE_LOW) ? HIGH : LOW);
}
