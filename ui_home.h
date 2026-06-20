#ifndef UI_HOME_H
#define UI_HOME_H

// =============================================================================
//  UI_HOME.H — Main dashboard page: speed control, ON/OFF, BME280 temperature, timer
//
//  Page layout (320x200 px):
//    - Header "AERIUS GOLD" + status dot
//    - Left card (180x130): speed arc 0-100%
//    - ON/OFF button (180x30) below the arc
//    - Top right card (118x90): BME280 temperature + humidity
//    - Bottom right card (118x66): timer +5min / -5min / RST
//
//  Hardware dependencies:
//    GPIO21 -> brushless PWM 25 kHz (LEDC ch.0, LOW speed mode)
//    GPIO15/16 -> I2C BME280 (address 0x76 or 0x77)
// =============================================================================

#include <Arduino.h>
#include <lvgl.h>

// Builds the page widgets inside the provided container
void ui_home_build(lv_obj_t* parent);

// Called in loop() — refreshes temperature and timer countdown
void ui_home_tick();

// Hardware init: fan PWM + BME280
void ui_home_init_hw();

// Visual update from MQTT (ON/OFF button + speed arc)
void ui_home_update_fan_visual();
uint32_t ui_home_get_rpm();   // Current fan RPM

// AUTO button — interface for ui_presence
void ui_home_set_auto_cb(lv_event_cb_t cb);
void ui_home_update_auto_style(bool active);

#endif // UI_HOME_H
