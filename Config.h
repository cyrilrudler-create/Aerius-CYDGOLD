#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
//  CONFIG.H — All GPIO pins and hardware constants
//
//  PRINCIPLE: this file is the single source of truth for wiring.
//  To port this project to another board, modify ONLY this file.
//
//  ┌─────────────────────────────────────────────────────┐
//  │  SECTION 1 — USER SETTINGS (edit these)             │
//  │  SECTION 2 — FIXED HARDWARE (do not modify)         │
//  └─────────────────────────────────────────────────────┘
// =============================================================================

#include <Arduino.h>

// =============================================================================
//  ╔══════════════════════════════════════════════════════════════════════════╗
//  ║          SECTION 1 — USER SETTINGS                                       ║
//  ╚══════════════════════════════════════════════════════════════════════════╝
// =============================================================================

// =============================================================================
//  WIFI — Network credentials
// =============================================================================
#define WIFI_SSID   "YourSSID"
#define WIFI_PASS   "YourPassword"

// =============================================================================
//  MQTT — Home Assistant integration
//
//  TO ENABLE : uncomment MQTT_BROKER and fill in your values.
//  TO DISABLE : leave commented — no impact on the rest of the project.
//  REQUIRED LIBRARY if enabled: "PubSubClient" by Nick O'Leary
// =============================================================================
#define MQTT_BROKER  "192.168.1.x"    // IP or hostname of your broker
#define MQTT_PORT    1883
#define MQTT_USER    "your_mqtt_user"
#define MQTT_PASS    "your_mqtt_password"

// =============================================================================
//  LD2410C — Presence sensor
//
//  LD2410_MAX_GATE    : maximum detection range (1 gate = 0.75 m)
//                       1=0.75m  2=1.5m  3=2.25m  4=3m
//  LD2410_ABSENCE_DELAY : seconds before fan stops when no presence detected
// =============================================================================
#define LD2410_MAX_GATE      2       // range 1.5 m by default
#define LD2410_ABSENCE_DELAY 60      // 60 s before stop

// =============================================================================
//  AUTO MODE — Temperature thresholds
//
//  AUTO_TEMP_ON   : temperature at which fan starts
//  AUTO_SPEED_MIN/MAX : speed range in auto mode (%)
//  Speed = AUTO_SPEED_MIN + (excess °C x 5%) up to AUTO_SPEED_MAX
// =============================================================================
#define AUTO_TEMP_ON     26.0f  // °C
#define AUTO_SPEED_MIN   30     // %
#define AUTO_SPEED_MAX   80     // %

// =============================================================================
//  NTP — Time server by country
//
//  Uncomment ONE line matching your country.
//  GMT_OFFSET : offset in seconds (UTC+1 = 3600)
//  DST_OFFSET : 3600 for daylight saving time, 0 otherwise
// =============================================================================

// ── France ───────────────────────────────────────────────────────────────────
#define NTP_SERVER   "fr.pool.ntp.org"
#define GMT_OFFSET   3600
#define DST_OFFSET   3600

// ── Belgium ──────────────────────────────────────────────────────────────────
// #define NTP_SERVER   "be.pool.ntp.org"
// #define GMT_OFFSET   3600
// #define DST_OFFSET   3600

// ── Switzerland ──────────────────────────────────────────────────────────────
// #define NTP_SERVER   "ch.pool.ntp.org"
// #define GMT_OFFSET   3600
// #define DST_OFFSET   3600

// ── Luxembourg ───────────────────────────────────────────────────────────────
// #define NTP_SERVER   "europe.pool.ntp.org"
// #define GMT_OFFSET   3600
// #define DST_OFFSET   3600

// ── Spain ────────────────────────────────────────────────────────────────────
// #define NTP_SERVER   "es.pool.ntp.org"
// #define GMT_OFFSET   3600
// #define DST_OFFSET   3600

// ── Italy ────────────────────────────────────────────────────────────────────
// #define NTP_SERVER   "it.pool.ntp.org"
// #define GMT_OFFSET   3600
// #define DST_OFFSET   3600

// ── Germany ──────────────────────────────────────────────────────────────────
// #define NTP_SERVER   "de.pool.ntp.org"
// #define GMT_OFFSET   3600
// #define DST_OFFSET   3600

// ── Netherlands ──────────────────────────────────────────────────────────────
// #define NTP_SERVER   "nl.pool.ntp.org"
// #define GMT_OFFSET   3600
// #define DST_OFFSET   3600

// ── Portugal ─────────────────────────────────────────────────────────────────
// #define NTP_SERVER   "pt.pool.ntp.org"
// #define GMT_OFFSET   0
// #define DST_OFFSET   3600

// ── United Kingdom ───────────────────────────────────────────────────────────
// #define NTP_SERVER   "uk.pool.ntp.org"
// #define GMT_OFFSET   0
// #define DST_OFFSET   3600

// ── Poland ───────────────────────────────────────────────────────────────────
// #define NTP_SERVER   "pl.pool.ntp.org"
// #define GMT_OFFSET   3600
// #define DST_OFFSET   3600

// ── Greece / Cyprus ──────────────────────────────────────────────────────────
// #define NTP_SERVER   "gr.pool.ntp.org"
// #define GMT_OFFSET   7200
// #define DST_OFFSET   3600

// ── Finland / Estonia / Latvia / Lithuania ───────────────────────────────────
// #define NTP_SERVER   "fi.pool.ntp.org"
// #define GMT_OFFSET   7200
// #define DST_OFFSET   3600

// =============================================================================
//  ╔══════════════════════════════════════════════════════════════════════════╗
//  ║          SECTION 2 — FIXED HARDWARE (do not modify)                      ║
//  ╚══════════════════════════════════════════════════════════════════════════╝
// =============================================================================

// =============================================================================
//  TFT SCREEN — ILI9341V, 4-wire SPI interface
// =============================================================================
#define TFT_W        240
#define TFT_H        320
#define TFT_MOSI     11
#define TFT_SCLK     12
#define TFT_CS       10
#define TFT_DC       46
#define TFT_RST      -1
#define TFT_MISO     13
#define TFT_BL       45
#define TFT_BL_ON    HIGH

// =============================================================================
//  CAPACITIVE TOUCH — FT6336G, I2C interface
// =============================================================================
#define TP_INT       GPIO_NUM_17
#define TP_RST       GPIO_NUM_18

// =============================================================================
//  I2C BUS — FT6336, ES8311, BME280, extension connector
// =============================================================================
#define I2C_SDA      GPIO_NUM_16
#define I2C_SCL      GPIO_NUM_15

// =============================================================================
//  SD CARD — SD_MMC 4-bit mode
// =============================================================================
#define SD_SCK       GPIO_NUM_38
#define SD_CMD       GPIO_NUM_40
#define SD_D0        GPIO_NUM_39
#define SD_D1        GPIO_NUM_41
#define SD_D2        GPIO_NUM_48
#define SD_D3        GPIO_NUM_47

// =============================================================================
//  AUDIO I2S — ES8311 codec
//  GPIO kept for reference — audio not used in this project
// =============================================================================
#define I2S_MCK      GPIO_NUM_4
#define I2S_BCK      GPIO_NUM_5
#define I2S_WS       GPIO_NUM_7
#define I2S_DOUT     GPIO_NUM_8
#define I2S_DIN      GPIO_NUM_6

// =============================================================================
//  AUDIO AMPLIFIER
//  GPIO kept for reference — audio not used in this project
// =============================================================================
#define AMP_EN       GPIO_NUM_1

// =============================================================================
//  BATTERY — voltage reading via ADC
// =============================================================================
#define BAT_ADC_PIN  9

// =============================================================================
//  FAN — 4-wire brushless PWM
// =============================================================================
#define FAN_PWM_GPIO       21
#define FAN_PWM_CHANNEL    LEDC_CHANNEL_0
#define FAN_PWM_FREQ       25000
#define FAN_PWM_RESOLUTION LEDC_TIMER_8_BIT

// =============================================================================
//  FAN — Tachometer RPM signal
//  2 pulses per revolution, FALLING edge interrupt
// =============================================================================
#define FAN_TACHY_GPIO   GPIO_NUM_14

// =============================================================================
//  BME280 — Temperature / humidity / pressure sensor
//  I2C address: 0x76 if SDO tied to GND, 0x77 if SDO tied to VCC
// =============================================================================
#define BME280_ADDR  0x76

// =============================================================================
//  ARGB LEDs — WS2812B fan + ring
// =============================================================================
#define LED_FAN_GPIO    3
#define LED_FAN_COUNT   12
#define LED_RING_GPIO   2
#define LED_RING_COUNT  36

// Maximum brightness allowed in pure white (R+G+B = max) to limit current draw
// 150/255 = ~60% -> approx 36 mA/LED instead of 60 mA
#define LED_WHITE_BRIGHT_MAX  150

// =============================================================================
//  LD2410C — UART GPIOs (fixed — board UART connector)
//  Sensor TX -> GPIO44, Sensor RX -> GPIO43
// =============================================================================
#define LD2410_TX_GPIO   GPIO_NUM_43
#define LD2410_RX_GPIO   GPIO_NUM_44
#define LD2410_BAUDRATE  256000
#define LD2410_UART      Serial1

#endif // CONFIG_H
