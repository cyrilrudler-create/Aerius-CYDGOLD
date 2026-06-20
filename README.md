# Aerius Gold — ARGB PWM Fan Controller
## CYD Gold ESP32-S3 · LVGL 8 · 3-page Dark Luxury UI

---

## Project overview

Aerius Gold is a smart desktop fan controller built on the **CYD Gold** board (ESP32-S3).  
It features a 2.8" IPS touchscreen, ARGB LED control, presence detection, Home Assistant integration via MQTT, and a BME280 temperature sensor.

---

## File structure

| File | Role |
|---|---|
| `cydgold_ventilateur.ino` | Arduino entry point |
| `cyd_lvgl_drv.h/cpp` | LVGL engine + hardware orchestration |
| `cydgold.h/cpp` | Low-level CYD Gold hardware drivers |
| `ui_shared.h/cpp` | Colour palette, shared styles, global state, fan PWM, navbar |
| `ui_home.h/cpp` | Dashboard page (speed arc, ON/OFF, temperature, timer) |
| `ui_leds.h/cpp` | LED effects page (10 effects, 8 colour presets) |
| `ui_info.h/cpp` | Info/Credits page (scrolling credits + live system data) |
| `ui_mqtt.h/cpp` | Home Assistant MQTT integration |
| `ui_presence.h/cpp` | LD2410C presence sensor |
| `Config.h` | All GPIO pins and user settings |
| `logo.c` | CRTeck logo embedded in flash |

---

## Required libraries (Arduino IDE)

Install via Library Manager:

1. **Adafruit BME280 Library** (by Adafruit) — v2.2.4+
2. **Adafruit NeoPixel** (by Adafruit) — v1.12+
3. **Adafruit Unified Sensor** (automatic dependency)
4. **PubSubClient** (by Nick O'Leary) — v2.8+ *(only if MQTT is enabled)*
5. **ld2410** (by ncmreynolds) — v0.2.2+

Already present in most CYD Gold setups:
- TFT_eSPI
- LVGL 8.x
- FT6336
- ESP32-audioI2S *(library kept for hardware compatibility — audio not used)*

---

## Configuration

Edit **`Config.h`** before flashing:

```cpp
// WiFi credentials
#define WIFI_SSID   "YourSSID"
#define WIFI_PASS   "YourPassword"

// MQTT — comment out to disable entirely
#define MQTT_BROKER  "192.168.1.x"
#define MQTT_PORT    1883
#define MQTT_USER    "your_mqtt_user"
#define MQTT_PASS    "your_mqtt_password"

// NTP — uncomment your country
#define NTP_SERVER   "fr.pool.ntp.org"
#define GMT_OFFSET   3600
#define DST_OFFSET   3600
```

---

## GPIO pinout

| GPIO | Function | Detail |
|---|---|---|
| 21 | Fan PWM | 25 kHz brushless, 8-bit, LEDC ch.0 |
| 14 | Fan tachometer | RPM, FALLING edge interrupt, 2 pulses/rev |
| 2 | Fan LEDs | 12× WS2812B, 800 kHz |
| 3 | Ring LEDs | 36× WS2812B, 800 kHz |
| 15 | I2C SCL | BME280 + FT6336G + ES8311 |
| 16 | I2C SDA | BME280 + FT6336G + ES8311 |
| 43 | LD2410C TX | Sensor RX |
| 44 | LD2410C RX | Sensor TX |
| 11/12/10/46/13 | TFT SPI | ILI9341 |
| 45 | TFT Backlight | HIGH = on |
| 17/18 | Touch INT/RST | FT6336G |
| 38/40/39/41/48/47 | SD_MMC | 4-bit mode |

*Audio GPIOs (4, 5, 6, 7, 8, 1) are defined in Config.h for reference — audio is not used in this build.*

---

## UI Pages

### Page 0 — Dashboard
- **Speed arc**: drag to set 0–100 %
- **ON/OFF button**: starts / stops the fan (gold = ON, dark = OFF)
- **Temperature card**: BME280 reading (white < 28°C, gold 28–35°C, red > 35°C)
- **AUTO button**: auto mode — fan speed tracks temperature via LD2410C presence
- **Timer**: `+` / `−` buttons add/remove 5 min, `RST` cancels
- **RPM display**: live tachometer reading
- **NTP clock + WiFi status icon**

### Page 1 — LED Effects
- **10 effects**: OFF / STATIC / BREATH / SPIN / RAINBOW / FIRE / VORTEX / PULSE / TWINKLE / WAVE
- **Brightness slider**: 0–255 (capped at 150 for pure white to limit current)
- **8 colour presets**: Gold · Pearl White · Ice Blue · Mint Green · Red · Orange · Violet · Yellow

### Page 2 — Info / Credits
- CRTeck logo + project name
- Auto-scrolling credits with live data: free RAM, free PSRAM, CPU temperature, uptime

---

## MQTT Home Assistant integration

When `MQTT_BROKER` is defined, the device auto-discovers itself in Home Assistant.

**Entities created automatically:**
- Fan speed (Number, 0–100 %)
- Fan switch (ON/OFF)
- Fan timer (Number, seconds)
- LEDs (Light entity — colour, brightness, effect)
- Temperature (Sensor, °C)
- Humidity (Sensor, %)
- CPU Temperature (Sensor, °C)
- Fan RPM (Sensor)
- Presence (Binary sensor, occupancy)
- Presence distance (Sensor, cm)

**To disable MQTT entirely:** comment out `#define MQTT_BROKER` in `Config.h`. The project compiles without PubSubClient and MQTT has zero runtime overhead.

---

## Auto mode (presence + temperature)

When AUTO is active:
- Fan starts only if **presence is detected** AND **temperature > AUTO_TEMP_ON**
- Speed scales linearly: `AUTO_SPEED_MIN + (excess °C × 5%)`, capped at `AUTO_SPEED_MAX`
- Fan stops after `LD2410_ABSENCE_DELAY` seconds of no presence

All thresholds are configurable in `Config.h`.

---

## Notes

### BME280 I2C address
Default address is `0x76` (SDO tied to GND). If your module uses `0x77` (SDO tied to VCC), change `BME280_ADDR` in `Config.h`.

### Fan tachometer
Standard PC fans output 2 pulses per revolution. If your fan reads double the expected RPM, change `TACHY_PULSES_PER_REV` to `4` in `ui_home.cpp`.

### WS2812B on GPIO2/GPIO3
These are strapping pins on ESP32-S3. If the board does not boot with LEDs connected, add a 10 kΩ pull-down resistor on each pin, or power the LEDs after boot (already the case here — `begin()` is called in `ui_leds_init_hw()`).

### Pure white brightness cap
To prevent excessive current draw when using pure white (R+G+B = max), brightness is automatically capped at `LED_WHITE_BRIGHT_MAX` (150/255 ≈ 60%, ~36 mA/LED instead of 60 mA).

---

## Hardware

- **Print**: https://cults3d.com/fr/mod%C3%A8le-3d/maison/aerius-gold-ventilateur-connecte-argb-esp32-s3-ecran-tactile-home-assistan
- **Board**: ESP32-S3 ESP32 S3 Display CYD 2.8 inch IPS Capacitive Touch Screen 240x320 Pixel
- **Screen**: ILI9341V 2.8" IPS 320×240
- **Touch**: FT6336G capacitive
- **Audio codec**: ES8311 *(GPIOs kept, not used in this build)*
- **Temperature**: BME280
- **LEDs**: WS2812B (12 fan + 36 ring)
- **Fan**: 4-wire brushless, 25 kHz PWM
- **Presence**: LD2410C mmWave radar
- **Power**: XL6009 DC-DC 

---

*Designed & built by CRTeck — 2026*
