// =============================================================================
//  UI_INFO.CPP — Information / Credits page
//
//  Layout:
//    Left  : 100x100 logo + project name
//    Right : auto-scrolling credits (lv_timer every 50 ms)
//            with live system info: RAM, PSRAM, CPU temperature
// =============================================================================

#include "ui_info.h"
#include "ui_shared.h"
#include <Arduino.h>
#include "esp_system.h"

// Logo embedded in flash
LV_IMG_DECLARE(CRTeckmini);

// =============================================================================
//  CREDITS — static content + live lines
// =============================================================================

// Static credit lines (in display order)
static const char* CREDITS_LINES[] = {
    "----------------------------------",
    "AERIUS GOLD  v1.0",
    "----------------------------------",
    "",
    "HARDWARE",
    "ESP32-S3 240MHz",
    "8MB PSRAM  16MB Flash",
    "ILI9341 2.8\" IPS 320x240",
    "FT6336G Capacitive Touch",
    "ES8311 Audio Codec",
    "BME280 Temp/Hum/Press",
    "WS2812B ARGB LEDs",
    "Brushless Fan PWM 25kHz",
    "LD2410C Presence Sensor",
    "Tachy RPM GPIO14",
    "",
    "LIBRARIES",
    "LVGL  v8.3.x",
    "ESP32 Arduino  v3.3.x",
    "TFT_eSPI",
    "ESP32-audioI2S",
    "Adafruit BME280  v2.3.x",
    "Adafruit NeoPixel  v1.15.x",
    "FT6336",
    "PubSubClient  v2.8.x",
    "ld2410  v0.2.2",
    "",
    "SYSTEM",
    // Les 4 lignes suivantes sont générées dynamiquement dans le timer
    ">> LIVE DATA <<",
    "",
    "",
    "",
    "",
    "",
    "----------------------------------",
    "",
    "Designed & Built by",
    "CRTeck - 2026",
    "",
    "----------------------------------",
    "",
    "",
    "",
};

#define NB_CREDITS  (sizeof(CREDITS_LINES) / sizeof(CREDITS_LINES[0]))
#define VISIBLE_LINES  7      // lines visible simultaneously in the area
#define LINE_H        18      // line height in px
#define SCROLL_SPEED  1       // px per tick (50 ms) -> ~20 px/s

// Credits area: x=120, y=36, w=192, h=VISIBLE_LINES*LINE_H = 126
#define CREDITS_X   120
#define CREDITS_Y   36
#define CREDITS_W   192
#define CREDITS_H   (VISIBLE_LINES * LINE_H)

// Scroll state
static int32_t      s_scroll_y     = 0;    // position in px (increases from 0)
static uint32_t     s_total_height = 0;    // total content height
static lv_obj_t*    s_credits_cont = nullptr;  // scrollable container
static lv_timer_t*  s_scroll_timer = nullptr;

// Live labels (updated in the timer)
static lv_obj_t*    s_lbl_ram      = nullptr;
static lv_obj_t*    s_lbl_psram    = nullptr;
static lv_obj_t*    s_lbl_temp     = nullptr;
static lv_obj_t*    s_lbl_uptime   = nullptr;

// =============================================================================
//  SCROLL TIMER + LIVE UPDATE
// =============================================================================

static void cb_scroll_timer(lv_timer_t* t) {
    if (!s_credits_cont) return;

    // ── Scroll par déplacement direct du conteneur ────────────────────────────
    // lv_obj_scroll_to_y does not work inside an LVGL 8 clip object
    // We move the container upward with set_y
    s_scroll_y += SCROLL_SPEED;

    int32_t content_h = (int32_t)(NB_CREDITS * LINE_H);
    if (s_scroll_y >= content_h) {
        s_scroll_y = 0;
    }

    lv_obj_set_y(s_credits_cont, -(s_scroll_y));

    // ── Live data update (every ~2 s = 40 ticks) ──────────────────────────────
    static uint8_t tick_count = 0;
    tick_count++;
    if (tick_count < 40) return;
    tick_count = 0;

    // Free internal RAM
    if (s_lbl_ram) {
        char buf[32];
        uint32_t free_heap = esp_get_free_heap_size();
        snprintf(buf, sizeof(buf), "Free RAM : %lu KB", free_heap / 1024);
        lv_label_set_text(s_lbl_ram, buf);
    }

    // Free PSRAM
    if (s_lbl_psram) {
        char buf[32];
        uint32_t free_psram = esp_get_free_internal_heap_size();
        // Using heap_caps for PSRAM
        uint32_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        snprintf(buf, sizeof(buf), "Free PSRAM : %lu KB", psram_free / 1024);
        lv_label_set_text(s_lbl_psram, buf);
    }

    // Internal CPU temperature (integrated ESP32-S3 sensor)
    if (s_lbl_temp) {
        char buf[32];
        // temperatureRead() available on ESP32 Arduino core >= 2.x
        float cpu_temp = temperatureRead();
        snprintf(buf, sizeof(buf), "CPU Temp : %.1f C", cpu_temp);
        lv_label_set_text(s_lbl_temp, buf);
    }

    // Uptime
    if (s_lbl_uptime) {
        char buf[32];
        uint32_t secs  = millis() / 1000;
        uint32_t mins  = secs / 60;
        uint32_t hours = mins / 60;
        snprintf(buf, sizeof(buf), "Uptime : %02luh %02lum %02lus",
                 hours, mins % 60, secs % 60);
        lv_label_set_text(s_lbl_uptime, buf);
    }
}

// =============================================================================
//  BUILD
// =============================================================================

void ui_info_build(lv_obj_t* parent) {

    // ── Titre ─────────────────────────────────────────────────────────────────
    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "INFORMATION");
    lv_obj_set_style_text_color(title, COL_GOLD, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(title, 0, 6);
    lv_obj_set_size(title, 320, 18);

    // ── Séparateur ────────────────────────────────────────────────────────────
    lv_obj_t* sep = lv_obj_create(parent);
    lv_obj_remove_style_all(sep);
    lv_obj_set_pos(sep, 8, 28);
    lv_obj_set_size(sep, 304, 1);
    lv_obj_set_style_bg_color(sep, COL_GOLD_DIM, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_40, 0);

    // ── Left logo ───────────────────────────────────────────────────────────
    lv_obj_t* logo_obj = lv_img_create(parent);
    lv_obj_set_pos(logo_obj, 8, 36);
    lv_obj_set_size(logo_obj, 100, 100);
    lv_img_set_src(logo_obj, &CRTeckmini);

    // Project name below the logo
    lv_obj_t* lbl_name = lv_label_create(parent);
    lv_label_set_text(lbl_name, "AERIUS\nGOLD");
    lv_obj_set_style_text_color(lbl_name, COL_GOLD, 0);
    lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(lbl_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(lbl_name, 2, 0);
    lv_obj_set_pos(lbl_name, 8, 140);
    lv_obj_set_size(lbl_name, 100, 30);

    // Vertical separator between logo and credits
    lv_obj_t* vsep = lv_obj_create(parent);
    lv_obj_remove_style_all(vsep);
    lv_obj_set_pos(vsep, 114, 32);
    lv_obj_set_size(vsep, 1, 164);
    lv_obj_set_style_bg_color(vsep, COL_GOLD_DIM, 0);
    lv_obj_set_style_bg_opa(vsep, LV_OPA_20, 0);

    // ── Scrolling credits (right side) ───────────────────────────────────────────
    // Visual mask — clips the visible area
    lv_obj_t* clip = lv_obj_create(parent);
    lv_obj_remove_style_all(clip);
    lv_obj_set_pos(clip, CREDITS_X, CREDITS_Y);
    lv_obj_set_size(clip, CREDITS_W, CREDITS_H);
    lv_obj_set_style_bg_opa(clip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(clip, 0, 0);
    lv_obj_set_style_border_width(clip, 0, 0);
    lv_obj_clear_flag(clip, LV_OBJ_FLAG_SCROLLABLE);

    // Internal scrollable container (height = full content)
    s_total_height = NB_CREDITS * LINE_H;
    s_credits_cont = lv_obj_create(clip);
    lv_obj_remove_style_all(s_credits_cont);
    lv_obj_set_pos(s_credits_cont, 0, 0);
    lv_obj_set_size(s_credits_cont, CREDITS_W, s_total_height);
    lv_obj_set_style_bg_opa(s_credits_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_credits_cont, 0, 0);
    lv_obj_set_style_border_width(s_credits_cont, 0, 0);
    lv_obj_clear_flag(s_credits_cont, LV_OBJ_FLAG_SCROLLABLE);

    // Create credit labels
    int live_idx = 0; // index to locate live lines
    for (uint32_t i = 0; i < NB_CREDITS; i++) {
        lv_obj_t* lbl = lv_label_create(s_credits_cont);
        lv_label_set_text(lbl, CREDITS_LINES[i]);
        lv_obj_set_pos(lbl, 4, i * LINE_H);
        lv_obj_set_size(lbl, CREDITS_W - 8, LINE_H);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);

        // Style based on content type
        if (CREDITS_LINES[i][0] == '\0') {
            // empty line — nothing to do
        } else if (CREDITS_LINES[i][0] == '-') {
            // separator line
            lv_obj_set_style_text_color(lbl, COL_GOLD_DIM, 0);
        } else if (strcmp(CREDITS_LINES[i], "HARDWARE") == 0 ||
                   strcmp(CREDITS_LINES[i], "LIBRARIES") == 0 ||
                   strcmp(CREDITS_LINES[i], "SYSTEM") == 0) {
            // section title: bright gold, font 12, very light gold background
            lv_obj_set_style_text_color(lbl, COL_GOLD, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_letter_space(lbl, 3, 0);
            lv_obj_set_style_bg_color(lbl, COL_GOLD_DIM, 0);
            lv_obj_set_style_bg_opa(lbl, LV_OPA_20, 0);
            lv_obj_set_style_radius(lbl, 3, 0);
        } else if (strcmp(CREDITS_LINES[i], ">> LIVE DATA <<") == 0) {
            lv_obj_set_style_text_color(lbl, COL_GREY, 0);
        } else if (strcmp(CREDITS_LINES[i], "CRTeck - 2026") == 0 ||
                   strcmp(CREDITS_LINES[i], "AERIUS GOLD  v1.0") == 0 ||
                   strcmp(CREDITS_LINES[i], "Designed & Built by") == 0) {
            lv_obj_set_style_text_color(lbl, COL_WHITE, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        } else {
            lv_obj_set_style_text_color(lbl, COL_GREY_LIGHT, 0);
        }

        // Locate the 4 live lines (after ">> LIVE DATA <<")
        if (strcmp(CREDITS_LINES[i], ">> LIVE DATA <<") == 0) {
            // The 4 following lines are live labels
            if (i + 1 < NB_CREDITS) {
                s_lbl_ram = lv_label_create(s_credits_cont);
                lv_label_set_text(s_lbl_ram, "Free RAM : ...");
                lv_obj_set_pos(s_lbl_ram, 4, (i+1) * LINE_H);
                lv_obj_set_size(s_lbl_ram, CREDITS_W - 8, LINE_H);
                lv_obj_set_style_text_color(s_lbl_ram, COL_GREEN, 0);
                lv_obj_set_style_text_font(s_lbl_ram, &lv_font_montserrat_10, 0);
            }
            if (i + 2 < NB_CREDITS) {
                s_lbl_psram = lv_label_create(s_credits_cont);
                lv_label_set_text(s_lbl_psram, "Free PSRAM : ...");
                lv_obj_set_pos(s_lbl_psram, 4, (i+2) * LINE_H);
                lv_obj_set_size(s_lbl_psram, CREDITS_W - 8, LINE_H);
                lv_obj_set_style_text_color(s_lbl_psram, COL_GREEN, 0);
                lv_obj_set_style_text_font(s_lbl_psram, &lv_font_montserrat_10, 0);
            }
            if (i + 3 < NB_CREDITS) {
                s_lbl_temp = lv_label_create(s_credits_cont);
                lv_label_set_text(s_lbl_temp, "CPU Temp : ...");
                lv_obj_set_pos(s_lbl_temp, 4, (i+3) * LINE_H);
                lv_obj_set_size(s_lbl_temp, CREDITS_W - 8, LINE_H);
                lv_obj_set_style_text_color(s_lbl_temp, COL_GOLD, 0);
                lv_obj_set_style_text_font(s_lbl_temp, &lv_font_montserrat_10, 0);
            }
            if (i + 4 < NB_CREDITS) {
                s_lbl_uptime = lv_label_create(s_credits_cont);
                lv_label_set_text(s_lbl_uptime, "Uptime : ...");
                lv_obj_set_pos(s_lbl_uptime, 4, (i+4) * LINE_H);
                lv_obj_set_size(s_lbl_uptime, CREDITS_W - 8, LINE_H);
                lv_obj_set_style_text_color(s_lbl_uptime, COL_GOLD, 0);
                lv_obj_set_style_text_font(s_lbl_uptime, &lv_font_montserrat_10, 0);
            }
        }
    }

    // ── 50 ms scroll timer ─────────────────────────────────────────────────────
    s_scroll_y    = 0;
    s_scroll_timer = lv_timer_create(cb_scroll_timer, 50, nullptr);
}

// =============================================================================
//  TICK (called in loop — optional, LVGL timer handles everything)
// =============================================================================
void ui_info_tick() {
    // Nothing — everything is managed by the internal lv_timer
}
