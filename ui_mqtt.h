#ifndef UI_MQTT_H
#define UI_MQTT_H

// =============================================================================
//  UI_MQTT.H — Home Assistant integration via MQTT
//
//  NON-BLOCKING PRINCIPLE:
//    If the MQTT broker is absent or unreachable, EVERYTHING ELSE works
//    normally. Reconnection happens in the background every 10 s.
//
//  CONFIGURATION in Config.h:
//    MQTT_BROKER   IP or hostname of the broker (e.g. "192.168.1.10")
//    MQTT_PORT     Port (default 1883)
//    MQTT_USER     Username (leave "" if no auth)
//    MQTT_PASS     Password (leave "" if no auth)
//
//  HOME ASSISTANT TOPICS (auto-discovery):
//
//  Commands received (ESP32 subscribes):
//    aerius/fan/speed/set        -> "0" to "100"
//    aerius/fan/switch/set       -> "ON" or "OFF"
//    aerius/fan/timer/set        -> seconds (e.g. "1800" = 30 min)
//    aerius/led/effect/set       -> "0" to "9"
//    aerius/led/color/set        -> "R,G,B" (e.g. "201,168,76")
//    aerius/led/brightness/set   -> "0" to "255"
//
//  States published (ESP32 sends):
//    aerius/fan/speed/state      -> "0" to "100"
//    aerius/fan/switch/state     -> "ON" or "OFF"
//    aerius/fan/timer/state      -> remaining seconds
//    aerius/led/effect/state     -> "0" to "9"
//    aerius/led/brightness/state -> "0" to "255"
//    aerius/sensor/temperature   -> "23.4"
//    aerius/sensor/humidity      -> "58.2"
//    aerius/sensor/cpu_temp      -> "42.1"
//    aerius/system/ip            -> "192.168.1.x"
//    aerius/system/rssi          -> "-65"
//    aerius/system/uptime        -> seconds
//
//  HOME ASSISTANT AUTO-DISCOVERY:
//    At startup, publishes MQTT Discovery configs to
//    homeassistant/... so HA automatically detects
//    all entities without manual configuration.
// =============================================================================

#include <Arduino.h>

// Called in setup() after WiFi.begin()
void mqtt_init();

// Called in loop() — handles reconnection + incoming messages
// Non-blocking: returns immediately if not connected
void mqtt_tick();

// Manual publish (called by ui_home/ui_leds when a state changes)
void mqtt_publish_fan_state();
void mqtt_publish_led_state();
void mqtt_publish_sensors();

#endif // UI_MQTT_H
