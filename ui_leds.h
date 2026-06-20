#ifndef UI_LEDS_H
#define UI_LEDS_H

// =============================================================================
//  UI_LEDS.H — WS2812B LED effects page
//
//  Page layout (320x200 px):
//    - Name of the active effect (centred)
//    - 10 effect buttons: OFF / STATIC / BREATH / SPIN / RAINBOW /
//                         FIRE / VORTEX / PULSE / TWINKLE / WAVE
//    - Brightness slider 0-255
//    - 8 colour preset swatches
//
//  Hardware dependencies:
//    GPIO2 -> WS2812B fan   (12 LEDs)
//    GPIO3 -> WS2812B ring  (36 LEDs)
// =============================================================================

#include <Arduino.h>
#include <lvgl.h>

// Builds the page widgets inside the provided container
void ui_leds_build(lv_obj_t* parent);

// Called in loop() — animates SPIN/BREATH/RAINBOW and other effects
void ui_leds_tick();

// Hardware init: WS2812B GPIO2 + GPIO3
void ui_leds_init_hw();

// Visual update from MQTT (brightness slider + effect buttons)
void ui_leds_update_visual();

#endif // UI_LEDS_H
