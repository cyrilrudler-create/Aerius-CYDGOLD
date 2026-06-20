#ifndef CYDGOLD_H
#define CYDGOLD_H

// =============================================================================
//  CYDGOLD.H — Public interface for CYD Gold hardware drivers
//
//  This file declares only the objects and functions DEFINED in cydgold.cpp.
//  It contains no implementations.
//
//  RULE: any file that needs hardware access (tft, ts, codec...) must
//  include this header — and only this header.
//
//  DEPENDENCIES:
//    - TFT_eSPI           : ILI9341 screen driver
//    - FT6336             : capacitive touch driver
//    - Config.h           : GPIO pins
//    - driver/i2c_master.h: i2c_master_dev_handle_t type (ESP-IDF)
// =============================================================================

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <FT6336.h>
#include "Config.h"
#include "driver/i2c_master.h"


// =============================================================================
//  GLOBAL OBJECTS
//
//  Defined once in cydgold.cpp.
//  The "extern" keyword tells the compiler these objects exist elsewhere
//  (avoids duplicate symbols if this header is included in multiple .cpp files).
//
//  tft: drives the ILI9341 TFT screen via SPI. Use tft.xxx() to draw
//       directly (outside LVGL) or as the LVGL backend.
//
//  ts : drives the FT6336G touch controller via I2C. Call ts.read()
//       then check ts.isTouched and ts.points[0].x / ts.points[0].y.
// =============================================================================
extern TFT_eSPI tft;  // ILI9341 TFT screen (SPI)
extern FT6336   ts;   // FT6336G capacitive touch (I2C)


// =============================================================================
//  TFT SCREEN
// =============================================================================

/**
 * @brief Initialises the ILI9341 TFT screen.
 *
 *  - Calls tft.init() to configure SPI and send the init sequence
 *  - tft.setRotation(1) -> landscape 320x240
 *  - tft.invertDisplay(true) -> visual correction for this IPS screen
 *  - Enables backlight (GPIO45 HIGH)
 *
 *  Called in init_cyd_hardware() AFTER I2S/codec init.
 */
void setup_display();


// =============================================================================
//  SD CARD
// =============================================================================

/**
 * @brief Mounts the microSD card via SD_MMC peripheral in 4-bit mode.
 *
 *  - Configures the 6 SD pins (CLK, CMD, D0...D3) via SD_MMC.setPins()
 *  - Mounts the FAT filesystem at mount point "/sd"
 *  - Prints the card capacity to serial
 *
 *  4-bit mode: uses 4 data lines instead of 1 -> faster.
 *  Requires that pins D1/D2/D3 are not used by anything else.
 *
 *  After this function, use SD_MMC.open("/file.mp3") etc.
 */
void setup_sd_card();


// =============================================================================
//  LVGL SD FILESYSTEM
// =============================================================================

/**
 * @brief Registers an LVGL filesystem driver pointing to the SD card.
 *
 *  Mount letter: 'S' -> LVGL paths are written as "S:/image.png"
 *
 *  Allows LVGL to load images, fonts, or other assets directly from the SD
 *  card without additional code.
 *
 *  Must be called AFTER setup_sd_card() AND lv_init().
 *  Called in init_cyd_lvgl().
 */
void lv_fs_sd_init();


// =============================================================================
//  ES8311 AUDIO CODEC
// =============================================================================

/**
 * @brief Initialises the I2C bus and configures the ES8311 codec via registers.
 *
 *  Does NOT depend on the es8311.h library — everything is inline in cydgold.cpp.
 *  Register values are pre-calculated for:
 *    MCLK = 44100 x 256 = 11,289,600 Hz
 *    Sample rate = 44100 Hz, 16-bit resolution, I2S slave mode
 *
 *  Sequence performed:
 *    1. Creates I2C master bus on I2C_NUM_1 (400 kHz)
 *    2. Adds ES8311 device (address 0x18)
 *    3. Soft-resets the codec
 *    4. Configures clocks (REG01 to REG08)
 *    5. Configures DAC/ADC, volumes, mic gain
 *
 *  IMPORTANT: call audio.setPinout() BEFORE this function because the ES8311
 *  needs the MCLK signal on GPIO4 to respond to I2C commands.
 *
 *  @return ESP_OK on success, ESP_FAIL if bus or codec does not respond.
 */
esp_err_t setup_i2c_and_codec();

/**
 * @brief Sets the DAC output volume of the ES8311 codec.
 *
 *  Writes directly to the DAC_VOL register (0x32) of the codec.
 *  Formula: reg = (vol * 256 / 100) - 1 for vol > 0, else 0.
 *
 *  @param vol Volume from 0 (mute) to 100 (maximum).
 *             Any out-of-range value is clamped by constrain().
 */
void codec_volume(int vol);

/**
 * @brief Returns the internal I2C handle of the ES8311 codec.
 *
 *  Used by external modules that need direct codec register access
 *  without going through the public functions in cydgold.cpp.
 *  Exposes the private handle s_codec_dev in a controlled way.
 *
 *  @return i2c_master_dev_handle_t handle to the ES8311.
 */
i2c_master_dev_handle_t get_codec_handle();


// =============================================================================
//  FT6336G CAPACITIVE TOUCH
// =============================================================================

/**
 * @brief Initialises the FT6336G touch controller.
 *
 *  - Calls ts.begin() for I2C communication
 *  - ts.setRotation(1) -> consistent with landscape screen rotation
 *
 *  After this function, touch data is available in the LVGL callback
 *  my_touchpad_read() (cyd_lvgl_drv.cpp).
 */
void setup_touch();


#endif // CYDGOLD_H
