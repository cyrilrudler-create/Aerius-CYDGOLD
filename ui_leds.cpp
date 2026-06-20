// =============================================================================
//  UI_LEDS.CPP — LED effects page + WS2812B control
//  10 effects on 2 rows, 8 colour presets
// =============================================================================

#include "ui_leds.h"
#include "ui_mqtt.h"
#include "ui_shared.h"
#include "Config.h"
#include <Adafruit_NeoPixel.h>
#include <math.h>

// ── LED hardware ──────────────────────────────────────────────────────────────
static Adafruit_NeoPixel s_led_fan (LED_FAN_COUNT,  LED_FAN_GPIO,  NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel s_led_ring(LED_RING_COUNT, LED_RING_GPIO, NEO_GRB + NEO_KHZ800);

// ── Animation state ────────────────────────────────────────────────────────────
static float    s_breath_phase  = 0.0f;
static uint8_t  s_spin_pos      = 0;
static uint32_t s_last_led_ms   = 0;
static uint8_t  s_meteor_pos    = 0;
static float    s_pulse_phase   = 0.0f;
static uint8_t  s_twinkle_timer = 0;
static float    s_wave_phase    = 0.0f;

// ── LVGL objects ───────────────────────────────────────────────────────────────
#define NB_EFFECTS  10
#define NB_COLORS   8

static lv_obj_t* s_btn_effects[NB_EFFECTS] = {nullptr};
static lv_obj_t* s_color_btns[NB_COLORS]   = {nullptr};
static lv_obj_t* s_slider_bright            = nullptr;

// ── Names and colours ──────────────────────────────────────────────────────────
static const char* EFFECT_NAMES[NB_EFFECTS] = {
    "OFF","STATIC","BREATH","SPIN","RAINBOW",   // row 1
    "FIRE","VORTEX","PULSE","TWINKLE","WAVE"    // row 2
};

static const uint32_t PRESET_COLORS[NB_COLORS] = {
    0xC9A84C,   // Or chaud
    0xF0EDE8,   // Blanc nacré
    0x5090E0,   // Bleu glace
    0x50C878,   // Vert menthe
    0xE05050,   // Rouge rosé
    0xFF6B35,   // Orange
    0xAA44FF,   // Violet
    0xFFE000    // Jaune
};

// =============================================================================
//  LED EFFECTS
// =============================================================================

static void leds_apply_effect() {
    uint8_t r = (g_led_color >> 16) & 0xFF;
    uint8_t g = (g_led_color >> 8)  & 0xFF;
    uint8_t b =  g_led_color        & 0xFF;

    switch (g_led_effect) {

        case 0: // OFF
            s_led_fan.fill(0);  s_led_ring.fill(0);
            s_led_fan.show();   s_led_ring.show();
            break;

        case 1: { // STATIC
            uint8_t R = r * g_led_bright / 255;
            uint8_t G = g * g_led_bright / 255;
            uint8_t B = b * g_led_bright / 255;
            s_led_fan.fill( s_led_fan.Color(R, G, B));
            s_led_ring.fill(s_led_ring.Color(R, G, B));
            s_led_fan.show(); s_led_ring.show();
            break;
        }

        case 2: { // BREATH — soft sine wave
            float val = (sinf(s_breath_phase) + 1.0f) * 0.5f;
            s_breath_phase += 0.04f;
            if (s_breath_phase > 2.0f * M_PI) s_breath_phase -= 2.0f * M_PI;
            uint8_t bri = (uint8_t)(val * g_led_bright);
            s_led_fan.fill( s_led_fan.Color( r*bri/255, g*bri/255, b*bri/255));
            s_led_ring.fill(s_led_ring.Color(r*bri/255, g*bri/255, b*bri/255));
            s_led_fan.show(); s_led_ring.show();
            break;
        }

        case 3: { // SPIN — pixel + trailing tail
            s_led_fan.fill(0); s_led_ring.fill(0);
            s_spin_pos = (s_spin_pos + 1) % LED_RING_COUNT;
            s_led_ring.setPixelColor(s_spin_pos,
                s_led_ring.Color(r*g_led_bright/255, g*g_led_bright/255, b*g_led_bright/255));
            for (int t = 1; t <= 6; t++) {
                int idx = ((int)s_spin_pos - t + LED_RING_COUNT) % LED_RING_COUNT;
                float fade = 1.0f - (float)t / 7.0f;
                s_led_ring.setPixelColor(idx, s_led_ring.Color(
                    (uint8_t)(r * g_led_bright / 255 * fade),
                    (uint8_t)(g * g_led_bright / 255 * fade),
                    (uint8_t)(b * g_led_bright / 255 * fade)));
            }
            uint8_t fi = (s_spin_pos * LED_FAN_COUNT / LED_RING_COUNT) % LED_FAN_COUNT;
            s_led_fan.setPixelColor(fi,
                s_led_fan.Color(r*g_led_bright/255, g*g_led_bright/255, b*g_led_bright/255));
            s_led_fan.show(); s_led_ring.show();
            break;
        }

        case 4: { // RAINBOW
            for (int i = 0; i < LED_RING_COUNT; i++)
                s_led_ring.setPixelColor(i, s_led_ring.ColorHSV(
                    (uint16_t)((i * 65536L / LED_RING_COUNT) + (s_spin_pos * 256)),
                    255, g_led_bright));
            for (int i = 0; i < LED_FAN_COUNT; i++)
                s_led_fan.setPixelColor(i, s_led_fan.ColorHSV(
                    (uint16_t)((i * 65536L / LED_FAN_COUNT) + (s_spin_pos * 256)),
                    255, g_led_bright));
            s_spin_pos += 2;
            s_led_fan.show(); s_led_ring.show();
            break;
        }

        case 5: { // FIRE — random red/orange flames
            // Ring simulates flames: each pixel has a random intensity
            // in red/orange tones, independent of the chosen colour
            for (int i = 0; i < LED_RING_COUNT; i++) {
                uint8_t heat = (uint8_t)(random(120, 255) * g_led_bright / 255);
                uint8_t orange = heat / 2;
                s_led_ring.setPixelColor(i, s_led_ring.Color(heat, orange / 3, 0));
            }
            // Fan: user base colour with flicker
            for (int i = 0; i < LED_FAN_COUNT; i++) {
                uint8_t flicker = (uint8_t)(random(180, 255) * g_led_bright / 255);
                s_led_fan.setPixelColor(i, s_led_fan.Color(
                    r * flicker / 255, g * flicker / 255, b * flicker / 255));
            }
            s_led_fan.show(); s_led_ring.show();
            break;
        }

        case 6: { // VORTEX — synchronised ring+fan spiral with depth illusion
            // Ring (36 LEDs) rotates at normal speed
            // Fan (12 LEDs) rotates 3x faster -> converging spiral effect
            // Colour gradient -> black to simulate depth

            s_meteor_pos++;  // reused as phase counter

            // ── Outer ring: 3 light arms spaced 120° apart ──────────────────
            s_led_ring.fill(0);
            for (int arm = 0; arm < 3; arm++) {
                int head = ((int)s_meteor_pos + arm * (LED_RING_COUNT / 3)) % LED_RING_COUNT;
                // Bright head pixel
                s_led_ring.setPixelColor(head, s_led_ring.Color(
                    r * g_led_bright / 255,
                    g * g_led_bright / 255,
                    b * g_led_bright / 255));
                // 5-pixel trailing tail that fades out
                for (int t = 1; t <= 5; t++) {
                    int idx = ((int)head - t + LED_RING_COUNT) % LED_RING_COUNT;
                    float fade = 1.0f - (float)t / 6.0f;
                    s_led_ring.setPixelColor(idx, s_led_ring.Color(
                        (uint8_t)(r * g_led_bright / 255 * fade),
                        (uint8_t)(g * g_led_bright / 255 * fade),
                        (uint8_t)(b * g_led_bright / 255 * fade)));
                }
            }

            // ── Inner fan: 2 opposite arms, 3x speed -> spiral effect ─────
            // Rotates 3x faster -> creates the illusion of light diving toward the center
            s_led_fan.fill(0);
            for (int arm = 0; arm < 2; arm++) {
                int fhead = ((int)(s_meteor_pos * 3) + arm * (LED_FAN_COUNT / 2)) % LED_FAN_COUNT;
                // Slightly shifted complementary colour for depth effect
                uint8_t fr = (uint8_t)(255 - r) * g_led_bright / 255 / 3;
                uint8_t fg = (uint8_t)(255 - g) * g_led_bright / 255 / 3;
                uint8_t fb = (uint8_t)(255 - b) * g_led_bright / 255 / 3;
                s_led_fan.setPixelColor(fhead, s_led_fan.Color(
                    r * g_led_bright / 255,
                    g * g_led_bright / 255,
                    b * g_led_bright / 255));
                // Short fan trailing tail
                for (int t = 1; t <= 3; t++) {
                    int idx = ((int)fhead - t + LED_FAN_COUNT) % LED_FAN_COUNT;
                    float fade = 1.0f - (float)t / 4.0f;
                    s_led_fan.setPixelColor(idx, s_led_fan.Color(
                        (uint8_t)(fr * fade),
                        (uint8_t)(fg * fade),
                        (uint8_t)(fb * fade)));
                }
            }

            s_led_fan.show(); s_led_ring.show();
            break;
        }

        case 7: { // PULSE — heartbeat pattern (double fast pulse)
            // Phase 0->pi: first peak, pi->2pi: rest, 2pi->3pi: second weaker peak
            s_pulse_phase += 0.08f;
            if (s_pulse_phase > 3.0f * M_PI) s_pulse_phase = 0.0f;
            float val = 0.0f;
            if (s_pulse_phase < M_PI)
                val = sinf(s_pulse_phase);                      // 1st strong peak
            else if (s_pulse_phase < 1.6f * M_PI)
                val = 0.0f;                                      // short rest
            else if (s_pulse_phase < 2.6f * M_PI)
                val = 0.5f * sinf(s_pulse_phase - 1.6f * M_PI); // 2nd weaker peak
            uint8_t bri = (uint8_t)(val * g_led_bright);
            s_led_fan.fill( s_led_fan.Color( r*bri/255, g*bri/255, b*bri/255));
            s_led_ring.fill(s_led_ring.Color(r*bri/255, g*bri/255, b*bri/255));
            s_led_fan.show(); s_led_ring.show();
            break;
        }

        case 8: { // TWINKLE — random star-like sparkle
            // Every N frames, random pixels are turned on/off
            s_twinkle_timer++;
            if (s_twinkle_timer >= 2) {
                s_twinkle_timer = 0;
                // Ring: 3 random pixels light up, others go dark
                for (int i = 0; i < LED_RING_COUNT; i++) {
                    uint8_t chance = (uint8_t)random(0, 10);
                    if (chance < 2) {
                        uint8_t bri = (uint8_t)(random(100, 255) * g_led_bright / 255);
                        s_led_ring.setPixelColor(i,
                            s_led_ring.Color(r*bri/255, g*bri/255, b*bri/255));
                    } else {
                        s_led_ring.setPixelColor(i, 0);
                    }
                }
                // Fan: same
                for (int i = 0; i < LED_FAN_COUNT; i++) {
                    uint8_t chance = (uint8_t)random(0, 5);
                    if (chance == 0) {
                        uint8_t bri = (uint8_t)(random(100, 255) * g_led_bright / 255);
                        s_led_fan.setPixelColor(i,
                            s_led_fan.Color(r*bri/255, g*bri/255, b*bri/255));
                    } else {
                        s_led_fan.setPixelColor(i, 0);
                    }
                }
            }
            s_led_fan.show(); s_led_ring.show();
            break;
        }

        case 9: { // WAVE — sine wave travelling around the ring
            s_wave_phase += 0.15f;
            if (s_wave_phase > 2.0f * M_PI) s_wave_phase -= 2.0f * M_PI;
            for (int i = 0; i < LED_RING_COUNT; i++) {
                float angle = s_wave_phase + (float)i * 2.0f * M_PI / LED_RING_COUNT;
                float val   = (sinf(angle) + 1.0f) * 0.5f;
                uint8_t bri = (uint8_t)(val * g_led_bright);
                s_led_ring.setPixelColor(i,
                    s_led_ring.Color(r*bri/255, g*bri/255, b*bri/255));
            }
            // Fan: wave shifted by pi
            for (int i = 0; i < LED_FAN_COUNT; i++) {
                float angle = s_wave_phase + M_PI + (float)i * 2.0f * M_PI / LED_FAN_COUNT;
                float val   = (sinf(angle) + 1.0f) * 0.5f;
                uint8_t bri = (uint8_t)(val * g_led_bright);
                s_led_fan.setPixelColor(i,
                    s_led_fan.Color(r*bri/255, g*bri/255, b*bri/255));
            }
            s_led_fan.show(); s_led_ring.show();
            break;
        }
    }
}

// =============================================================================
//  CALLBACKS
// =============================================================================

static void update_effect_btn_styles() {
    for (int i = 0; i < NB_EFFECTS; i++) {
        if (!s_btn_effects[i]) continue;
        bool active = (i == (int)g_led_effect);
        lv_obj_set_style_bg_color(s_btn_effects[i],
            active ? COL_GOLD : COL_SURFACE2, 0);
        lv_obj_set_style_bg_opa(s_btn_effects[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_btn_effects[i], active ? 0 : 1, 0);
        lv_obj_set_style_border_color(s_btn_effects[i], COL_GREY, 0);
        lv_obj_set_style_border_opa(s_btn_effects[i], LV_OPA_30, 0);
        lv_obj_t* lbl = lv_obj_get_child(s_btn_effects[i], 0);
        if (lbl) lv_obj_set_style_text_color(lbl,
            active ? COL_BG : COL_WHITE, 0);
    }
}

static void cb_effect_btn(lv_event_t* e) {
    g_led_effect = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    update_effect_btn_styles();
    mqtt_publish_led_state();
}

static void cb_color_btn(lv_event_t* e) {
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    g_led_color = PRESET_COLORS[idx];
    for (int i = 0; i < NB_COLORS; i++) {
        if (!s_color_btns[i]) continue;
        lv_obj_set_style_border_width(s_color_btns[i], i == idx ? 2 : 0, 0);
        lv_obj_set_style_border_color(s_color_btns[i], COL_WHITE, 0);
    }

    // Pure white (index 1 = 0xF0EDE8 ~= white) -> cap brightness
    // Detection: all 3 channels are high (> 0xC0)
    uint8_t r = (g_led_color >> 16) & 0xFF;
    uint8_t g = (g_led_color >> 8)  & 0xFF;
    uint8_t b =  g_led_color        & 0xFF;
    bool is_white = (r > 0xC0 && g > 0xC0 && b > 0xC0);

    if (is_white && g_led_bright > LED_WHITE_BRIGHT_MAX) {
        g_led_bright = LED_WHITE_BRIGHT_MAX;
        s_led_fan.setBrightness(g_led_bright);
        s_led_ring.setBrightness(g_led_bright);
        if (s_slider_bright)
            lv_slider_set_value(s_slider_bright, g_led_bright, LV_ANIM_OFF);
        Serial.print("[LEDS] White detected — brightness capped to ");
        Serial.println(LED_WHITE_BRIGHT_MAX);
    }

    // Limite aussi le slider en blanc
    if (s_slider_bright)
        lv_slider_set_range(s_slider_bright, 0, is_white ? LED_WHITE_BRIGHT_MAX : 255);
}

static void cb_brightness(lv_event_t* e) {
    g_led_bright = (uint8_t)lv_slider_get_value(lv_event_get_target(e));

    // Cap if pure white is selected
    uint8_t r = (g_led_color >> 16) & 0xFF;
    uint8_t g = (g_led_color >> 8)  & 0xFF;
    uint8_t b =  g_led_color        & 0xFF;
    bool is_white = (r > 0xC0 && g > 0xC0 && b > 0xC0);
    if (is_white && g_led_bright > LED_WHITE_BRIGHT_MAX) {
        g_led_bright = LED_WHITE_BRIGHT_MAX;
        lv_slider_set_value(lv_event_get_target(e), g_led_bright, LV_ANIM_OFF);
    }

    s_led_fan.setBrightness(g_led_bright);
    s_led_ring.setBrightness(g_led_bright);
    mqtt_publish_led_state();
}

// =============================================================================
//  BUILD
// =============================================================================

void ui_leds_build(lv_obj_t* parent) {

    // ── Title ─────────────────────────────────────────────────────────────────
    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "LIGHT EFFECTS");
    lv_obj_set_style_text_color(title, COL_GOLD, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_set_pos(title, 0, 4);
    lv_obj_set_size(title, 320, 18);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // ── Separator ────────────────────────────────────────────────────────────
    lv_obj_t* sep = lv_obj_create(parent);
    lv_obj_remove_style_all(sep);
    lv_obj_set_pos(sep, 8, 25);
    lv_obj_set_size(sep, 304, 1);
    lv_obj_set_style_bg_color(sep, COL_GOLD_DIM, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_40, 0);

    // ── 10 buttons on 2 rows of 5 ─────────────────────────────────────────
    // 5 x 58px + 4 x 5px = 310px -> margin = (320-310)/2 = 5
    const lv_coord_t bw = 58, bh = 28, bgap = 5;
    const lv_coord_t bsx = (320 - (5 * bw + 4 * bgap)) / 2;

    for (int i = 0; i < NB_EFFECTS; i++) {
        int col  = i % 5;
        int row  = i / 5;
        lv_coord_t bx = bsx + col * (bw + bgap);
        lv_coord_t by = (row == 0) ? 30 : 62;

        s_btn_effects[i] = lv_btn_create(parent);
        lv_obj_remove_style_all(s_btn_effects[i]);
        lv_obj_set_pos(s_btn_effects[i], bx, by);
        lv_obj_set_size(s_btn_effects[i], bw, bh);
        lv_obj_set_style_radius(s_btn_effects[i], 6, 0);
        lv_obj_set_style_shadow_width(s_btn_effects[i], 0, 0);

        lv_obj_t* lbl = lv_label_create(s_btn_effects[i]);
        lv_label_set_text(lbl, EFFECT_NAMES[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(lbl, 0, 9);
        lv_obj_set_size(lbl, bw, 12);

        lv_obj_add_event_cb(s_btn_effects[i], cb_effect_btn,
                            LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }
    update_effect_btn_styles();

    // ── Separator ────────────────────────────────────────────────────────────
    lv_obj_t* sep2 = lv_obj_create(parent);
    lv_obj_remove_style_all(sep2);
    lv_obj_set_pos(sep2, 8, 95);
    lv_obj_set_size(sep2, 304, 1);
    lv_obj_set_style_bg_color(sep2, COL_GOLD_DIM, 0);
    lv_obj_set_style_bg_opa(sep2, LV_OPA_20, 0);

    // ── Brightness label + slider ─────────────────────────────────────────────
    lv_obj_t* lbl_bri = lv_label_create(parent);
    lv_label_set_text(lbl_bri, "BRIGHTNESS");
    lv_obj_set_style_text_color(lbl_bri, COL_GREY_LIGHT, 0);
    lv_obj_set_style_text_font(lbl_bri, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_letter_space(lbl_bri, 2, 0);
    lv_obj_set_pos(lbl_bri, 16, 101);
    lv_obj_set_size(lbl_bri, 100, 12);

    s_slider_bright = lv_slider_create(parent);
    lv_obj_set_pos(s_slider_bright, 16, 118);
    lv_obj_set_size(s_slider_bright, 288, 10);
    lv_slider_set_range(s_slider_bright, 0, 255);
    lv_slider_set_value(s_slider_bright, g_led_bright, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_slider_bright, COL_ARC_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_slider_bright, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_slider_bright, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_slider_bright, COL_GOLD, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_slider_bright, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider_bright, COL_WHITE, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_slider_bright, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_slider_bright, 6, LV_PART_KNOB);
    lv_obj_set_style_radius(s_slider_bright, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_add_event_cb(s_slider_bright, cb_brightness, LV_EVENT_VALUE_CHANGED, nullptr);

    // ── Colour label + swatches ─────────────────────────────────────────────
    lv_obj_t* lbl_col = lv_label_create(parent);
    lv_label_set_text(lbl_col, "COLOR");
    lv_obj_set_style_text_color(lbl_col, COL_GREY_LIGHT, 0);
    lv_obj_set_style_text_font(lbl_col, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_letter_space(lbl_col, 2, 0);
    lv_obj_set_pos(lbl_col, 16, 138);
    lv_obj_set_size(lbl_col, 80, 12);

    // 8 x 30px + 7 x 7px = 289px -> margin = (320-289)/2 = 15
    const lv_coord_t cs = 30, cg = 7;
    const lv_coord_t csx = (320 - (NB_COLORS * cs + (NB_COLORS - 1) * cg)) / 2;

    for (int i = 0; i < NB_COLORS; i++) {
        s_color_btns[i] = lv_btn_create(parent);
        lv_obj_remove_style_all(s_color_btns[i]);
        lv_obj_set_pos(s_color_btns[i], csx + i * (cs + cg), 154);
        lv_obj_set_size(s_color_btns[i], cs, cs);
        lv_obj_set_style_radius(s_color_btns[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_color_btns[i], lv_color_hex(PRESET_COLORS[i]), 0);
        lv_obj_set_style_bg_opa(s_color_btns[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_color_btns[i], 0, 0);
        lv_obj_set_style_shadow_width(s_color_btns[i], 0, 0);
        lv_obj_add_event_cb(s_color_btns[i], cb_color_btn,
                            LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }
    lv_obj_set_style_border_width(s_color_btns[0], 2, 0);
    lv_obj_set_style_border_color(s_color_btns[0], COL_WHITE, 0);
}

// =============================================================================
//  HARDWARE INIT
// =============================================================================

void ui_leds_init_hw() {
    s_led_fan.begin();
    s_led_fan.setBrightness(g_led_bright);
    s_led_fan.fill(0);
    s_led_fan.show();

    s_led_ring.begin();
    s_led_ring.setBrightness(g_led_bright);
    s_led_ring.fill(0);
    s_led_ring.show();

    Serial.println("[LEDS] WS2812B init GPIO2 (fan 12px) + GPIO3 (ring 36px)");
}

// =============================================================================
//  TICK
// =============================================================================

void ui_leds_tick() {
    uint32_t now = millis();
    if (now - s_last_led_ms > 50) {
        s_last_led_ms = now;
        leds_apply_effect();
    }
}

// =============================================================================
//  VISUAL UPDATE FROM MQTT
// =============================================================================

void ui_leds_update_visual() {
    // Update brightness slider
    if (s_slider_bright)
        lv_slider_set_value(s_slider_bright, g_led_bright, LV_ANIM_OFF);

    // Update effect buttons (highlight the active one)
    update_effect_btn_styles();
}
