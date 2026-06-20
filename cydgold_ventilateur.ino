// =============================================================================
//  CYDGOLD_VENTILATEUR.INO
//  WiFi: auto-connect at startup using WIFI_SSID / WIFI_PASS from Config.h
//  Pages: 0=Dashboard  1=LEDs  2=Info/Credits
// =============================================================================

#include <Arduino.h>
#include <lvgl.h>
#include <WiFi.h>

#include "cyd_lvgl_drv.h"
#include "cydgold.h"
#include "Config.h"

#include "ui_shared.h"
#include "ui_home.h"
#include "ui_leds.h"
#include "ui_info.h"
#include "ui_mqtt.h"
#include "ui_presence.h"


static lv_obj_t* make_page(lv_obj_t* parent) {
    lv_obj_t* p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_style_bg_color(p, lv_color_hex(0x0D0D10), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}


void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("=== CYD Gold — ARGB PWM Fan Controller ===");

    // ── 1. CYD Gold hardware ──────────────────────────────────────────────────
    init_cyd_hardware();
    init_cyd_lvgl();
   
    // ── 2. WiFi — automatic connection ───────────────────────────────────────
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("[WiFi] Connecting to %s...\n", WIFI_SSID);

    // Wait for WiFi up to 10 s — non-blocking if network is absent
    uint32_t wifi_t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifi_t < 10000) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (WiFi.status() == WL_CONNECTED)
        Serial.printf("[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());
    else
        Serial.println("[WiFi] Not connected — continuing without WiFi");

    // ── 3. MQTT ───────────────────────────────────────────────────────────────
    mqtt_init();

    // ── 4. LVGL ───────────────────────────────────────────────────────────────
    ui_shared_init_styles();

    lv_obj_t* scr = lv_scr_act();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0D0D10), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── 5. Pages ──────────────────────────────────────────────────────────────
    for (int i = 0; i < 3; i++) {
        g_pages[i] = make_page(scr);
        lv_obj_set_pos(g_pages[i], 0, 0);
        lv_obj_set_size(g_pages[i], 320, 200);
        lv_obj_add_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
    }

    ui_home_build(g_pages[0]);
    ui_presence_build(g_pages[0]);  // adds presence widgets on home page
    ui_leds_build(g_pages[1]);
    ui_info_build(g_pages[2]);

    // ── 6. Navbar ─────────────────────────────────────────────────────────────
    build_navbar(scr);

    // ── 7. Fan + LEDs + BME280 + LD2410C hardware init ────────────────────────
    ui_home_init_hw();
    ui_leds_init_hw();
    ui_presence_init();  // LD2410C — after all other hardware inits

    // ── 8. Show home page ─────────────────────────────────────────────────────
    ui_nav_update(0);

    Serial.println("=== System ready ===");
}


void loop() {
    ui_home_tick();
    ui_leds_tick();
    ui_info_tick();
    ui_presence_tick();  // LD2410C: frame parsing + auto mode
    mqtt_tick();         // MQTT: reconnect + incoming messages + sensor publish

    lv_timer_handler();
    loop_cyd_audio();

    vTaskDelay(pdMS_TO_TICKS(5));
}
