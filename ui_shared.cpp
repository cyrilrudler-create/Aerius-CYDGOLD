// =============================================================================
//  UI_SHARED.CPP — Styles, global state, fan PWM, navbar, navigation
// =============================================================================

#include "ui_shared.h"
#include "Config.h"
#include <driver/ledc.h>

// =============================================================================
//  STYLES (extern declarations in ui_shared.h)
// =============================================================================
lv_style_t g_sty_page;
lv_style_t g_sty_card;
lv_style_t g_sty_btn_dark;
lv_style_t g_sty_btn_dark_pr;
lv_style_t g_sty_btn_round;
lv_style_t g_sty_btn_round_pr;
lv_style_t g_sty_nav;
lv_style_t g_sty_nav_btn;
lv_style_t g_sty_nav_act;
lv_style_t g_sty_arc_ind;
lv_style_t g_sty_arc_bg;
lv_style_t g_sty_arc_knob;

// =============================================================================
//  GLOBAL STATE (extern declarations in ui_shared.h)
// =============================================================================
uint8_t  g_fan_speed    = 0;
bool     g_fan_running  = false;
uint32_t g_timer_secs   = 0;
uint32_t g_timer_start  = 0;

uint8_t  g_led_effect   = 0;
uint32_t g_led_color    = 0xC9A84C;
uint8_t  g_led_bright   = 150;

float    g_temperature  = 0.0f;
float    g_humidity     = 0.0f;

lv_obj_t* g_pages[3]     = {nullptr};
lv_obj_t* g_nav_btns[3]  = {nullptr};
uint8_t   g_current_page = 0;

// =============================================================================
//  FAN PWM
// =============================================================================
void fan_set_speed(uint8_t pct) {
    pct = constrain(pct, 0, 100);
    g_fan_speed = pct;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, FAN_PWM_CHANNEL, (pct * 255) / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, FAN_PWM_CHANNEL);
}

void fan_stop() {
    g_fan_running = false;
    fan_set_speed(0);
}

void fan_start() {
    g_fan_running = true;
    if (g_fan_speed == 0) g_fan_speed = 50;
    fan_set_speed(g_fan_speed);
}

// =============================================================================
//  NAVIGATION
// =============================================================================
void ui_nav_update(uint8_t active) {
    g_current_page = active;
    for (int i = 0; i < 3; i++) {
        bool is_active = (i == (int)active);

        // Gold underline on active tab, none on others
        lv_obj_set_style_bg_opa(g_nav_btns[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(g_nav_btns[i], is_active ? 2 : 0, 0);
        lv_obj_set_style_border_color(g_nav_btns[i], COL_GOLD, 0);
        lv_obj_set_style_border_side(g_nav_btns[i], LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_shadow_width(g_nav_btns[i], 0, 0);

        // Label colour: gold if active, grey otherwise
        // Label is the first child (index 0)
        lv_obj_t* lbl = lv_obj_get_child(g_nav_btns[i], 0);
        if (lbl) {
            lv_obj_set_style_text_color(lbl, is_active ? COL_GOLD : COL_GREY, 0);
        }

        // Show / hide the corresponding page
        if (is_active)
            lv_obj_clear_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
}

// Static callbacks for the navbar
static void cb_nav0(lv_event_t*) { ui_nav_update(0); }
static void cb_nav1(lv_event_t*) { ui_nav_update(1); }
static void cb_nav2(lv_event_t*) { ui_nav_update(2); }

void build_navbar(lv_obj_t* parent) {
    // No container — buttons are placed directly on the parent
    // (container or screen) with absolute coordinates.
    // This is the only reliable method in LVGL 8 to prevent the auto
    // layout from stacking everything top-left.

    const char*   labels[3] = {"HOME", "LEDs", "INFO"};
    lv_event_cb_t cbs[3]    = {cb_nav0, cb_nav1, cb_nav2};

    // Navbar background: plain rectangle, no lv_obj_create overhead
    lv_obj_t* nav_bg = lv_obj_create(parent);
    lv_obj_remove_style_all(nav_bg);
    lv_obj_set_pos(nav_bg, 0, 200);
    lv_obj_set_size(nav_bg, 320, 40);
    lv_obj_set_style_bg_color(nav_bg, lv_color_hex(0x0A0A10), 0);
    lv_obj_set_style_bg_opa(nav_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(nav_bg, COL_GOLD_DIM, 0);
    lv_obj_set_style_border_width(nav_bg, 1, 0);
    lv_obj_set_style_border_side(nav_bg, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_opa(nav_bg, LV_OPA_30, 0);
    lv_obj_set_style_radius(nav_bg, 0, 0);
    lv_obj_set_style_pad_all(nav_bg, 0, 0);
    lv_obj_clear_flag(nav_bg, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 3; i++) {
        // Button placed directly on parent, not on nav_bg
        g_nav_btns[i] = lv_btn_create(parent);
        lv_obj_remove_style_all(g_nav_btns[i]);
        lv_obj_set_pos(g_nav_btns[i], i * 107, 200);
        lv_obj_set_size(g_nav_btns[i], 107, 40);
        lv_obj_set_style_bg_opa(g_nav_btns[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(g_nav_btns[i], 0, 0);
        lv_obj_set_style_shadow_width(g_nav_btns[i], 0, 0);
        lv_obj_set_style_radius(g_nav_btns[i], 0, 0);
        lv_obj_set_style_pad_all(g_nav_btns[i], 0, 0);

        // Label manually centred within the button
        lv_obj_t* lbl = lv_label_create(g_nav_btns[i]);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_letter_space(lbl, 1, 0);
        lv_obj_set_style_text_color(lbl, COL_GREY, 0);
        lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
        // Absolute position inside button: manually centred
        lv_obj_set_pos(lbl, 0, 0);
        lv_obj_set_size(lbl, 107, 40);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(lbl, 14, 0);  // (40 - 12px font) / 2 ~= 14

        lv_obj_add_event_cb(g_nav_btns[i], cbs[i], LV_EVENT_CLICKED, nullptr);
    }
}

// =============================================================================
//  STYLES
// =============================================================================
void ui_shared_init_styles() {
    lv_style_init(&g_sty_page);
    lv_style_set_bg_color(&g_sty_page, COL_BG);
    lv_style_set_bg_opa(&g_sty_page, LV_OPA_COVER);
    lv_style_set_border_width(&g_sty_page, 0);
    lv_style_set_pad_all(&g_sty_page, 0);
    lv_style_set_radius(&g_sty_page, 0);

    lv_style_init(&g_sty_card);
    lv_style_set_bg_color(&g_sty_card, COL_SURFACE);
    lv_style_set_bg_opa(&g_sty_card, LV_OPA_COVER);
    lv_style_set_border_color(&g_sty_card, COL_GOLD_DIM);
    lv_style_set_border_width(&g_sty_card, 1);
    lv_style_set_border_opa(&g_sty_card, LV_OPA_40);
    lv_style_set_radius(&g_sty_card, 12);
    lv_style_set_pad_all(&g_sty_card, 8);
    lv_style_set_shadow_width(&g_sty_card, 0);

    lv_style_init(&g_sty_btn_dark);
    lv_style_set_bg_color(&g_sty_btn_dark, COL_SURFACE2);
    lv_style_set_bg_opa(&g_sty_btn_dark, LV_OPA_COVER);
    lv_style_set_border_color(&g_sty_btn_dark, COL_GREY);
    lv_style_set_border_width(&g_sty_btn_dark, 1);
    lv_style_set_border_opa(&g_sty_btn_dark, LV_OPA_30);
    lv_style_set_radius(&g_sty_btn_dark, 8);
    lv_style_set_shadow_width(&g_sty_btn_dark, 0);

    lv_style_init(&g_sty_btn_dark_pr);
    lv_style_set_bg_color(&g_sty_btn_dark_pr, lv_color_hex(0x2A2A38));

    lv_style_init(&g_sty_btn_round);
    lv_style_set_bg_color(&g_sty_btn_round, COL_SURFACE2);
    lv_style_set_bg_opa(&g_sty_btn_round, LV_OPA_COVER);
    lv_style_set_border_color(&g_sty_btn_round, COL_GOLD_DIM);
    lv_style_set_border_width(&g_sty_btn_round, 1);
    lv_style_set_border_opa(&g_sty_btn_round, LV_OPA_50);
    lv_style_set_radius(&g_sty_btn_round, LV_RADIUS_CIRCLE);
    lv_style_set_shadow_width(&g_sty_btn_round, 0);

    lv_style_init(&g_sty_btn_round_pr);
    lv_style_set_bg_color(&g_sty_btn_round_pr, lv_color_hex(0x2A2A38));

    lv_style_init(&g_sty_nav);
    lv_style_set_bg_color(&g_sty_nav, lv_color_hex(0x0A0A10));
    lv_style_set_bg_opa(&g_sty_nav, LV_OPA_COVER);
    lv_style_set_border_color(&g_sty_nav, COL_GOLD_DIM);
    lv_style_set_border_width(&g_sty_nav, 1);
    lv_style_set_border_opa(&g_sty_nav, LV_OPA_30);
    lv_style_set_pad_all(&g_sty_nav, 0);
    lv_style_set_radius(&g_sty_nav, 0);
    lv_style_set_shadow_width(&g_sty_nav, 0);

    lv_style_init(&g_sty_nav_btn);
    lv_style_set_bg_opa(&g_sty_nav_btn, LV_OPA_TRANSP);
    lv_style_set_border_width(&g_sty_nav_btn, 0);
    lv_style_set_radius(&g_sty_nav_btn, 0);
    lv_style_set_shadow_width(&g_sty_nav_btn, 0);

    lv_style_init(&g_sty_nav_act);
    lv_style_set_bg_opa(&g_sty_nav_act, LV_OPA_TRANSP);
    lv_style_set_border_color(&g_sty_nav_act, COL_GOLD);
    lv_style_set_border_side(&g_sty_nav_act, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_width(&g_sty_nav_act, 2);
    lv_style_set_radius(&g_sty_nav_act, 0);
    lv_style_set_shadow_width(&g_sty_nav_act, 0);

    lv_style_init(&g_sty_arc_ind);
    lv_style_set_arc_color(&g_sty_arc_ind, COL_GOLD);
    lv_style_set_arc_width(&g_sty_arc_ind, 7);

    lv_style_init(&g_sty_arc_bg);
    lv_style_set_arc_color(&g_sty_arc_bg, COL_ARC_BG);
    lv_style_set_arc_width(&g_sty_arc_bg, 7);

    lv_style_init(&g_sty_arc_knob);
    lv_style_set_bg_color(&g_sty_arc_knob, COL_GOLD);
    lv_style_set_bg_opa(&g_sty_arc_knob, LV_OPA_COVER);
    lv_style_set_pad_all(&g_sty_arc_knob, 3);
}
