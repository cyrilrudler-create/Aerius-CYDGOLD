// =============================================================================
//  UI_MQTT.CPP — Home Assistant integration via MQTT (PubSubClient)
//
//  REQUIRED LIBRARY:
//    "PubSubClient" by Nick O'Leary — install via Arduino Library Manager
//
//  NON-BLOCKING: if the broker is absent, mqtt_tick() returns in <1 ms
//  Reconnection is attempted every MQTT_RECONNECT_INTERVAL ms
// =============================================================================

#include "ui_mqtt.h"
#include "ui_shared.h"
#include "ui_home.h"
#include "ui_leds.h"
#include "ui_presence.h"
#include "Config.h"
#include <WiFi.h>

// =============================================================================
//  CONFIG CHECK
//  If MQTT_BROKER is not defined in Config.h -> empty stubs are compiled
//  PubSubClient.h is included ONLY if MQTT_BROKER is defined
//  -> no need to install the library if MQTT is disabled
// =============================================================================
#ifndef MQTT_BROKER
// Empty stubs — MQTT disabled, no impact on the rest of the project
void mqtt_init()              {}
void mqtt_tick()              {}
void mqtt_publish_fan_state() {}
void mqtt_publish_led_state() {}
void mqtt_publish_sensors()   {}
#else

// =============================================================================
//  FULL MQTT IMPLEMENTATION
//  PubSubClient included here only — library required only if enabled
// =============================================================================
#include <PubSubClient.h>

#ifndef MQTT_PORT
  #define MQTT_PORT  1883
#endif
#ifndef MQTT_USER
  #define MQTT_USER  ""
#endif
#ifndef MQTT_PASS
  #define MQTT_PASS  ""
#endif

// ── Topics ────────────────────────────────────────────────────────────────────
#define T_FAN_SPEED_CMD    "aerius/fan/speed/set"
#define T_FAN_SPEED_STATE  "aerius/fan/speed/state"
#define T_FAN_SW_CMD       "aerius/fan/switch/set"
#define T_FAN_SW_STATE     "aerius/fan/switch/state"
#define T_FAN_TIMER_CMD    "aerius/fan/timer/set"
#define T_FAN_TIMER_STATE  "aerius/fan/timer/state"
#define T_LED_EFFECT_CMD   "aerius/led/effect/set"
#define T_LED_EFFECT_STATE "aerius/led/effect/state"
#define T_LED_COLOR_CMD    "aerius/led/color/set"
#define T_LED_BRI_CMD      "aerius/led/brightness/set"
#define T_LED_BRI_STATE    "aerius/led/brightness/state"
#define T_SENSOR_TEMP      "aerius/sensor/temperature"
#define T_SENSOR_HUMI      "aerius/sensor/humidity"
#define T_SENSOR_CPU       "aerius/sensor/cpu_temp"
#define T_SYS_IP           "aerius/system/ip"
#define T_SYS_RSSI         "aerius/system/rssi"
#define T_SYS_UPTIME       "aerius/system/uptime"
#define T_FAN_RPM          "aerius/fan/rpm"
#define T_PRESENCE         "aerius/sensor/presence"
#define T_PRESENCE_DIST    "aerius/sensor/presence_distance"
#define T_LIGHT_CMD        "aerius/light/set"
#define T_LIGHT_STATE      "aerius/light/state"

// ── Intervals ───────────────────────────────────────────────────────────────
#define MQTT_RECONNECT_INTERVAL  10000   // ms between reconnect attempts
#define MQTT_SENSOR_INTERVAL     5000    // ms between sensor publishes

// ── Objects ────────────────────────────────────────────────────────────────────
static WiFiClient   s_wifi_client;
static PubSubClient s_mqtt(s_wifi_client);
static uint32_t     s_last_reconnect_ms = 0;
static uint32_t     s_last_sensor_ms    = 0;
static bool         s_discovery_done    = false;

// =============================================================================
//  HOME ASSISTANT AUTO-DISCOVERY
//  Publishes each entity config once at startup
//  HA automatically detects all entities in its interface
// =============================================================================
static void publish_discovery() {
    // ── Fan: Number (speed 0-100) ────────────────────────────────────────────
    s_mqtt.publish(
        "homeassistant/number/aerius_fan_speed/config",
        "{\"name\":\"Fan Speed\","
        "\"unique_id\":\"aerius_fan_speed\","
        "\"command_topic\":\"" T_FAN_SPEED_CMD "\","
        "\"state_topic\":\"" T_FAN_SPEED_STATE "\","
        "\"min\":0,\"max\":100,\"step\":5,"
        "\"unit_of_measurement\":\"%\","
        "\"icon\":\"mdi:fan\","
        "\"device\":{\"identifiers\":[\"aerius_gold\"],"
        "\"name\":\"Aerius Gold\",\"model\":\"CYD Gold ESP32-S3\","
        "\"manufacturer\":\"CRTeck\"}}",
        true  // retain
    );

    // ── Fan: Switch (ON/OFF) ─────────────────────────────────────────────────
    s_mqtt.publish(
        "homeassistant/switch/aerius_fan_switch/config",
        "{\"name\":\"Fan\","
        "\"unique_id\":\"aerius_fan_switch\","
        "\"command_topic\":\"" T_FAN_SW_CMD "\","
        "\"state_topic\":\"" T_FAN_SW_STATE "\","
        "\"icon\":\"mdi:fan\","
        "\"device\":{\"identifiers\":[\"aerius_gold\"]}}",
        true
    );

    // ── Timer: Number ────────────────────────────────────────────────────────
    s_mqtt.publish(
        "homeassistant/number/aerius_fan_timer/config",
        "{\"name\":\"Fan Timer\","
        "\"unique_id\":\"aerius_fan_timer\","
        "\"command_topic\":\"" T_FAN_TIMER_CMD "\","
        "\"state_topic\":\"" T_FAN_TIMER_STATE "\","
        "\"min\":0,\"max\":7200,\"step\":300,"
        "\"unit_of_measurement\":\"s\","
        "\"icon\":\"mdi:timer\","
        "\"device\":{\"identifiers\":[\"aerius_gold\"]}}",
        true
    );

    // ── LEDs: Light entity (colour + brightness + effect) ──────────────────
    s_mqtt.publish(
        "homeassistant/light/aerius_leds/config",
        "{\"name\":\"LEDs\","
        "\"unique_id\":\"aerius_leds\","
        "\"schema\":\"json\","
        "\"command_topic\":\"" T_LIGHT_CMD "\","
        "\"state_topic\":\"" T_LIGHT_STATE "\","
        "\"brightness\":true,"
        "\"brightness_scale\":255,"
        "\"color_mode\":true,"
        "\"supported_color_modes\":[\"rgb\"],"
        "\"effect\":true,"
        "\"effect_list\":[\"OFF\",\"STATIC\",\"BREATH\",\"SPIN\",\"RAINBOW\","
                        "\"FIRE\",\"METEOR\",\"PULSE\",\"TWINKLE\",\"WAVE\"],"
        "\"icon\":\"mdi:led-strip-variant\","
        "\"device\":{\"identifiers\":[\"aerius_gold\"]}}",
        true
    );

    // ── BME280 temperature ───────────────────────────────────────────────────
    s_mqtt.publish(
        "homeassistant/sensor/aerius_temperature/config",
        "{\"name\":\"Temperature\","
        "\"unique_id\":\"aerius_temperature\","
        "\"state_topic\":\"" T_SENSOR_TEMP "\","
        "\"unit_of_measurement\":\"°C\","
        "\"device_class\":\"temperature\","
        "\"state_class\":\"measurement\","
        "\"device\":{\"identifiers\":[\"aerius_gold\"]}}",
        true
    );

    // ── BME280 humidity ──────────────────────────────────────────────────────
    s_mqtt.publish(
        "homeassistant/sensor/aerius_humidity/config",
        "{\"name\":\"Humidity\","
        "\"unique_id\":\"aerius_humidity\","
        "\"state_topic\":\"" T_SENSOR_HUMI "\","
        "\"unit_of_measurement\":\"%\","
        "\"device_class\":\"humidity\","
        "\"state_class\":\"measurement\","
        "\"device\":{\"identifiers\":[\"aerius_gold\"]}}",
        true
    );

    // ── ESP32 CPU temperature ────────────────────────────────────────────────
    s_mqtt.publish(
        "homeassistant/sensor/aerius_cpu_temp/config",
        "{\"name\":\"CPU Temperature\","
        "\"unique_id\":\"aerius_cpu_temp\","
        "\"state_topic\":\"" T_SENSOR_CPU "\","
        "\"unit_of_measurement\":\"°C\","
        "\"device_class\":\"temperature\","
        "\"state_class\":\"measurement\","
        "\"icon\":\"mdi:chip\","
        "\"device\":{\"identifiers\":[\"aerius_gold\"]}}",
        true
    );

    // ── Fan RPM ──────────────────────────────────────────────────────────────
    s_mqtt.publish(
        "homeassistant/sensor/aerius_fan_rpm/config",
        "{\"name\":\"Fan RPM\","
        "\"unique_id\":\"aerius_fan_rpm\","
        "\"state_topic\":\"" T_FAN_RPM "\","
        "\"unit_of_measurement\":\"RPM\","
        "\"state_class\":\"measurement\","
        "\"icon\":\"mdi:fan\","
        "\"device\":{\"identifiers\":[\"aerius_gold\"]}}",
        true
    );

    // ── LD2410C presence ─────────────────────────────────────────────────────
    s_mqtt.publish(
        "homeassistant/binary_sensor/aerius_presence/config",
        "{\"name\":\"Presence\","
        "\"unique_id\":\"aerius_presence\","
        "\"state_topic\":\"" T_PRESENCE "\","
        "\"payload_on\":\"ON\","
        "\"payload_off\":\"OFF\","
        "\"device_class\":\"occupancy\","
        "\"icon\":\"mdi:motion-sensor\","
        "\"device\":{\"identifiers\":[\"aerius_gold\"]}}",
        true
    );

    s_mqtt.publish(
        "homeassistant/sensor/aerius_presence_dist/config",
        "{\"name\":\"Presence Distance\","
        "\"unique_id\":\"aerius_presence_dist\","
        "\"state_topic\":\"" T_PRESENCE_DIST "\","
        "\"unit_of_measurement\":\"cm\","
        "\"icon\":\"mdi:ruler\","
        "\"state_class\":\"measurement\","
        "\"device\":{\"identifiers\":[\"aerius_gold\"]}}",
        true
    );

    Serial.println("[MQTT] Auto-discovery published OK");
    s_discovery_done = true;
}

// =============================================================================
//  INCOMING MESSAGE CALLBACK
// =============================================================================
static void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    // Copy payload into a null-terminated buffer
    char msg[32];
    uint8_t len = min((unsigned int)31, length);
    memcpy(msg, payload, len);
    msg[len] = '\0';

    // ── Fan speed ────────────────────────────────────────────────────────────
    if (strcmp(topic, T_FAN_SPEED_CMD) == 0) {
        int spd = constrain((int)atof(msg), 0, 100);
        g_fan_speed = (uint8_t)spd;
        if (g_fan_running) fan_set_speed(g_fan_speed);
        ui_home_update_fan_visual();
        mqtt_publish_fan_state();
        Serial.printf("[MQTT] Fan speed → %d%%\n", spd);
    }

    // ── Fan ON/OFF ───────────────────────────────────────────────────────────
    else if (strcmp(topic, T_FAN_SW_CMD) == 0) {
        // HA may send ON/OFF or on/off depending on version
        if (strcasecmp(msg, "ON") == 0)  fan_start();
        else                              fan_stop();
        ui_home_update_fan_visual();
        mqtt_publish_fan_state();
        Serial.printf("[MQTT] Fan switch → %s\n", msg);
    }

    // ── Timer ────────────────────────────────────────────────────────────────
    else if (strcmp(topic, T_FAN_TIMER_CMD) == 0) {
        g_timer_secs = (uint32_t)max(0, atoi(msg));
        g_timer_start = (g_timer_secs > 0 && g_fan_running) ? millis() : 0;
        Serial.printf("[MQTT] Timer → %lus\n", g_timer_secs);
    }

    // ── LED effect (HA sends the name: "OFF", "SPIN", etc.) ─────────────────
    else if (strcmp(topic, T_LED_EFFECT_CMD) == 0) {
        // Conversion nom → index
        static const char* names[] = {
            "OFF","STATIC","BREATH","SPIN","RAINBOW",
            "FIRE","VORTEX","PULSE","TWINKLE","WAVE"
        };
        uint8_t idx = g_led_effect;  // keep current value if name unknown
        for (int i = 0; i < 10; i++) {
            if (strcasecmp(msg, names[i]) == 0) { idx = i; break; }
        }
        g_led_effect = idx;
        ui_leds_update_visual();
        mqtt_publish_led_state();
        Serial.printf("[MQTT] LED effect → %s (%d)\n", msg, g_led_effect);
    }

    // ── Light JSON command (colour + brightness + effect) ────────────────────
    // HA sends: {"state":"ON","brightness":150,"color":{"r":201,"g":168,"b":76},"effect":"SPIN"}
    else if (strcmp(topic, T_LIGHT_CMD) == 0) {
        // state ON/OFF
        if (strstr(msg, "\"OFF\""))  { fan_stop(); }  // OFF = turn off LEDs
        // brightness
        char* bri_p = strstr(msg, "\"brightness\":");
        if (bri_p) {
            int bri = atoi(bri_p + 13);
            uint8_t r = (g_led_color >> 16) & 0xFF;
            uint8_t g = (g_led_color >> 8)  & 0xFF;
            uint8_t b =  g_led_color        & 0xFF;
            bool is_white = (r > 0xC0 && g > 0xC0 && b > 0xC0);
            if (is_white && bri > LED_WHITE_BRIGHT_MAX) bri = LED_WHITE_BRIGHT_MAX;
            g_led_bright = (uint8_t)constrain(bri, 0, 255);
        }
        // colour RGB
        char* r_p = strstr(msg, "\"r\":");
        char* g_p = strstr(msg, "\"g\":");
        char* b_p = strstr(msg, "\"b\":");
        if (r_p && g_p && b_p) {
            int r = atoi(r_p + 4);
            int g = atoi(g_p + 4);
            int b = atoi(b_p + 4);
            g_led_color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            bool is_white = (r > 0xC0 && g > 0xC0 && b > 0xC0);
            if (is_white && g_led_bright > LED_WHITE_BRIGHT_MAX)
                g_led_bright = LED_WHITE_BRIGHT_MAX;
        }
        // effect
        static const char* names[] = {
            "OFF","STATIC","BREATH","SPIN","RAINBOW",
            "FIRE","VORTEX","PULSE","TWINKLE","WAVE"
        };
        char* eff_p = strstr(msg, "\"effect\":");
        if (eff_p) {
            for (int i = 0; i < 10; i++) {
                if (strstr(eff_p, names[i])) { g_led_effect = i; break; }
            }
        }
        ui_leds_update_visual();
        mqtt_publish_led_state();
        Serial.printf("[MQTT] Light cmd → color=#%06X bri=%d eff=%d\n",
                      g_led_color, g_led_bright, g_led_effect);
    }

    // ── LED colour old format "R,G,B" (backward compatibility) ──────────────
    else if (strcmp(topic, T_LED_COLOR_CMD) == 0) {
        int r = 0, g = 0, b = 0;
        sscanf(msg, "%d,%d,%d", &r, &g, &b);
        g_led_color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        bool is_white = (r > 0xC0 && g > 0xC0 && b > 0xC0);
        if (is_white && g_led_bright > LED_WHITE_BRIGHT_MAX) {
            g_led_bright = LED_WHITE_BRIGHT_MAX;
            ui_leds_update_visual();
        }
        Serial.printf("[MQTT] LED color → #%06X\n", g_led_color);
    }

    // ── LED brightness ──────────────────────────────────────────────────────
    else if (strcmp(topic, T_LED_BRI_CMD) == 0) {
        uint8_t bri = (uint8_t)constrain((int)atof(msg), 0, 255);
        // Cap if pure white is selected
        uint8_t r = (g_led_color >> 16) & 0xFF;
        uint8_t g = (g_led_color >> 8)  & 0xFF;
        uint8_t b =  g_led_color        & 0xFF;
        bool is_white = (r > 0xC0 && g > 0xC0 && b > 0xC0);
        if (is_white && bri > LED_WHITE_BRIGHT_MAX) {
            bri = LED_WHITE_BRIGHT_MAX;
            Serial.println("[MQTT] White detected — brightness capped");
        }
        g_led_bright = bri;
        ui_leds_update_visual();
        mqtt_publish_led_state();
        Serial.printf("[MQTT] LED brightness → %d\n", g_led_bright);
    }
}

// =============================================================================
//  NON-BLOCKING RECONNECTION
// =============================================================================
static void mqtt_reconnect() {
    if (WiFi.status() != WL_CONNECTED) return;  // No WiFi -> wait
    if (s_mqtt.connected()) return;              // Already connected

    uint32_t now = millis();
    if (now - s_last_reconnect_ms < MQTT_RECONNECT_INTERVAL) return;
    s_last_reconnect_ms = now;

    Serial.printf("[MQTT] Connecting to %s:%d...\n", MQTT_BROKER, MQTT_PORT);

    bool ok;
    if (strlen(MQTT_USER) > 0) {
        ok = s_mqtt.connect("AeriusGold", MQTT_USER, MQTT_PASS,
                            "aerius/system/status", 0, true, "offline");
    } else {
        ok = s_mqtt.connect("AeriusGold", nullptr, nullptr,
                            "aerius/system/status", 0, true, "offline");
    }

    if (ok) {
        Serial.println("[MQTT] Connecté !");
        s_mqtt.publish("aerius/system/status", "online", true);
        s_mqtt.publish(T_SYS_IP, WiFi.localIP().toString().c_str(), true);

        // Subscribe to command topics
        s_mqtt.subscribe(T_FAN_SPEED_CMD);
        s_mqtt.subscribe(T_FAN_SW_CMD);
        s_mqtt.subscribe(T_FAN_TIMER_CMD);
        s_mqtt.subscribe(T_LED_EFFECT_CMD);
        s_mqtt.subscribe(T_LED_COLOR_CMD);
        s_mqtt.subscribe(T_LED_BRI_CMD);
        s_mqtt.subscribe(T_LIGHT_CMD);

        // HA auto-discovery (once only)
        if (!s_discovery_done) publish_discovery();

        // Publish initial state
        mqtt_publish_fan_state();
        mqtt_publish_led_state();
    } else {
        Serial.printf("[MQTT] Failed (rc=%d) — retry in %d s\n",
                      s_mqtt.state(), MQTT_RECONNECT_INTERVAL / 1000);
    }
}

// =============================================================================
//  PUBLIC FUNCTIONS
// =============================================================================

void mqtt_init() {
    s_mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    s_mqtt.setCallback(mqtt_callback);
    s_mqtt.setKeepAlive(30);
    s_mqtt.setBufferSize(768);  // large enough for discovery payloads
    Serial.printf("[MQTT] Initialisé — broker : %s:%d\n", MQTT_BROKER, MQTT_PORT);
}

void mqtt_tick() {
    // Attempt reconnection if needed (non-blocking)
    if (!s_mqtt.connected()) {
        mqtt_reconnect();
        return;
    }

    // Process incoming messages
    s_mqtt.loop();

    // Periodic sensor publish
    uint32_t now = millis();
    if (now - s_last_sensor_ms > MQTT_SENSOR_INTERVAL) {
        s_last_sensor_ms = now;
        mqtt_publish_sensors();
    }
}

void mqtt_publish_fan_state() {
    if (!s_mqtt.connected()) return;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", g_fan_speed);
    s_mqtt.publish(T_FAN_SPEED_STATE, buf, true);
    s_mqtt.publish(T_FAN_SW_STATE, g_fan_running ? "ON" : "OFF", true);
    snprintf(buf, sizeof(buf), "%lu", g_timer_secs);
    s_mqtt.publish(T_FAN_TIMER_STATE, buf, true);
}

void mqtt_publish_led_state() {
    if (!s_mqtt.connected()) return;

    static const char* names[] = {
        "OFF","STATIC","BREATH","SPIN","RAINBOW",
        "FIRE","VORTEX","PULSE","TWINKLE","WAVE"
    };
    uint8_t idx = (g_led_effect < 10) ? g_led_effect : 0;

    // Publish legacy topics (backward compatibility)
    s_mqtt.publish(T_LED_EFFECT_STATE, names[idx], true);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", g_led_bright);
    s_mqtt.publish(T_LED_BRI_STATE, buf, true);

    // Publish JSON light state for HA entity
    uint8_t r = (g_led_color >> 16) & 0xFF;
    uint8_t g = (g_led_color >> 8)  & 0xFF;
    uint8_t b =  g_led_color        & 0xFF;
    char json[160];
    snprintf(json, sizeof(json),
        "{\"state\":\"%s\","
        "\"brightness\":%d,"
        "\"color\":{\"r\":%d,\"g\":%d,\"b\":%d},"
        "\"effect\":\"%s\"}",
        g_led_effect == 0 ? "OFF" : "ON",
        g_led_bright, r, g, b, names[idx]
    );
    s_mqtt.publish(T_LIGHT_STATE, json, true);
}

void mqtt_publish_sensors() {
    if (!s_mqtt.connected()) return;
    char buf[16];

    // BME280 temperature + humidity
    snprintf(buf, sizeof(buf), "%.1f", g_temperature);
    s_mqtt.publish(T_SENSOR_TEMP, buf);
    snprintf(buf, sizeof(buf), "%.1f", g_humidity);
    s_mqtt.publish(T_SENSOR_HUMI, buf);

    // CPU temperature
    snprintf(buf, sizeof(buf), "%.1f", temperatureRead());
    s_mqtt.publish(T_SENSOR_CPU, buf);

    // WiFi RSSI
    snprintf(buf, sizeof(buf), "%d", (int)WiFi.RSSI());
    s_mqtt.publish(T_SYS_RSSI, buf);

    // Uptime in seconds
    snprintf(buf, sizeof(buf), "%lu", millis() / 1000);
    s_mqtt.publish(T_SYS_UPTIME, buf);

    // Fan RPM
    snprintf(buf, sizeof(buf), "%lu", ui_home_get_rpm());
    s_mqtt.publish(T_FAN_RPM, buf);

    // LD2410C presence
    s_mqtt.publish(T_PRESENCE, g_presence_detected ? "ON" : "OFF");
    snprintf(buf, sizeof(buf), "%d", g_presence_distance);
    s_mqtt.publish(T_PRESENCE_DIST, buf);
}

#endif  // MQTT_BROKER
