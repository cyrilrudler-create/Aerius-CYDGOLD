// =============================================================================
//  UI_PRESENCE.CPP — LD2410C presence detection
//  Library: ld2410 by ncmreynolds v0.2.2
//
//  At startup: configures range to 1.5 m to avoid false positives
//  In operation: reads presence + distance every 100 ms
// =============================================================================

#include "ui_presence.h"
#include "ui_shared.h"
#include "ui_home.h"
#include "ui_mqtt.h"
#include "Config.h"
#include <Arduino.h>
#include <ld2410.h>

// Pointer — initialised in ui_presence_init() to avoid
// any crash from a global constructor running before setup()
static ld2410* s_radar = nullptr;

// =============================================================================
//  GLOBAL STATE
// =============================================================================
bool     g_presence_detected = false;
uint16_t g_presence_distance = 0;
bool     g_auto_mode         = false;

// =============================================================================
//  PRIVATE VARIABLES
// =============================================================================
static uint32_t s_absence_start_ms  = 0;
static uint32_t s_presence_start_ms = 0;
static bool     s_was_present       = false;
static uint32_t s_last_read_ms      = 0;

// LVGL widgets
static lv_obj_t* s_lbl_presence = nullptr;

// Thresholds defined in Config.h:
// AUTO_TEMP_ON, AUTO_SPEED_MIN, AUTO_SPEED_MAX

// =============================================================================
//  AUTO MODE LOGIC
// =============================================================================
static void update_auto_mode() {
    if (!g_auto_mode) return;
    uint32_t now = millis();

    if (g_presence_detected) {
        s_absence_start_ms = 0;
        s_was_present = true;

        if (g_temperature > AUTO_TEMP_ON) {
            if (!g_fan_running) fan_start();
            float excess = g_temperature - AUTO_TEMP_ON;
            int spd = (int)(AUTO_SPEED_MIN + excess * 5.0f);
            spd = constrain(spd, AUTO_SPEED_MIN, AUTO_SPEED_MAX);
            if (spd != (int)g_fan_speed) {
                g_fan_speed = (uint8_t)spd;
                fan_set_speed(g_fan_speed);
                ui_home_update_fan_visual();
                mqtt_publish_fan_state();
            }
        } else {
            if (g_fan_running) {
                fan_stop();
                ui_home_update_fan_visual();
                mqtt_publish_fan_state();
            }
        }
    } else {
        if (s_was_present) {
            if (s_absence_start_ms == 0) s_absence_start_ms = now;
            uint32_t elapsed = (now - s_absence_start_ms) / 1000;
            if (elapsed >= LD2410_ABSENCE_DELAY && g_fan_running) {
                fan_stop();
                ui_home_update_fan_visual();
                mqtt_publish_fan_state();
                Serial.println("[PRESENCE] Extended absence -> fan stopped");
                s_was_present = false;
            }
        }
    }
}

// =============================================================================
//  AUTO BUTTON CALLBACK
// =============================================================================
static void update_auto_btn_style() {
    ui_home_update_auto_style(g_auto_mode);
}

static void cb_auto(lv_event_t* e) {
    g_auto_mode = !g_auto_mode;
    ui_home_update_auto_style(g_auto_mode);
    if (!g_auto_mode) { s_absence_start_ms = 0; s_was_present = false; }
    Serial.print("[PRESENCE] Auto mode: ");
    Serial.println(g_auto_mode ? "ON" : "OFF");
    mqtt_publish_fan_state();
}

// =============================================================================
//  BUILD
// =============================================================================
void ui_presence_build(lv_obj_t* parent_home) {
    // Eye icon — top right corner of the temperature card
    s_lbl_presence = lv_label_create(parent_home);
    lv_label_set_text(s_lbl_presence, LV_SYMBOL_EYE_CLOSE);
    lv_obj_set_style_text_color(s_lbl_presence, COL_GREY, 0);
    lv_obj_set_style_text_font(s_lbl_presence, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(s_lbl_presence, 292, 36);
    lv_obj_set_size(s_lbl_presence, 16, 16);

    // AUTO button is created in ui_home.cpp — we only register the callback here
    ui_home_set_auto_cb(cb_auto);
    ui_home_update_auto_style(g_auto_mode);
}

// =============================================================================
//  INIT
// =============================================================================
void ui_presence_init() {
    s_radar = new ld2410();
    // Start UART
    LD2410_UART.begin(LD2410_BAUDRATE, SERIAL_8N1,
                      (int)LD2410_RX_GPIO, (int)LD2410_TX_GPIO);
    delay(100);

    Serial.print("[LD2410C] Init RX=GPIO");
    Serial.print((int)LD2410_RX_GPIO);
    Serial.print(" TX=GPIO");
    Serial.print((int)LD2410_TX_GPIO);
    Serial.print(" @ ");
    Serial.println(LD2410_BAUDRATE);

    if (s_radar->begin(LD2410_UART, true)) {
        Serial.println("[LD2410C] Capteur detecte OK");

        // Configure max detection range: gate 1 = 0.75 m, gate 2 = 1.5 m, etc.
        // Each gate = 0.75 m. Gate 2 = 1.5 m, gate 3 = 2.25 m
        // Limiting to gate 2 (1.5 m) to avoid false positives
        // setMaxValues(moving_gate, static_gate, inactive_time)
        if (s_radar->setMaxValues(LD2410_MAX_GATE, LD2410_MAX_GATE, LD2410_ABSENCE_DELAY)) {
            Serial.print("[LD2410C] Range limited to ");
            Serial.print(LD2410_MAX_GATE * 75);
            Serial.println(" cm OK");
        } else {
            Serial.println("[LD2410C] Range config failed — factory values kept");
        }

        // Print current config
        Serial.print("[LD2410C] Max moving range: ");
        Serial.print(s_radar->movingTargetDistance());
        Serial.println(" cm");
        Serial.print("[LD2410C] Max static range: ");
        Serial.print(s_radar->stationaryTargetDistance());
        Serial.println(" cm");

    } else {
        Serial.println("[LD2410C] ERROR: sensor not detected!");
        Serial.println("[LD2410C] Check: sensor TX -> GPIO44, sensor RX -> GPIO43");
        Serial.println("[LD2410C] Check: VCC=5V, common GND");
    }


}

// =============================================================================
//  TICK
// =============================================================================
void ui_presence_tick() {
    uint32_t now = millis();
    if (now - s_last_read_ms < 100) return;
    s_last_read_ms = now;

    // Read via ld2410 library
    if (!s_radar) return;
    s_radar->read();

    bool prev = g_presence_detected;

    if (s_radar->isConnected()) {
        if (s_radar->presenceDetected()) {
            // Get distance
            if (s_radar->movingTargetDetected()) {
                g_presence_distance = s_radar->movingTargetDistance();
            } else if (s_radar->stationaryTargetDetected()) {
                g_presence_distance = s_radar->stationaryTargetDistance();
            }

            // Smart filter: only accept if
            // - moving OR stationary target confirmed (not just presenceDetected)
            // - distance > 20 cm (eliminates close-range reflections from the enclosure)
            bool valid = (s_radar->movingTargetDetected() ||
                          s_radar->stationaryTargetDetected()) &&
                         (g_presence_distance > 20);

            if (valid) {
                if (s_presence_start_ms == 0) s_presence_start_ms = now;
                if ((now - s_presence_start_ms) >= 1000)
                    g_presence_detected = true;
            } else {
                s_presence_start_ms = 0;
            }
            s_absence_start_ms = 0;

        } else {
            s_presence_start_ms = 0;
            g_presence_distance = 0;

            // Absence confirmation: stable for 2 s
            if (g_presence_detected) {
                if (s_absence_start_ms == 0) s_absence_start_ms = now;
                if ((now - s_absence_start_ms) >= 2000)
                    g_presence_detected = false;
            }
        }
    } else {
        // Sensor not connected -> no detection
        g_presence_detected = false;
        g_presence_distance = 0;
    }

    // Log only on state change
    if (g_presence_detected != prev) {
        Serial.print("[LD2410C] -> ");
        Serial.print(g_presence_detected ? "PRESENCE" : "ABSENCE");
        if (g_presence_distance > 0) {
            Serial.print(" @ ");
            Serial.print(g_presence_distance);
            Serial.print("cm");
        }
        Serial.println();
    }

    // Update icon
    if (s_lbl_presence) {
        if (g_presence_detected) {
            lv_label_set_text(s_lbl_presence, LV_SYMBOL_EYE_OPEN);
            lv_obj_set_style_text_color(s_lbl_presence, COL_GREEN, 0);
        } else {
            lv_label_set_text(s_lbl_presence, LV_SYMBOL_EYE_CLOSE);
            lv_obj_set_style_text_color(s_lbl_presence, COL_GREY, 0);
        }
    }

    update_auto_mode();
}
