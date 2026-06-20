#ifndef UI_PRESENCE_H
#define UI_PRESENCE_H

// =============================================================================
//  UI_PRESENCE.H — LD2410C presence detection via UART
//
//  Full UART mode: uses the ld2410 library by ncmreynolds
//  Provides: presence detection, distance, motion/stationary distinction
//
//  WIRING:
//    LD2410C TX -> GPIO44 (ESP32 RX)
//    LD2410C RX -> GPIO43 (ESP32 TX)
//    LD2410C VCC -> 5V
//    LD2410C GND -> GND
// =============================================================================

#include <Arduino.h>
#include <lvgl.h>

extern bool     g_presence_detected;
extern uint16_t g_presence_distance;
extern bool     g_auto_mode;

void ui_presence_init();
void ui_presence_tick();
void ui_presence_build(lv_obj_t* parent_home);

#endif // UI_PRESENCE_H
