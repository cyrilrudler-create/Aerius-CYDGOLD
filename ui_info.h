#ifndef UI_INFO_H
#define UI_INFO_H

// =============================================================================
//  UI_INFO.H — Information / Credits page
//
//  Layout:
//    Left  (0->114px)  : CRTeckmini logo 100x100 + project name
//    Right (120->312px): auto-scrolling credits
//
//  Credits content:
//    - Project info (name, version)
//    - Libraries used with versions
//    - Live data: free RAM, free PSRAM, CPU temperature, uptime
//
//  Scrolling is managed by an LVGL lv_timer (50 ms) — no need to call
//  ui_info_tick() in loop(), but the function is provided for consistency.
// =============================================================================

#include <Arduino.h>
#include <lvgl.h>

void ui_info_build(lv_obj_t* parent);
void ui_info_tick();

#endif // UI_INFO_H
