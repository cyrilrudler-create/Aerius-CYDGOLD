// =============================================================================
//  CYD_LVGL_DRV.CPP — LVGL engine + CYD Gold hardware orchestration
//
//  This file is the "conductor" of the system. It contains:
//    - The global Audio object (single instance, shared via extern)
//    - Private LVGL callbacks (screen flush, touch read)
//    - Implementation of init_cyd_hardware() and init_cyd_lvgl()
//    - loop_cyd_audio(): wrapper around audio.loop()
// =============================================================================

#include "cyd_lvgl_drv.h"
#include "Config.h"         // GPIO pins (I2S_BCK, I2S_WS, I2S_DOUT, I2S_MCK...)
#include "cydgold.h"        // setup_i2c_and_codec, codec_volume, setup_display...
#include "Audio.h"          // ESP32-audioI2S library


// =============================================================================
//  GLOBAL AUDIO OBJECT
//
//  Audio audio(0):
//    Port 0 only — leaves NUM_1 free for recording.
//
//  Declared here = defined ONCE in the entire project.
//  Other modules access it via:
//    extern Audio audio;
// =============================================================================
Audio audio(0);  // Port 0 only — leaves NUM_1 free for recording


// =============================================================================
//  PRIVATE LVGL VARIABLES
// =============================================================================

// LVGL single/double graphics buffer management structure
static lv_disp_draw_buf_t  s_draw_buf;

// Pointer to the allocated pixel buffer (in PSRAM or DRAM)
static lv_color_t*         s_buf = nullptr;

// Handle of the ESP32 timer used for lv_tick_inc(1) every 1 ms
static esp_timer_handle_t  s_lvgl_tick_timer = nullptr;


// =============================================================================
//  PRIVATE LVGL CALLBACKS
// =============================================================================

/**
 * @brief 1 ms timer callback — increments the LVGL internal clock.
 *
 *  LVGL needs an accurate time counter for:
 *    - Animations (duration, easing)
 *    - LVGL timers (lv_timer_create)
 *    - Long-press detection on buttons
 *    - Event timeouts
 *
 *  Placed in IRAM_ATTR: code runs from internal RAM to guarantee
 *  minimal latency even when the Flash cache is busy.
 *
 *  Triggered by an esp_timer (hardware) -> more reliable than millis()
 *  because it increments even when loop() is busy.
 */
static void IRAM_ATTR lvgl_tick_cb(void*) {
    lv_tick_inc(1);  // Signals to LVGL that 1 ms has elapsed
}

/**
 * @brief Render callback — LVGL calls this function to send a rectangular
 *        zone of pixels to the physical screen.
 *
 *  LVGL splits the screen into zones and only sends what has changed.
 *  This function receives:
 *    - area    : coordinates of the rectangle to update (x1,y1,x2,y2)
 *    - color_p : array of RGB565 pixels in row-major order
 *
 *  Implementation via TFT_eSPI:
 *    startWrite() / endWrite(): keeps SPI active between calls
 *    setAddrWindow(): positions the write window on the ILI9341
 *    pushColors(): sends pixels via SPI DMA
 *
 *  lv_disp_flush_ready(): MANDATORY at the end — signals to LVGL that
 *  the flush is complete and it can prepare the next zone.
 */
static void my_disp_flush(lv_disp_drv_t* disp, const lv_area_t* area,
                           lv_color_t* color_p) {
    uint32_t w = area->x2 - area->x1 + 1;  // Zone width in pixels
    uint32_t h = area->y2 - area->y1 + 1;  // Zone height in pixels

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    // reinterpret_cast: lv_color_t is an RGB565 struct, sent as uint16_t*
    tft.pushColors(reinterpret_cast<uint16_t*>(&color_p->full), w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(disp);  // "Flush done" signal -> LVGL can continue
}

/**
 * @brief Touch callback — LVGL calls this function every cycle to know
 *        whether the screen is touched and at what position.
 *
 *  Uses polling (ts.read() on each call) rather than interrupts.
 *  Sufficient for a standard touch interface (reactivity ~20 ms).
 *
 *  For maximum reactivity, the TP_INT interrupt (GPIO17, active low)
 *  could be used to only read ts when triggered.
 *
 *  LV_INDEV_STATE_PR  : pressed
 *  LV_INDEV_STATE_REL : released
 *  points[0]: first contact point (FT6336 supports up to 2 points)
 */
static void my_touchpad_read(lv_indev_drv_t* indev_driver,
                              lv_indev_data_t* data) {
    ts.read();  // Queries FT6336G via I2C and updates ts.isTouched, ts.points[]

    if (ts.isTouched) {
        data->state   = LV_INDEV_STATE_PR;
        data->point.x = ts.points[0].x;    // X coordinate of touch
        data->point.y = ts.points[0].y;    // Y coordinate of touch
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}


// =============================================================================
//  PUBLIC IMPLEMENTATIONS
// =============================================================================

// -----------------------------------------------------------------------------
void init_cyd_hardware() {

    // ── 1. I2S audio ──────────────────────────────────────────────────────────
    //  MUST come first: audio.setPinout() starts MCLK generation on GPIO4.
    //  The ES8311 needs this signal for its internal PLL to lock before it
    //  can respond to I2C commands that follow.
    //  Without active MCLK, I2C writes to the codec fail silently.
    audio.setPinout(I2S_BCK, I2S_WS, I2S_DOUT, I2S_MCK);
    audio.setVolume(12);    // Software volume: 0 (mute) to 21 (max)
                            // Stacks with the codec hardware volume

    // ── 2. ES8311 codec ───────────────────────────────────────────────────────
    //  Initialises the I2C bus and sends the register sequence to the codec.
    //  codec_volume(80) sets the DAC hardware volume (register 0x32).
    if (setup_i2c_and_codec() == ESP_OK) {
        codec_volume(80);   // Hardware volume 80% (~229/255 in DAC register)
        Serial.println("[HARDWARE] ES8311 codec ready");
    } else {
        Serial.println("[HARDWARE] ERROR: ES8311 codec not detected!");
    }

    // ── 3. Amplifier ──────────────────────────────────────────────────────────
    //  Amp is active LOW: LOW = on, HIGH = off.
    //  Must be enabled AFTER the codec to avoid a power-on pop.
    pinMode(AMP_EN, OUTPUT);
    digitalWrite(AMP_EN, LOW);     // Enable amplifier
    Serial.println("[HARDWARE] Amplifier enabled (AMP_EN=LOW)");

    // ── 4. TFT screen ─────────────────────────────────────────────────────────
    setup_display();    // SPI + ILI9341 init + backlight

    // ── 5. FT6336G touch ──────────────────────────────────────────────────────
    setup_touch();      // I2C + rotation matching the screen

    // ── 6. SD card ────────────────────────────────────────────────────────────
    //  Last: no timing dependency, and avoids bus conflicts if an SD error
    //  occurs at startup.
    setup_sd_card();    // SD_MMC 4-bit mode
}

// -----------------------------------------------------------------------------
void init_cyd_lvgl() {
    lv_init();  // Initialises the LVGL graphics engine (internal memory, lists...)

    // ── 1 ms timer for lv_tick_inc() ─────────────────────────────────────────
    //  LVGL needs a time counter for its animations and timers.
    //  Using an esp_timer (hardware, precise, unaffected by CPU load)
    //  rather than millis() in loop() (less precise, depends on loop rate).
    const esp_timer_create_args_t tick_args = {
        .callback        = lvgl_tick_cb,    // Function called every 1 ms
        .arg             = nullptr,
        .dispatch_method = ESP_TIMER_TASK,  // Runs in esp_timer task context
        .name            = "lvgl_tick"      // Name for FreeRTOS debugging
    };
    esp_timer_create(&tick_args, &s_lvgl_tick_timer);
    esp_timer_start_periodic(s_lvgl_tick_timer, 1000); // 1000 us = 1 ms

    // ── Graphics buffer ───────────────────────────────────────────────────────
    //  The buffer holds the pixels to be sent to the screen.
    //  Larger buffer = fewer flush calls -> smoother rendering.
    //  320x40 = 25,600 pixels = 51,200 bytes in RGB565 (2 bytes/pixel)
    //
    //  First attempt: PSRAM (fast, 8 MB available).
    //  Fallback: internal DRAM if PSRAM is unavailable (320x10 = 6,400 bytes).
    s_buf = static_cast<lv_color_t*>(
        heap_caps_malloc(320 * 40 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM)
    );

    if (s_buf) {
        lv_disp_draw_buf_init(&s_draw_buf, s_buf, nullptr, 320 * 40);
        Serial.println("[LVGL] Graphics buffer: PSRAM 320x40 px (51,200 bytes)");
    } else {
        // DRAM fallback — choppier rendering but functional
        s_buf = static_cast<lv_color_t*>(
            heap_caps_malloc(320 * 10 * sizeof(lv_color_t), MALLOC_CAP_DEFAULT)
        );
        if (!s_buf) {
            Serial.println("[LVGL] FATAL ERROR: not enough memory for graphics buffer!");
            while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }  // Controlled halt
        }
        lv_disp_draw_buf_init(&s_draw_buf, s_buf, nullptr, 320 * 10);
        Serial.println("[LVGL] Graphics buffer: DRAM 320x10 px (PSRAM unavailable)");
    }

    // ── Display driver ────────────────────────────────────────────────────────
    //  Registers my_disp_flush() as the pixel-sending function to the screen.
    //  Resolution 320x240 = landscape mode (consistent with tft.setRotation(1)).
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = 320;            // Width in pixels (landscape)
    disp_drv.ver_res  = 240;            // Height in pixels (landscape)
    disp_drv.flush_cb = my_disp_flush;  // Our render callback
    disp_drv.draw_buf = &s_draw_buf;    // Graphics buffer allocated above
    lv_disp_drv_register(&disp_drv);

    // ── Touch input driver ────────────────────────────────────────────────────
    //  Registers my_touchpad_read() as the pointer event source.
    //  LV_INDEV_TYPE_POINTER = "pointer" type (vs keyboard, encoder...)
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // ── SD filesystem driver ──────────────────────────────────────────────────
    //  Allows LVGL to load images and fonts from the SD via "S:/file"
    lv_fs_sd_init();

    Serial.println("[LVGL] LVGL engine initialised and ready");
}

// -----------------------------------------------------------------------------
void loop_cyd_audio() {
    // Polling call for the ESP32-audioI2S library.
    // Each call reads a data chunk, decodes it, and fills the I2S DMA buffer.
    // Recommended frequency: as often as possible (every loop iteration).
    // A missed call may cause a brief audio glitch.
    audio.loop();
}
