// =============================================================================
//  CYDGOLD.CPP — Low-level hardware drivers for the CYD Gold board
//
//  This file contains the implementation of everything hardware-related:
//    - ILI9341 TFT screen (SPI via TFT_eSPI)
//    - SD card (SD_MMC 4-bit mode)
//    - LVGL filesystem driver for SD
//    - ES8311 audio codec (I2C, direct register control)
//    - FT6336G capacitive touch (I2C)
//
//  ES8311 CODEC — inline driver (no es8311.h library):
//    Register values are pre-calculated once for:
//      MCLK = 11,289,600 Hz (44100 x 256), sample = 44100 Hz, 16-bit
//    Source of coefficients: coeff_div[] table in Espressif es8311.cpp,
//    entry {11289600, 44100} -> pre_div=1, pre_multi=0, adc_div=1, dac_div=1,
//    fs_mode=0, lrck_h=0x00, lrck_l=0xFF, bclk_div=4, adc_osr=0x10, dac_osr=0x10
// =============================================================================

#include "cydgold.h"
#include "FS.h"
#include "SD_MMC.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include <lvgl.h>


// =============================================================================
//  GLOBAL OBJECTS
//  Defined here once. Other files access them via "extern" declared in cydgold.h.
// =============================================================================

// TFT driver — SPI pin configuration comes from Config.h
// (TFT_eSPI reads the TFT_MOSI, TFT_SCLK, etc. defines at compile time)
TFT_eSPI tft = TFT_eSPI();

// Touch driver — parameters: SDA, SCL, INT, RST, touch area width, height
FT6336 ts = FT6336(
    (int)I2C_SDA,   // GPIO16 — shared with ES8311 and I2C extension
    (int)I2C_SCL,   // GPIO15 — shared with ES8311 and I2C extension
    (int)TP_INT,    // GPIO17 — touch interrupt (active low)
    (int)TP_RST,    // GPIO18 — controller reset (active low)
    240,            // Touch area width (pixels, portrait mode)
    320             // Touch area height (pixels, portrait mode)
);


// =============================================================================
//  PRIVATE VARIABLES — visible only within this file (.cpp)
// =============================================================================

// Handle for the ES8311 device on the I2C bus
// Used by codec_write/read and exposed via get_codec_handle()
static i2c_master_dev_handle_t s_codec_dev;

// Handle for the I2C master bus (I2C_NUM_1)
static i2c_master_bus_handle_t s_i2c_bus;

// I2C address of the ES8311 (CE pin LOW -> 0x18, CE pin HIGH -> 0x19)
#define ES8311_ADDR  0x18


// =============================================================================
//  LOW-LEVEL I2C HELPERS — private, used only for the ES8311 codec
// =============================================================================

/**
 * @brief Writes one byte to an ES8311 codec register.
 * @param reg  Register address (0x00 to 0xFF)
 * @param val  Value to write
 * @return ESP_OK on success, ESP error code otherwise
 */
static esp_err_t codec_write(uint8_t reg, uint8_t val) {
    const uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_codec_dev, buf, 2, pdMS_TO_TICKS(100));
}

/**
 * @brief Reads one byte from an ES8311 codec register.
 * @param reg  Register address to read
 * @param val  Pointer to the result byte
 * @return ESP_OK on success, ESP error code otherwise
 */
static esp_err_t codec_read(uint8_t reg, uint8_t *val) {
    return i2c_master_transmit_receive(
        s_codec_dev, &reg, 1, val, 1, pdMS_TO_TICKS(100));
}


// =============================================================================
//  LVGL FILESYSTEM CALLBACKS FOR SD
//
//  These 5 functions implement the lv_fs_drv_t interface for LVGL.
//  LVGL calls them internally when loading a file (image, font...).
//  They must NEVER be called directly from application code.
//
//  Each open file is a fs::File* object allocated on the heap.
//  The void* file_p passed between callbacks is this pointer cast to void*.
// =============================================================================

/** Opens a file on the SD card. Returns nullptr on failure or if it's a directory. */
static void* sd_fs_open(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode) {
    fs::File* f = new fs::File();
    *f = SD_MMC.open(path, (mode == LV_FS_MODE_WR) ? FILE_WRITE : FILE_READ);
    if (!*f || f->isDirectory()) { delete f; return nullptr; }
    return static_cast<void*>(f);
}

/** Closes a file and frees its memory. */
static lv_fs_res_t sd_fs_close(lv_fs_drv_t* drv, void* file_p) {
    auto* f = static_cast<fs::File*>(file_p);
    f->close();
    delete f;
    return LV_FS_RES_OK;
}

/** Reads btr bytes from the current position. Writes the number of bytes read into *br. */
static lv_fs_res_t sd_fs_read(lv_fs_drv_t* drv, void* file_p,
                               void* buf, uint32_t btr, uint32_t* br) {
    *br = static_cast<fs::File*>(file_p)->read(static_cast<uint8_t*>(buf), btr);
    return LV_FS_RES_OK;
}

/** Moves the read cursor (SET from start, CUR from current position, END from end). */
static lv_fs_res_t sd_fs_seek(lv_fs_drv_t* drv, void* file_p,
                               uint32_t pos, lv_fs_whence_t whence) {
    auto* f = static_cast<fs::File*>(file_p);
    f->seek(pos, whence == LV_FS_SEEK_SET ? fs::SeekSet :
                (whence == LV_FS_SEEK_CUR ? fs::SeekCur : fs::SeekEnd));
    return LV_FS_RES_OK;
}

/** Returns the current read cursor position. */
static lv_fs_res_t sd_fs_tell(lv_fs_drv_t* drv, void* file_p, uint32_t* pos_p) {
    *pos_p = static_cast<fs::File*>(file_p)->position();
    return LV_FS_RES_OK;
}


// =============================================================================
//  PUBLIC IMPLEMENTATIONS
// =============================================================================

// -----------------------------------------------------------------------------
void setup_display() {
    tft.init();                  // Initialises SPI + ILI9341 init sequence
    tft.setRotation(3);          // Rotation 3 = landscape (320x240)
    tft.fillScreen(TFT_BLACK);   // Clear screen (avoids random pixel flash)
    tft.invertDisplay(true);     // Required for this IPS screen (otherwise colours inverted)

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);  // Turn on backlight
                                 // For PWM: ledcAttach(TFT_BL, freq, bits) (core 3.x)

    Serial.println("[DISPLAY] ILI9341 TFT screen initialised (320x240, landscape)");
}

// -----------------------------------------------------------------------------
void setup_sd_card() {
    // Configure the 6 SD_MMC bus pins
    if (!SD_MMC.setPins(SD_SCK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3)) {
        Serial.println("[SD] Error configuring SDMMC pins");
        return;
    }

    // Mount SD in 4-bit mode (false = not 1-bit mode)
    // Mount point "/sd" — files are accessible via "/file.mp3"
    if (!SD_MMC.begin("/sd", false)) {
        Serial.println("[SD] SD card not found or not FAT32 formatted!");
        return;
    }

    Serial.printf("[SD] Card initialised: %llu MB (4-bit mode)\n",
                  SD_MMC.cardSize() / (1024 * 1024));
}

// -----------------------------------------------------------------------------
void lv_fs_sd_init() {
    static lv_fs_drv_t drv;     // static: must survive after this function returns
    lv_fs_drv_init(&drv);

    drv.letter   = 'S';         // LVGL prefix: "S:/image.png", "S:/font.bin"...
    drv.ready_cb = nullptr;     // No "ready" callback (SD always mounted)
    drv.open_cb  = sd_fs_open;
    drv.close_cb = sd_fs_close;
    drv.read_cb  = sd_fs_read;
    drv.seek_cb  = sd_fs_seek;
    drv.tell_cb  = sd_fs_tell;

    lv_fs_drv_register(&drv);
    Serial.println("[LVGL] SD filesystem driver registered (prefix 'S:/')");
}

// -----------------------------------------------------------------------------
esp_err_t setup_i2c_and_codec() {

    // ── I2C master bus ────────────────────────────────────────────────────────
    // Using I2C_NUM_1 to leave I2C_NUM_0 available if needed.
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port   = I2C_NUM_1;
    bus_cfg.sda_io_num = (gpio_num_t)I2C_SDA;   // GPIO16
    bus_cfg.scl_io_num = (gpio_num_t)I2C_SCL;   // GPIO15
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;   // System clock

    if (i2c_new_master_bus(&bus_cfg, &s_i2c_bus) != ESP_OK) {
        Serial.println("[CODEC] Failed to create I2C bus");
        return ESP_FAIL;
    }

    // ── ES8311 device ─────────────────────────────────────────────────────────
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;  // 7-bit address
    dev_cfg.device_address  = ES8311_ADDR;          // 0x18
    dev_cfg.scl_speed_hz    = 400000;               // 400 kHz (fast mode)

    if (i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_codec_dev) != ESP_OK) {
        Serial.println("[CODEC] Failed to add ES8311 to I2C bus");
        return ESP_FAIL;
    }

    // ── ES8311 codec register initialisation ──────────────────────────────────
    //
    //  Direct register control without the Espressif es8311.h library
    //  (saves ~3 KB of flash and removes dependencies).
    //
    //  Coefficients for MCLK=11,289,600 Hz, sample=44100 Hz (es8311.cpp table):
    //    pre_div=1, pre_multi=0 -> REG02 = 0x00
    //    adc_osr=0x10           -> REG03 = 0x10
    //    dac_osr=0x10           -> REG04 = 0x10
    //    adc_div=1, dac_div=1   -> REG05 = 0x00
    //    bclk_div=4 (<19)->(4-1)=3 -> REG06 = 0x03
    //    lrck_h=0x00            -> REG07 = 0x00
    //    lrck_l=0xFF            -> REG08 = 0xFF

    // Soft reset (0x1F = reset all, 0x00 = end reset)
    codec_write(0x00, 0x1F);
    vTaskDelay(pdMS_TO_TICKS(20));
    codec_write(0x00, 0x00);
    codec_write(0x00, 0x80);    // Power-on (bit7=1)

    // Clock: MCLK from MCLK pin, all internal clocks enabled
    codec_write(0x01, 0x3F);    // REG01: 0x3F = enable all, MCLK pin selected

    // Clock dividers (calculated for 44100 Hz from MCLK=11,289,600 Hz)
    codec_write(0x02, 0x00);    // REG02: pre_div=1, pre_multi=0
    codec_write(0x03, 0x10);    // REG03: adc_osr=16 (0x10)
    codec_write(0x04, 0x10);    // REG04: dac_osr=16 (0x10)
    codec_write(0x05, 0x00);    // REG05: adc_div=1, dac_div=1
    codec_write(0x06, 0x03);    // REG06: bclk_div=4 -> register value=3
    codec_write(0x07, 0x00);    // REG07: lrck_h=0 (high byte of LRCK divider)
    codec_write(0x08, 0xFF);    // REG08: lrck_l=255 (low byte -> 44100 Hz)

    // Exit power-down: bit6 of REG00 must be 0
    uint8_t reg00 = 0;
    codec_read(0x00, &reg00);
    codec_write(0x00, reg00 & 0xBF);   // Force bit6=0 (normal operation)

    // I2S format: 16-bit, standard mode (MSB first)
    codec_write(0x09, 0x0C);    // REG09: SDP In  — 16-bit resolution (bits[3:2]=11)
    codec_write(0x0A, 0x0C);    // REG0A: SDP Out — 16-bit resolution

    // Analog block power supply
    codec_write(0x0D, 0x01);    // REG0D: power-up analog (bit0=1)
    codec_write(0x0E, 0x02);    // REG0E: enable PGA + ADC modulator

    // DAC: speaker output
    codec_write(0x12, 0x00);    // REG12: power-up DAC (0x00=on, 0xFF=off — inverted logic!)
    codec_write(0x13, 0x10);    // REG13: enable HP output driver

    // Filters and correction
    codec_write(0x1C, 0x6A);    // REG1C: ADC equalizer bypass + DC offset cancellation
    codec_write(0x37, 0x08);    // REG37: DAC ramp rate (anti-pop)

    // DAC volume: 90% -> formula: (90 * 256 / 100) - 1 = 229 = 0xE5
    codec_write(0x32, 0xE5);    // REG32: DAC volume (0x00=mute, 0xFF=max)

    // Analog microphone
    codec_write(0x14, 0x1A);    // REG14: enable analog MIC + max PGA gain (0x1A)
    codec_write(0x16, 0x07);    // REG16: ADC digital gain = 42 dB (value 7)
    codec_write(0x17, 0xA8);    // REG17: ADC volume

    Serial.println("[CODEC] ES8311 initialised (44100 Hz, 16-bit, inline driver)");
    return ESP_OK;
}

// -----------------------------------------------------------------------------
void codec_volume(int vol) {
    vol = constrain(vol, 0, 100);
    // Formula from es8311_voice_volume_set():
    //   register = (vol * 256 / 100) - 1  for vol > 0
    //   register = 0                       for vol = 0 (full mute)
    uint8_t reg32 = (vol == 0) ? 0 : (uint8_t)((vol * 256 / 100) - 1);
    codec_write(0x32, reg32);
}

// -----------------------------------------------------------------------------
i2c_master_dev_handle_t get_codec_handle() {
    // Exposes the private s_codec_dev handle for modules that need
    // direct codec access.
    return s_codec_dev;
}

// -----------------------------------------------------------------------------
void setup_touch() {
    ts.begin();          // Initialises I2C communication with FT6336G
    ts.setRotation(3);   // Rotation consistent with tft.setRotation(1) (landscape)
    Serial.println("[TOUCH] FT6336G touch controller initialised");
}
