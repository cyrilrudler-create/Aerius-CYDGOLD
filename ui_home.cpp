// =============================================================================
//  UI_HOME.CPP — Dashboard page: speed arc, ON/OFF, BME280 temperature, timer
// =============================================================================

#include "ui_home.h"
#include "ui_mqtt.h"
#include "ui_shared.h"
#include "Config.h"
#include <Adafruit_BME280.h>
#include <Wire.h>
#include <driver/ledc.h>
#include <WiFi.h>
#include <time.h>

// ── Local LVGL objects ────────────────────────────────────────────────────────
static lv_obj_t* s_arc_speed     = nullptr;
static lv_obj_t* s_lbl_speed_val = nullptr;
static lv_obj_t* s_lbl_speed_pct = nullptr;
static lv_obj_t* s_btn_onoff     = nullptr;
static lv_obj_t* s_lbl_onoff     = nullptr;
static lv_obj_t* s_lbl_temp      = nullptr;
static lv_obj_t* s_lbl_humi      = nullptr;
static lv_obj_t* s_lbl_timer     = nullptr;
static lv_obj_t* s_lbl_time      = nullptr;   // HH:MM clock
static lv_obj_t* s_lbl_wifi_icon = nullptr;   // coloured WiFi dot
static lv_obj_t* s_btn_auto       = nullptr;   // AUTO mode button
static lv_obj_t* s_lbl_auto       = nullptr;

// (Arc styles s_sty_arc_... are defined in ui_shared.h)

// ── Local hardware ────────────────────────────────────────────────────────────
static Adafruit_BME280 s_bme;
static bool            s_bme_ok      = false;
static uint32_t        s_last_bme_ms = 0;
static bool            s_ntp_synced  = false;
static uint32_t        s_last_ntp_ms = 0;

// ── Tachometer RPM ────────────────────────────────────────────────────────────
static volatile uint32_t s_tachy_count  = 0;
static uint32_t          s_last_rpm_ms  = 0;
static uint32_t          s_rpm          = 0;
static lv_obj_t*         s_lbl_rpm      = nullptr;

// Pulses per revolution depends on the fan model:
// Arctic / standard = 2 pulses/rev
// Some models       = 4 pulses/rev
// Adjust if RPM reads 2x too high -> change to 4
#define TACHY_PULSES_PER_REV  2

static void IRAM_ATTR isr_tachy() {
    // Hardware debounce: ignore pulses < 1 ms apart
    static uint32_t last_us = 0;
    uint32_t now_us = micros();
    if (now_us - last_us > 1000) {  // 1 ms minimum between pulses
        s_tachy_count++;
        last_us = now_us;
    }
}

// =============================================================================
//  CALLBACKS
// =============================================================================

static void cb_onoff(lv_event_t* e) {
    if (g_fan_running) {
        fan_stop();
        lv_label_set_text(s_lbl_onoff, "OFF");
        lv_obj_set_style_bg_color(s_btn_onoff, COL_SURFACE2, 0);
        lv_obj_set_style_border_width(s_btn_onoff, 1, 0);
        lv_obj_set_style_text_color(s_lbl_onoff, COL_GREY_LIGHT, 0);
    } else {
        fan_start();
        lv_label_set_text(s_lbl_onoff, "ON");
        lv_obj_set_style_bg_color(s_btn_onoff, COL_GOLD, 0);
        lv_obj_set_style_border_width(s_btn_onoff, 0, 0);
        lv_obj_set_style_text_color(s_lbl_onoff, COL_BG, 0);
        if (g_timer_secs > 0 && g_timer_start == 0)
            g_timer_start = millis();
    }
    mqtt_publish_fan_state();
}

static void cb_arc(lv_event_t* e) {
    lv_obj_t* target = lv_event_get_target(e);
    int val = lv_arc_get_value(target);
    g_fan_speed = (uint8_t)val;
    if (g_fan_running) fan_set_speed(g_fan_speed);

    // ── Dynamic blue gradient (0% to 100%) ───────────────────────────────────
    lv_color_t dynamic_color;
    
    if (val < 50) {
        // First half: transition from deep blue to mid blue
        uint8_t ratio = (val * 510) / 100; 
        dynamic_color = lv_color_mix(COL_BLUE_MED, COL_BLUE_DARK, ratio);
    } else {
        // Second half: transition from mid blue to cyan
        uint8_t ratio = ((val - 50) * 510) / 100;
        dynamic_color = lv_color_mix(COL_BLUE_LIGHT, COL_BLUE_MED, ratio);
    }

    // Apply the computed colour to the arc indicator and knob
    lv_obj_set_style_arc_color(target, dynamic_color, LV_PART_INDICATOR);
    lv_obj_set_style_border_color(target, dynamic_color, LV_PART_KNOB);
    lv_obj_set_style_shadow_color(target, dynamic_color, LV_PART_KNOB);

    // Update speed label
    char buf[6];
    snprintf(buf, sizeof(buf), "%d", val);
    lv_label_set_text(s_lbl_speed_val, buf);
    mqtt_publish_fan_state();
}

static void cb_timer_inc(lv_event_t* e) {
    g_timer_secs += 300;
    if (g_timer_secs > 7200) g_timer_secs = 7200;
    if (g_fan_running && g_timer_start == 0)
        g_timer_start = millis();
    mqtt_publish_fan_state();
}

static void cb_timer_dec(lv_event_t* e) {
    if (g_timer_secs >= 300) g_timer_secs -= 300;
    else g_timer_secs = 0;
    mqtt_publish_fan_state();
}

static void cb_timer_rst(lv_event_t* e) {
    g_timer_secs  = 0;
    g_timer_start = 0;
    lv_label_set_text(s_lbl_timer, "--:--");
    mqtt_publish_fan_state();
}

// =============================================================================
//  BUILD
// =============================================================================

void ui_home_build(lv_obj_t* parent) {

    // ── Arc styles init (once only) ──────────────────────────────────────────
    if (!s_styles_initialized) {
        // 1. Arc background (smoked glass rail)
        lv_style_init(&s_sty_arc_bg_futur);
        lv_style_set_arc_color(&s_sty_arc_bg_futur, COL_DARK_GLASS);
        lv_style_set_arc_width(&s_sty_arc_bg_futur, 11); 
        lv_style_set_arc_rounded(&s_sty_arc_bg_futur, true);

        // 2. Arc indicator (the glowing progress gauge)
        lv_style_init(&s_sty_arc_ind_futur);
        lv_style_set_arc_width(&s_sty_arc_ind_futur, 11); 
        lv_style_set_arc_rounded(&s_sty_arc_ind_futur, true);

        // 3. Arc knob ("energy core" style)
        lv_style_init(&s_sty_arc_knob_futur);
        lv_style_set_bg_color(&s_sty_arc_knob_futur, COL_WHITE); 
        lv_style_set_bg_opa(&s_sty_arc_knob_futur, LV_OPA_COVER);
        lv_style_set_border_width(&s_sty_arc_knob_futur, 2);
        lv_style_set_radius(&s_sty_arc_knob_futur, LV_RADIUS_CIRCLE);
        lv_style_set_width(&s_sty_arc_knob_futur, 15); 
        lv_style_set_height(&s_sty_arc_knob_futur, 15);
        lv_style_set_shadow_width(&s_sty_arc_knob_futur, 8);
        lv_style_set_shadow_spread(&s_sty_arc_knob_futur, 1);

        s_styles_initialized = true;
    }

    // ── Header: title | WiFi icon | clock | status dot ───────────────────────
    // All at y=8, same font montserrat_14 -> perfect alignment

    // Title "AERIUS GOLD"
    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "AERIUS GOLD");
    lv_obj_set_style_text_color(title, COL_GOLD, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_obj_set_pos(title, 12, 8);

    // HH:MM clock — right side, before WiFi symbol
    s_lbl_time = lv_label_create(parent);
    lv_label_set_text(s_lbl_time, "--:--");
    lv_obj_set_style_text_color(s_lbl_time, COL_GREY, 0);
    lv_obj_set_style_text_font(s_lbl_time, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_lbl_time, 238, 8);
    lv_obj_set_size(s_lbl_time, 50, 18);
    lv_obj_set_style_text_align(s_lbl_time, LV_TEXT_ALIGN_RIGHT, 0);

    // WiFi icon — far right, colour updated dynamically in tick()
    // grey = not connected, green = connected, red = error
    s_lbl_wifi_icon = lv_label_create(parent);
    lv_label_set_text(s_lbl_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_lbl_wifi_icon, COL_GREY, 0);
    lv_obj_set_style_text_font(s_lbl_wifi_icon, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_lbl_wifi_icon, 296, 8);
    lv_obj_set_size(s_lbl_wifi_icon, 20, 18);

    // Separator
    lv_obj_t* sep = lv_obj_create(parent);
    lv_obj_remove_style_all(sep);
    lv_obj_set_pos(sep, 8, 28);
    lv_obj_set_size(sep, 304, 1);
    lv_obj_set_style_bg_color(sep, COL_GOLD_DIM, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_40, 0);

    // ── Speed arc card ────────────────────────────────────────────────────────
    lv_obj_t* card_arc = lv_obj_create(parent);
    lv_obj_remove_style_all(card_arc);
    lv_obj_set_pos(card_arc, 8, 32);
    lv_obj_set_size(card_arc, 180, 166);
    lv_obj_add_style(card_arc, &g_sty_card, 0);
    lv_obj_set_style_pad_all(card_arc, 0, 0);
    lv_obj_clear_flag(card_arc, LV_OBJ_FLAG_SCROLLABLE);

    // "SPEED" label
    lv_obj_t* lbl_vit = lv_label_create(card_arc);
    lv_label_set_text(lbl_vit, "SPEED");
    lv_obj_set_style_text_color(lbl_vit, COL_GREY_LIGHT, 0);
    lv_obj_set_style_text_font(lbl_vit, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_letter_space(lbl_vit, 3, 0);
    lv_obj_set_pos(lbl_vit, 8, 6);
    lv_obj_set_size(lbl_vit, 80, 14);
    lv_obj_set_style_text_align(lbl_vit, LV_TEXT_ALIGN_LEFT, 0);

    // Speed value
    s_lbl_speed_val = lv_label_create(card_arc);
    lv_label_set_text(s_lbl_speed_val, "0");
    lv_obj_set_style_text_color(s_lbl_speed_val, COL_WHITE, 0);
    lv_obj_set_style_text_font(s_lbl_speed_val, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(s_lbl_speed_val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_lbl_speed_val, 37, 54);
    lv_obj_set_size(s_lbl_speed_val, 106, 30);

    // % sign
    s_lbl_speed_pct = lv_label_create(card_arc);
    lv_label_set_text(s_lbl_speed_pct, "%");
    lv_obj_set_style_text_color(s_lbl_speed_pct, COL_GREY, 0);
    lv_obj_set_style_text_font(s_lbl_speed_pct, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(s_lbl_speed_pct, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_lbl_speed_pct, 37, 84);
    lv_obj_set_size(s_lbl_speed_pct, 106, 16);

    // RPM label — below %, above ON/OFF button
    s_lbl_rpm = lv_label_create(card_arc);
    lv_label_set_text(s_lbl_rpm, "-- RPM");
    lv_obj_set_style_text_color(s_lbl_rpm, COL_GREY, 0);
    lv_obj_set_style_text_font(s_lbl_rpm, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(s_lbl_rpm, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_lbl_rpm, 0, 115);
    lv_obj_set_size(s_lbl_rpm, 180, 16);

    // Arc — cyberpunk/futuristic style
    s_arc_speed = lv_arc_create(card_arc);
    lv_obj_set_size(s_arc_speed, 112, 112);
    lv_obj_set_pos(s_arc_speed, 34, 10); 
    lv_arc_set_rotation(s_arc_speed, 135);
    lv_arc_set_bg_angles(s_arc_speed, 0, 270);
    lv_arc_set_range(s_arc_speed, 0, 100);
    lv_arc_set_value(s_arc_speed, g_fan_speed);
    
    lv_obj_add_style(s_arc_speed, &s_sty_arc_bg_futur,  LV_PART_MAIN);
    lv_obj_add_style(s_arc_speed, &s_sty_arc_ind_futur, LV_PART_INDICATOR);
    lv_obj_add_style(s_arc_speed, &s_sty_arc_knob_futur, LV_PART_KNOB);
    
    lv_obj_add_event_cb(s_arc_speed, cb_arc, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_event_send(s_arc_speed, LV_EVENT_VALUE_CHANGED, nullptr);

    // ── ON/OFF button ─────────────────────────────────────────────────────────
    s_btn_onoff = lv_btn_create(card_arc);
    lv_obj_remove_style_all(s_btn_onoff);
    lv_obj_set_pos(s_btn_onoff, 8, 133);
    lv_obj_set_size(s_btn_onoff, 164, 28);
    lv_obj_set_style_bg_color(s_btn_onoff, COL_SURFACE2, 0);
    lv_obj_set_style_bg_opa(s_btn_onoff, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_btn_onoff, COL_GREY, 0);
    lv_obj_set_style_border_width(s_btn_onoff, 1, 0);
    lv_obj_set_style_border_opa(s_btn_onoff, LV_OPA_40, 0);
    lv_obj_set_style_radius(s_btn_onoff, 8, 0);
    lv_obj_set_style_shadow_width(s_btn_onoff, 0, 0);
    lv_obj_add_style(s_btn_onoff, &g_sty_btn_dark_pr, LV_STATE_PRESSED);

    s_lbl_onoff = lv_label_create(s_btn_onoff);
    lv_label_set_text(s_lbl_onoff, "OFF");
    lv_obj_set_style_text_color(s_lbl_onoff, COL_GREY_LIGHT, 0);
    lv_obj_set_style_text_font(s_lbl_onoff, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_lbl_onoff, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_lbl_onoff, 0, 7);
    lv_obj_set_size(s_lbl_onoff, 164, 14);
    lv_obj_add_event_cb(s_btn_onoff, cb_onoff, LV_EVENT_CLICKED, nullptr);

    // ── Temperature card ──────────────────────────────────────────────────────
    lv_obj_t* card_temp = lv_obj_create(parent);
    lv_obj_remove_style_all(card_temp);
    lv_obj_set_pos(card_temp, 194, 32);
    lv_obj_set_size(card_temp, 118, 80);
    lv_obj_add_style(card_temp, &g_sty_card, 0);
    lv_obj_set_style_pad_all(card_temp, 0, 0);
    lv_obj_clear_flag(card_temp, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_t_title = lv_label_create(card_temp);
    lv_label_set_text(lbl_t_title, "TEMPERATURE");
    lv_obj_set_style_text_color(lbl_t_title, COL_GREY_LIGHT, 0);
    lv_obj_set_style_text_font(lbl_t_title, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lbl_t_title, 8, 6);
    lv_obj_set_size(lbl_t_title, 110, 14);
    lv_obj_set_style_text_align(lbl_t_title, LV_TEXT_ALIGN_LEFT, 0);

    s_lbl_temp = lv_label_create(card_temp);
    lv_label_set_text(s_lbl_temp, "--.-- C");
    lv_obj_set_style_text_color(s_lbl_temp, COL_WHITE, 0);
    lv_obj_set_style_text_font(s_lbl_temp, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(s_lbl_temp, 0, 30);
    lv_obj_set_size(s_lbl_temp, 118, 26);
    lv_obj_set_style_text_align(s_lbl_temp, LV_TEXT_ALIGN_CENTER, 0);

    // Humidity label not used — not needed for fan control
    s_lbl_humi = nullptr;

    // ── AUTO button — aligned with timer buttons
    s_btn_auto = lv_btn_create(parent);
    lv_obj_remove_style_all(s_btn_auto);
    lv_obj_set_pos(s_btn_auto, 199, 90);
    lv_obj_set_size(s_btn_auto, 112, 18);
    lv_obj_set_style_bg_color(s_btn_auto, COL_SURFACE2, 0);
    lv_obj_set_style_bg_opa(s_btn_auto, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_btn_auto, COL_GREY, 0);
    lv_obj_set_style_border_width(s_btn_auto, 1, 0);
    lv_obj_set_style_border_opa(s_btn_auto, LV_OPA_40, 0);
    lv_obj_set_style_radius(s_btn_auto, 6, 0);
    lv_obj_set_style_shadow_width(s_btn_auto, 0, 0);
    s_lbl_auto = lv_label_create(s_btn_auto);
    lv_label_set_text(s_lbl_auto, "AUTO");
    lv_obj_set_style_text_font(s_lbl_auto, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_auto, COL_GREY_LIGHT, 0);
    lv_obj_set_style_text_align(s_lbl_auto, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_lbl_auto, 0, 3);
    lv_obj_set_size(s_lbl_auto, 112, 14);
    // Callback is registered by ui_presence via ui_home_set_auto_cb()

    // ── Timer card ────────────────────────────────────────────────────────────
    lv_obj_t* card_timer = lv_obj_create(parent);
    lv_obj_remove_style_all(card_timer);
    lv_obj_set_pos(card_timer, 194, 116);
    lv_obj_set_size(card_timer, 118, 80);
    lv_obj_add_style(card_timer, &g_sty_card, 0);
    lv_obj_set_style_pad_all(card_timer, 0, 0);
    lv_obj_clear_flag(card_timer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_t_timer = lv_label_create(card_timer);
    lv_label_set_text(lbl_t_timer, "TIMER");
    lv_obj_set_style_text_color(lbl_t_timer, COL_GREY_LIGHT, 0);
    lv_obj_set_style_text_font(lbl_t_timer, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lbl_t_timer, 0, 5);
    lv_obj_set_size(lbl_t_timer, 118, 14);
    lv_obj_set_style_text_align(lbl_t_timer, LV_TEXT_ALIGN_CENTER, 0);

    s_lbl_timer = lv_label_create(card_timer);
    lv_label_set_text(s_lbl_timer, "--:--");
    lv_obj_set_style_text_color(s_lbl_timer, COL_WHITE, 0);
    lv_obj_set_style_text_font(s_lbl_timer, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(s_lbl_timer, 0, 22);
    lv_obj_set_size(s_lbl_timer, 118, 24);
    lv_obj_set_style_text_align(s_lbl_timer, LV_TEXT_ALIGN_CENTER, 0);

    // Three uniform buttons at the bottom — same style as ON/OFF button
    // 3 x 34px + 2 x 5px gap = 112px -> margin = (118-112)/2 = 3px
    const lv_coord_t bw = 34, bh = 22, bgap = 5, by = 52;

    // Decrease button
    lv_obj_t* btn_dec = lv_btn_create(card_timer);
    lv_obj_remove_style_all(btn_dec);
    lv_obj_set_pos(btn_dec, 3, by);
    lv_obj_set_size(btn_dec, bw, bh);
    lv_obj_set_style_bg_color(btn_dec, COL_SURFACE2, 0);
    lv_obj_set_style_bg_opa(btn_dec, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn_dec, COL_GREY, 0);
    lv_obj_set_style_border_width(btn_dec, 1, 0);
    lv_obj_set_style_border_opa(btn_dec, LV_OPA_40, 0);
    lv_obj_set_style_radius(btn_dec, 6, 0);
    lv_obj_set_style_shadow_width(btn_dec, 0, 0);
    lv_obj_add_style(btn_dec, &g_sty_btn_dark_pr, LV_STATE_PRESSED);
    lv_obj_t* l_dec = lv_label_create(btn_dec);
    lv_label_set_text(l_dec, "-");
    lv_obj_set_style_text_color(l_dec, COL_GOLD, 0);
    lv_obj_set_style_text_font(l_dec, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(l_dec, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l_dec, 0, 4);
    lv_obj_set_size(l_dec, bw, 14);
    lv_obj_add_event_cb(btn_dec, cb_timer_dec, LV_EVENT_CLICKED, nullptr);

    // Reset button
    lv_obj_t* btn_rst = lv_btn_create(card_timer);
    lv_obj_remove_style_all(btn_rst);
    lv_obj_set_pos(btn_rst, 3 + bw + bgap, by);
    lv_obj_set_size(btn_rst, bw, bh);
    lv_obj_set_style_bg_color(btn_rst, COL_SURFACE2, 0);
    lv_obj_set_style_bg_opa(btn_rst, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn_rst, COL_GREY, 0);
    lv_obj_set_style_border_width(btn_rst, 1, 0);
    lv_obj_set_style_border_opa(btn_rst, LV_OPA_40, 0);
    lv_obj_set_style_radius(btn_rst, 6, 0);
    lv_obj_set_style_shadow_width(btn_rst, 0, 0);
    lv_obj_add_style(btn_rst, &g_sty_btn_dark_pr, LV_STATE_PRESSED);
    lv_obj_t* l_rst = lv_label_create(btn_rst);
    lv_label_set_text(l_rst, "RST");
    lv_obj_set_style_text_color(l_rst, COL_GREY_LIGHT, 0);
    lv_obj_set_style_text_font(l_rst, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_align(l_rst, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l_rst, 0, 6);
    lv_obj_set_size(l_rst, bw, 12);
    lv_obj_add_event_cb(btn_rst, cb_timer_rst, LV_EVENT_CLICKED, nullptr);

    // Increase button
    lv_obj_t* btn_inc = lv_btn_create(card_timer);
    lv_obj_remove_style_all(btn_inc);
    lv_obj_set_pos(btn_inc, 3 + 2*(bw + bgap), by);
    lv_obj_set_size(btn_inc, bw, bh);
    lv_obj_set_style_bg_color(btn_inc, COL_SURFACE2, 0);
    lv_obj_set_style_bg_opa(btn_inc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn_inc, COL_GREY, 0);
    lv_obj_set_style_border_width(btn_inc, 1, 0);
    lv_obj_set_style_border_opa(btn_inc, LV_OPA_40, 0);
    lv_obj_set_style_radius(btn_inc, 6, 0);
    lv_obj_set_style_shadow_width(btn_inc, 0, 0);
    lv_obj_add_style(btn_inc, &g_sty_btn_dark_pr, LV_STATE_PRESSED);
    lv_obj_t* l_inc = lv_label_create(btn_inc);
    lv_label_set_text(l_inc, "+");
    lv_obj_set_style_text_color(l_inc, COL_GOLD, 0);
    lv_obj_set_style_text_font(l_inc, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(l_inc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l_inc, 0, 4);
    lv_obj_set_size(l_inc, bw, 14);
    lv_obj_add_event_cb(btn_inc, cb_timer_inc, LV_EVENT_CLICKED, nullptr);
}

// =============================================================================
//  HARDWARE INIT
// =============================================================================

void ui_home_init_hw() {
    // ── Fan PWM ───────────────────────────────────────────────────────────────
    ledc_timer_config_t tc = {};
    tc.speed_mode      = LEDC_LOW_SPEED_MODE;
    tc.timer_num       = LEDC_TIMER_0;
    tc.duty_resolution = FAN_PWM_RESOLUTION;
    tc.freq_hz         = FAN_PWM_FREQ;
    tc.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&tc);

    ledc_channel_config_t cc = {};
    cc.gpio_num   = FAN_PWM_GPIO;
    cc.speed_mode = LEDC_LOW_SPEED_MODE;
    cc.channel    = FAN_PWM_CHANNEL;
    cc.timer_sel  = LEDC_TIMER_0;
    cc.duty       = 0;
    cc.hpoint     = 0;
    ledc_channel_config(&cc);
    Serial.println("[HOME] Fan PWM 25 kHz on GPIO21");

    // ── Tachometer RPM ────────────────────────────────────────────────────────
    pinMode((int)FAN_TACHY_GPIO, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt((int)FAN_TACHY_GPIO),
                    isr_tachy, FALLING);
    Serial.println("[HOME] Tachometer RPM on GPIO14");

    // ── NTP ───────────────────────────────────────────────────────────────────
    // configTime only registers the config — sync happens in the background
    // once WiFi is available. Non-blocking.
    configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
    Serial.printf("[NTP] Server: %s (GMT+%dh)\n", NTP_SERVER, GMT_OFFSET/3600);

    // ── BME280 ────────────────────────────────────────────────────────────────
    Wire.begin((int)I2C_SDA, (int)I2C_SCL);
    if (s_bme.begin(BME280_ADDR, &Wire)) {
        s_bme_ok = true;
        s_bme.setSampling(
            Adafruit_BME280::MODE_NORMAL,
            Adafruit_BME280::SAMPLING_X2,
            Adafruit_BME280::SAMPLING_X2,
            Adafruit_BME280::SAMPLING_X2,
            Adafruit_BME280::FILTER_X4,
            Adafruit_BME280::STANDBY_MS_500);
        g_temperature = s_bme.readTemperature();
        g_humidity    = s_bme.readHumidity();
        Serial.printf("[HOME] BME280 OK — %.1f°C / %.0f%% RH\n",
                      g_temperature, g_humidity);
    } else {
        Serial.println("[HOME] BME280 not detected (check address 0x76/0x77)");
    }
}

// =============================================================================
//  TICK
// =============================================================================

void ui_home_tick() {
    uint32_t now = millis();

    // ── RPM calculation (every 500 ms) ────────────────────────────────────────
    if (now - s_last_rpm_ms >= 500) {
        // Disable interrupt while reading the counter
        noInterrupts();
        uint32_t count = s_tachy_count;
        s_tachy_count  = 0;
        interrupts();

        // Formula: RPM = (count / PULSES_PER_REV) / (window_ms / 60000)
        // Window 500 ms -> RPM = count * 60000 / (TACHY_PULSES_PER_REV * 500)
        s_rpm = (count * 60000UL) / (TACHY_PULSES_PER_REV * 500UL);
        s_last_rpm_ms = now;

        if (s_lbl_rpm && g_current_page == 0) {
            if (!g_fan_running || s_rpm == 0) {
                lv_label_set_text(s_lbl_rpm, "-- RPM");
                lv_obj_set_style_text_color(s_lbl_rpm, COL_GREY, 0);
            } else {
                char rbuf[16];
                snprintf(rbuf, sizeof(rbuf), "%lu RPM", s_rpm);
                lv_label_set_text(s_lbl_rpm, rbuf);
                // Colour by RPM: green if normal, gold if high, red if very high
                lv_color_t rc = (s_rpm < 1500) ? COL_GREEN :
                                (s_rpm < 2500) ? COL_GOLD  : COL_RED;
                lv_obj_set_style_text_color(s_lbl_rpm, rc, 0);
            }
        }
    }

    // ── WiFi status + NTP clock ───────────────────────────────────────────────
    bool wifi_ok = (WiFi.status() == WL_CONNECTED);

    // WiFi icon colour reflects connection state
    if (s_lbl_wifi_icon) {
        lv_obj_set_style_text_color(s_lbl_wifi_icon,
            wifi_ok ? COL_GREEN : COL_RED, 0);
    }

    // NTP clock: update if connected and synchronised
    if (wifi_ok && s_lbl_time) {
        struct tm ti;
        if (getLocalTime(&ti)) {
            s_ntp_synced = true;
            char tbuf[6];
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d", ti.tm_hour, ti.tm_min);
            lv_label_set_text(s_lbl_time, tbuf);
            lv_obj_set_style_text_color(s_lbl_time, COL_WHITE, 0);
        }
    } else if (!wifi_ok && s_lbl_time) {
        lv_label_set_text(s_lbl_time, "--:--");
        lv_obj_set_style_text_color(s_lbl_time, COL_GREY, 0);
        s_ntp_synced = false;
    }

    // ── BME280 refresh every 3 seconds ───────────────────────────────────────
    if (s_bme_ok && (now - s_last_bme_ms > 3000)) {
        s_last_bme_ms = now;
        g_temperature = s_bme.readTemperature();
        g_humidity    = s_bme.readHumidity();

        if (s_lbl_temp && g_current_page == 0) {
            char tb[8];
            snprintf(tb, sizeof(tb), "%.1f C", g_temperature);
            lv_label_set_text(s_lbl_temp, tb);
            lv_color_t tc = (g_temperature < 28.0f) ? COL_WHITE :
                            (g_temperature < 35.0f) ? COL_GOLD  : COL_RED;
            lv_obj_set_style_text_color(s_lbl_temp, tc, 0);
        }
        if (s_lbl_humi && g_current_page == 0) {
            char hb[10];
            snprintf(hb, sizeof(hb), "%.0f%% RH", g_humidity);
            lv_label_set_text(s_lbl_humi, hb);
        }
    }

    // ── Timer countdown ───────────────────────────────────────────────────────
    if (g_timer_secs > 0 && g_fan_running && g_timer_start > 0) {
        uint32_t elapsed = (now - g_timer_start) / 1000;

        if (elapsed >= g_timer_secs) {
            fan_stop();
            g_timer_secs  = 0;
            g_timer_start = 0;
            if (s_lbl_timer) lv_label_set_text(s_lbl_timer, "--:--");
            if (s_lbl_onoff) {
                lv_label_set_text(s_lbl_onoff, "OFF");
                lv_obj_set_style_bg_color(s_btn_onoff, COL_SURFACE2, 0);
                lv_obj_set_style_border_width(s_btn_onoff, 1, 0);
                lv_obj_set_style_text_color(s_lbl_onoff, COL_GREY_LIGHT, 0);
            }
    
        } else {
            uint32_t rem = g_timer_secs - elapsed;
            if (s_lbl_timer) {
                char tb[8];
                snprintf(tb, sizeof(tb), "%02lu:%02lu", rem / 60, rem % 60);
                lv_label_set_text(s_lbl_timer, tb);
            }
        }
    } else if (g_timer_secs > 0 && !g_fan_running && s_lbl_timer) {
        char tb[8];
        snprintf(tb, sizeof(tb), "%02lu:%02lu",
                 g_timer_secs / 60, g_timer_secs % 60);
        lv_label_set_text(s_lbl_timer, tb);
    }
}

// =============================================================================
//  VISUAL UPDATE FROM MQTT
//  Called by ui_mqtt.cpp when HA sends a command
// =============================================================================

void ui_home_update_fan_visual() {
    // Update ON/OFF button
    if (s_btn_onoff && s_lbl_onoff) {
        if (g_fan_running) {
            lv_label_set_text(s_lbl_onoff, "ON");
            lv_obj_set_style_bg_color(s_btn_onoff, COL_GOLD, 0);
            lv_obj_set_style_border_width(s_btn_onoff, 0, 0);
            lv_obj_set_style_text_color(s_lbl_onoff, COL_BG, 0);
        } else {
            lv_label_set_text(s_lbl_onoff, "OFF");
            lv_obj_set_style_bg_color(s_btn_onoff, COL_SURFACE2, 0);
            lv_obj_set_style_border_width(s_btn_onoff, 1, 0);
            lv_obj_set_style_text_color(s_lbl_onoff, COL_GREY_LIGHT, 0);
        }
    }

    // Update speed arc + value label
    if (s_arc_speed && s_lbl_speed_val) {
        lv_arc_set_value(s_arc_speed, g_fan_speed);
        // Trigger cb_arc to update colour + label
        lv_event_send(s_arc_speed, LV_EVENT_VALUE_CHANGED, nullptr);
    }
}

uint32_t ui_home_get_rpm() {
    return s_rpm;
}

// =============================================================================
//  AUTO BUTTON — public interface for ui_presence
// =============================================================================

void ui_home_set_auto_cb(lv_event_cb_t cb) {
    if (s_btn_auto) lv_obj_add_event_cb(s_btn_auto, cb, LV_EVENT_CLICKED, nullptr);
}

void ui_home_update_auto_style(bool active) {
    if (!s_btn_auto || !s_lbl_auto) return;
    if (active) {
        lv_obj_set_style_bg_color(s_btn_auto, COL_GOLD, 0);
        lv_obj_set_style_text_color(s_lbl_auto, COL_BG, 0);
    } else {
        lv_obj_set_style_bg_color(s_btn_auto, COL_SURFACE2, 0);
        lv_obj_set_style_text_color(s_lbl_auto, COL_GREY_LIGHT, 0);
    }
}
