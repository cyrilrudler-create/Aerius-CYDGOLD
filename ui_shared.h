#ifndef UI_SHARED_H
#define UI_SHARED_H

// =============================================================================
//  UI_SHARED.H — Colour palette, shared styles and global inter-page state
//
//  Included by ui_home.h, ui_leds.h, ui_info.h.
//  Do NOT include directly in the .ino — use the module headers instead.
// =============================================================================

#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>   // for extern tft

// ── Colour palette ────────────────────────────────────────────────────────────
#define COL_BG          lv_color_hex(0x0D0D10)
#define COL_SURFACE     lv_color_hex(0x181820)
#define COL_SURFACE2    lv_color_hex(0x202030)
#define COL_GOLD        lv_color_hex(0xC9A84C)
#define COL_GOLD_DIM    lv_color_hex(0x8A6E2F)
#define COL_WHITE       lv_color_hex(0xEEEBE4)
#define COL_GREY        lv_color_hex(0x707080)
#define COL_GREY_LIGHT  lv_color_hex(0x9898A8)
#define COL_RED         lv_color_hex(0xE05050)
#define COL_GREEN       lv_color_hex(0x40C870)
#define COL_ARC_BG      lv_color_hex(0x282835)
#define COL_BLUE        lv_color_hex(0x4080D0)
#define COL_BLUE_DARK   lv_color_hex(0x0044FF) // Deep electric blue (min speed)
#define COL_BLUE_MED    lv_color_hex(0x00A2FF) // Mid electric blue
#define COL_BLUE_LIGHT  lv_color_hex(0x00F0FF) // Cyan / very light blue (max speed)
#define COL_DARK_GLASS  lv_color_hex(0x14171E) // Very dark grey smoked glass effect

// ── Shared styles (defined in ui_shared.cpp) ──────────────────────────────────
extern lv_style_t g_sty_page;       // solid black background, no padding
extern lv_style_t g_sty_card;       // dark surface + subtle gold border
extern lv_style_t g_sty_btn_dark;   // dark button + grey border
extern lv_style_t g_sty_btn_dark_pr;
extern lv_style_t g_sty_btn_round;  // round +/- timer buttons
extern lv_style_t g_sty_btn_round_pr;
extern lv_style_t g_sty_nav;        // navigation bar
extern lv_style_t g_sty_nav_btn;    // inactive tab
extern lv_style_t g_sty_nav_act;    // active tab (gold bottom line)
extern lv_style_t g_sty_arc_ind;    // arc indicator (gold)
extern lv_style_t g_sty_arc_bg;     // arc background (dark grey)
extern lv_style_t g_sty_arc_knob;   // arc knob (gold)
static lv_style_t s_sty_arc_bg_futur;
static lv_style_t s_sty_arc_ind_futur;
static lv_style_t s_sty_arc_knob_futur;
static bool       s_styles_initialized = false;

// ── Fan state (shared home <-> ui_home_tick) ──────────────────────────────────
extern uint8_t   g_fan_speed;
extern bool      g_fan_running;
extern uint32_t  g_timer_secs;
extern uint32_t  g_timer_start;

// ── LED state (shared ui_leds <-> ui_leds_tick) ───────────────────────────────
extern uint8_t   g_led_effect;
extern uint32_t  g_led_color;
extern uint8_t   g_led_bright;

// ── Temperature state (shared home <-> bme280) ───────────────────────────────
extern float     g_temperature;
extern float     g_humidity;

// ── Navigation (shared by all 3 modules) ──────────────────────────────────────
extern lv_obj_t* g_pages[3];
extern lv_obj_t* g_nav_btns[3];
extern uint8_t   g_current_page;

// ── Utility functions ─────────────────────────────────────────────────────────
void ui_shared_init_styles();           // call once in setup()
void ui_nav_update(uint8_t active);     // switch active page + update navbar style
void build_navbar(lv_obj_t* root);      // build the navigation bar on the root screen

// ── Fan PWM — low-level functions (defined in ui_shared.cpp) ──────────────────
void fan_set_speed(uint8_t pct);
void fan_stop();
void fan_start();

#endif // UI_SHARED_H
