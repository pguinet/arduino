/*
 * HA_Wall_Panel - ESP32-4848S040
 *
 * Panneau mural Home Assistant : controle de lumieres HA
 * et 3 relais locaux via MQTT avec auto-discovery.
 *
 * Board: ESP32-4848S040C_I_Y_3 (Guition 4" 480x480 IPS)
 * FQBN: PlatformIO esp32-s3-devkitm-1
 *
 * @dependencies Arduino_GFX, LVGL 8.4, TAMC_GT911, PubSubClient
 */

#include <Arduino.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <TAMC_GT911.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "credentials.h"
#include "esp32s3/rom/cache.h"

/* ── Display configuration ─────────────────────────────────── */

#define SCREEN_W 480
#define SCREEN_H 480
#define GFX_BL 38

// Software SPI for ST7701S init commands
Arduino_DataBus *bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED /* DC */, 39 /* CS */,
    48 /* SCK */, 47 /* MOSI */, GFX_NOT_DEFINED /* MISO */);

// RGB parallel panel
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
    11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0 /* R4 */,
    8 /* G0 */, 20 /* G1 */, 3 /* G2 */, 46 /* G3 */, 9 /* G4 */, 10 /* G5 */,
    4 /* B0 */, 5 /* B1 */, 6 /* B2 */, 7 /* B3 */, 15 /* B4 */,
    1, 10, 8, 50,   // hsync: polarity, front_porch, pulse_width, back_porch
    1, 10, 8, 20,   // vsync: polarity, front_porch, pulse_width, back_porch
    1, 12000000,     // pclk: active_neg, speed
    false,           // useBigEndian
    0,               // de_idle_high
    0,               // pclk_idle_high
    480 * 20);       // bounce_buffer_size_px (20 lines in internal SRAM)

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    SCREEN_W, SCREEN_H, rgbpanel, 0 /* rotation */, true /* auto_flush */,
    bus, GFX_NOT_DEFINED /* RST */,
    st7701_type9_init_operations, sizeof(st7701_type9_init_operations));

/* ── Touch configuration ───────────────────────────────────── */

#define TP_SDA 19
#define TP_SCL 45
#define TP_INT -1
#define TP_RST -1

TAMC_GT911 touch(TP_SDA, TP_SCL, TP_INT, TP_RST, SCREEN_W, SCREEN_H);

/* ── Relay configuration ───────────────────────────────────── */

#define RELAY_COUNT 3
const int relay_pins[RELAY_COUNT] = {40, 2, 1};
bool relay_state[RELAY_COUNT] = {false, false, false};

/* ── Light entities configuration ──────────────────────────── */

#define MAX_LIGHTS 6

struct LightEntity {
    const char *entity_id;   // zigbee2mqtt friendly name
    const char *name;        // display name
    bool state;
};

LightEntity lights[] = {
    {"Guirlande", "Guirlande", false},
    {"Lit", "Lit", false},
    {"Suspension", "Suspension", false},
};
const int light_count = sizeof(lights) / sizeof(lights[0]);

/* ── Motion sensor configuration ──────────────────────────── */

#define MOTION_TOPIC "zigbee2mqtt/MotionSensor01"
bool motion_state = false;

/* ── MQTT configuration ────────────────────────────────────── */

#define MQTT_TOPIC_AVAILABILITY "wall_panel/availability"
#define MQTT_CLIENT_ID "wall_panel"

WiFiClient espClient;
PubSubClient mqtt(espClient);

/* ── LVGL globals ──────────────────────────────────────────── */

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

/* ── UI elements ───────────────────────────────────────────── */

static lv_obj_t *btn_light[MAX_LIGHTS] = {};
static lv_obj_t *lbl_light[MAX_LIGHTS] = {};

static lv_obj_t *btn_relay[RELAY_COUNT] = {};
static lv_obj_t *lbl_relay[RELAY_COUNT] = {};

static lv_obj_t *motion_card = NULL;
static lv_obj_t *lbl_motion = NULL;

static lv_obj_t *mqtt_led = NULL;

/* ── Timers ────────────────────────────────────────────────── */

static unsigned long last_mqtt_led_update = 0;
static unsigned long last_mqtt_reconnect = 0;
static unsigned long last_wifi_check = 0;

/* ── Forward declarations ──────────────────────────────────── */

static void mqtt_callback(char *topic, byte *payload, unsigned int length);
static void mqtt_connect(void);
static void publish_discovery(void);
static void publish_relay_states(void);
static void toggle_light(int idx);
static void update_light_btn(int idx);
static void update_relay_btn(int idx);
static void update_motion_card(void);
static void ui_init(void);

/* ── LVGL display flush callback ───────────────────────────── */

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                           lv_color_t *color_p) {
    uint16_t *fb = (uint16_t *)gfx->getFramebuffer();
    if (fb) {
        // Fast line-by-line memcpy (much faster than pixel-by-pixel draw16bitRGBBitmap)
        int32_t w = area->x2 - area->x1 + 1;
        uint16_t *src = (uint16_t *)color_p;
        for (int32_t y = area->y1; y <= area->y2; y++) {
            memcpy(&fb[y * SCREEN_W + area->x1], src, w * sizeof(uint16_t));
            src += w;
        }
        // Flush cache so DMA sees updated pixels
        uint32_t flush_start = (uint32_t)&fb[area->y1 * SCREEN_W];
        uint32_t flush_size = (area->y2 - area->y1 + 1) * SCREEN_W * sizeof(uint16_t);
        Cache_WriteBack_Addr(flush_start, flush_size);
    }
    lv_disp_flush_ready(drv);
}

/* ── LVGL touch read callback ──────────────────────────────── */

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    touch.read();
    if (touch.isTouched) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = (SCREEN_W - 1) - touch.points[0].x;
        data->point.y = (SCREEN_H - 1) - touch.points[0].y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

/* ── MQTT callback ─────────────────────────────────────────── */

static void mqtt_callback(char *topic, byte *payload, unsigned int length) {
    char msg[512];
    if (length >= sizeof(msg)) length = sizeof(msg) - 1;
    memcpy(msg, payload, length);
    msg[length] = '\0';

    // Check relay commands: wall_panel/relay_X/set
    for (int i = 0; i < RELAY_COUNT; i++) {
        char cmd_topic[64];
        snprintf(cmd_topic, sizeof(cmd_topic), "wall_panel/relay_%d/set", i + 1);
        if (strcmp(topic, cmd_topic) == 0) {
            relay_state[i] = (strcmp(msg, "ON") == 0);
            digitalWrite(relay_pins[i], relay_state[i] ? HIGH : LOW);

            // Publish new state
            char state_topic[64];
            snprintf(state_topic, sizeof(state_topic), "wall_panel/relay_%d/state", i + 1);
            mqtt.publish(state_topic, relay_state[i] ? "ON" : "OFF", true);

            update_relay_btn(i);
            return;
        }
    }

    // Check motion sensor
    if (strcmp(topic, MOTION_TOPIC) == 0) {
        motion_state = (strstr(msg, "\"occupancy\":true") != NULL);
        update_motion_card();
        return;
    }

    // Check light states: zigbee2mqtt/<entity_id>
    for (int i = 0; i < light_count; i++) {
        char light_topic[128];
        snprintf(light_topic, sizeof(light_topic), "zigbee2mqtt/%s", lights[i].entity_id);
        if (strcmp(topic, light_topic) == 0) {
            lights[i].state = (strstr(msg, "\"state\":\"ON\"") != NULL);
            update_light_btn(i);
            return;
        }
    }
}

/* ── MQTT connect ──────────────────────────────────────────── */

static void mqtt_connect(void) {
    if (mqtt.connected()) return;

    Serial.println("MQTT connecting...");
    if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS,
                     MQTT_TOPIC_AVAILABILITY, 0, true, "offline")) {
        Serial.println("MQTT connected");
        mqtt.publish(MQTT_TOPIC_AVAILABILITY, "online", true);

        publish_discovery();
        publish_relay_states();

        // Subscribe to relay commands
        for (int i = 0; i < RELAY_COUNT; i++) {
            char topic[64];
            snprintf(topic, sizeof(topic), "wall_panel/relay_%d/set", i + 1);
            mqtt.subscribe(topic);
        }

        // Subscribe to motion sensor
        mqtt.subscribe(MOTION_TOPIC);

        // Subscribe to light states
        for (int i = 0; i < light_count; i++) {
            char topic[128];
            snprintf(topic, sizeof(topic), "zigbee2mqtt/%s", lights[i].entity_id);
            mqtt.subscribe(topic);
        }
    } else {
        Serial.print("MQTT failed, rc=");
        Serial.println(mqtt.state());
    }
}

/* ── MQTT discovery ────────────────────────────────────────── */

static void publish_discovery(void) {
    for (int i = 0; i < RELAY_COUNT; i++) {
        char topic[128];
        snprintf(topic, sizeof(topic),
                 "homeassistant/switch/wall_panel/relay_%d/config", i + 1);

        char payload[512];
        snprintf(payload, sizeof(payload),
            "{"
            "\"name\":\"Relais %d\","
            "\"unique_id\":\"wall_panel_relay_%d\","
            "\"command_topic\":\"wall_panel/relay_%d/set\","
            "\"state_topic\":\"wall_panel/relay_%d/state\","
            "\"availability_topic\":\"%s\","
            "\"payload_on\":\"ON\","
            "\"payload_off\":\"OFF\","
            "\"device\":{"
                "\"identifiers\":[\"wall_panel\"],"
                "\"name\":\"Wall Panel\","
                "\"model\":\"ESP32-4848S040\","
                "\"manufacturer\":\"Guition\""
            "}"
            "}",
            i + 1, i + 1, i + 1, i + 1, MQTT_TOPIC_AVAILABILITY);

        mqtt.publish(topic, payload, true);
    }
}

/* ── Publish relay states ──────────────────────────────────── */

static void publish_relay_states(void) {
    for (int i = 0; i < RELAY_COUNT; i++) {
        char topic[64];
        snprintf(topic, sizeof(topic), "wall_panel/relay_%d/state", i + 1);
        mqtt.publish(topic, relay_state[i] ? "ON" : "OFF", true);
    }
}

/* ── Toggle light via MQTT ─────────────────────────────────── */

static void toggle_light(int idx) {
    if (idx < 0 || idx >= light_count) return;

    // Toggle: send opposite of current state
    const char *new_state = lights[idx].state ? "OFF" : "ON";

    char topic[128];
    snprintf(topic, sizeof(topic), "zigbee2mqtt/%s/set", lights[idx].entity_id);

    char payload[32];
    snprintf(payload, sizeof(payload), "{\"state\":\"%s\"}", new_state);

    mqtt.publish(topic, payload);
}

/* ── Update light button appearance ────────────────────────── */

static void update_light_btn(int idx) {
    if (idx < 0 || idx >= light_count || !btn_light[idx]) return;

    if (lights[idx].state) {
        lv_obj_set_style_bg_color(btn_light[idx], lv_color_hex(0xfca311), 0);
        lv_obj_set_style_text_color(lbl_light[idx], lv_color_hex(0x1a1a2e), 0);
        lv_label_set_text_fmt(lbl_light[idx], LV_SYMBOL_POWER "\n%s\nON", lights[idx].name);
    } else {
        lv_obj_set_style_bg_color(btn_light[idx], lv_color_hex(0x333333), 0);
        lv_obj_set_style_text_color(lbl_light[idx], lv_color_hex(0x888888), 0);
        lv_label_set_text_fmt(lbl_light[idx], LV_SYMBOL_POWER "\n%s\nOFF", lights[idx].name);
    }
}

/* ── Update relay button appearance ────────────────────────── */

static void update_relay_btn(int idx) {
    if (idx < 0 || idx >= RELAY_COUNT || !btn_relay[idx]) return;

    if (relay_state[idx]) {
        lv_obj_set_style_bg_color(btn_relay[idx], lv_color_hex(0x00ff88), 0);
        lv_obj_set_style_text_color(lbl_relay[idx], lv_color_hex(0x1a1a2e), 0);
        lv_label_set_text_fmt(lbl_relay[idx], "Relais %d\nON", idx + 1);
    } else {
        lv_obj_set_style_bg_color(btn_relay[idx], lv_color_hex(0x333333), 0);
        lv_obj_set_style_text_color(lbl_relay[idx], lv_color_hex(0x888888), 0);
        lv_label_set_text_fmt(lbl_relay[idx], "Relais %d\nOFF", idx + 1);
    }
}

/* ── Update motion card appearance ─────────────────────────── */

static void update_motion_card(void) {
    if (!motion_card || !lbl_motion) return;

    if (motion_state) {
        lv_obj_set_style_bg_color(motion_card, lv_color_hex(0x4a1a2e), 0);
        lv_obj_set_style_text_color(lbl_motion, lv_color_hex(0xff6644), 0);
        lv_label_set_text(lbl_motion, LV_SYMBOL_EYE_OPEN "  Mouvement detecte");
    } else {
        lv_obj_set_style_bg_color(motion_card, lv_color_hex(0x16213e), 0);
        lv_obj_set_style_text_color(lbl_motion, lv_color_hex(0x888888), 0);
        lv_label_set_text(lbl_motion, LV_SYMBOL_EYE_CLOSE "  Aucun mouvement");
    }
}

/* ── Light button callback ─────────────────────────────────── */

static void light_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    toggle_light(idx);
}

/* ── Relay button callback ─────────────────────────────────── */

static void relay_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= RELAY_COUNT) return;

    relay_state[idx] = !relay_state[idx];
    digitalWrite(relay_pins[idx], relay_state[idx] ? HIGH : LOW);

    // Publish new state via MQTT
    if (mqtt.connected()) {
        char topic[64];
        snprintf(topic, sizeof(topic), "wall_panel/relay_%d/state", idx + 1);
        mqtt.publish(topic, relay_state[idx] ? "ON" : "OFF", true);
    }

    update_relay_btn(idx);
}

/* ── Build the UI ──────────────────────────────────────────── */

static void ui_init(void) {
    lv_obj_t *scr = lv_scr_act();

    // Dark background
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 10, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Title ──
    lv_obj_t *lbl_title = lv_label_create(scr);
    lv_label_set_text(lbl_title, "HA Wall Panel");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x00ff88), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_28, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 5);

    // ── MQTT LED indicator ──
    mqtt_led = lv_led_create(scr);
    lv_obj_set_size(mqtt_led, 16, 16);
    lv_obj_align(mqtt_led, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_led_set_color(mqtt_led, lv_color_hex(0xff4444));
    lv_led_on(mqtt_led);

    // ── Light buttons container (flex wrap, 2 columns) ──
    lv_obj_t *light_grid = lv_obj_create(scr);
    lv_obj_set_size(light_grid, 460, 340);
    lv_obj_set_style_bg_opa(light_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(light_grid, 0, 0);
    lv_obj_set_style_pad_all(light_grid, 5, 0);
    lv_obj_set_style_pad_row(light_grid, 10, 0);
    lv_obj_set_style_pad_column(light_grid, 10, 0);
    lv_obj_set_flex_flow(light_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(light_grid, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(light_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(light_grid, LV_ALIGN_TOP_MID, 0, 45);

    for (int i = 0; i < light_count; i++) {
        btn_light[i] = lv_btn_create(light_grid);
        lv_obj_set_size(btn_light[i], 218, 155);
        lv_obj_set_style_bg_color(btn_light[i], lv_color_hex(0x333333), 0);
        lv_obj_set_style_bg_opa(btn_light[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn_light[i], 15, 0);
        lv_obj_set_style_border_width(btn_light[i], 0, 0);
        lv_obj_set_style_shadow_width(btn_light[i], 0, 0);

        lbl_light[i] = lv_label_create(btn_light[i]);
        lv_label_set_text_fmt(lbl_light[i], LV_SYMBOL_POWER "\n%s\nOFF", lights[i].name);
        lv_obj_set_style_text_color(lbl_light[i], lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(lbl_light[i], &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_align(lbl_light[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl_light[i]);

        lv_obj_add_event_cb(btn_light[i], light_cb, LV_EVENT_SHORT_CLICKED,
                            (void *)(intptr_t)i);
    }

    // ── Motion sensor card (in the grid, same size as light buttons) ──
    motion_card = lv_obj_create(light_grid);
    lv_obj_set_size(motion_card, 218, 155);
    lv_obj_set_style_bg_color(motion_card, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_bg_opa(motion_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(motion_card, 15, 0);
    lv_obj_set_style_border_width(motion_card, 0, 0);
    lv_obj_clear_flag(motion_card, LV_OBJ_FLAG_SCROLLABLE);

    lbl_motion = lv_label_create(motion_card);
    lv_label_set_text(lbl_motion, LV_SYMBOL_EYE_CLOSE "  Aucun mouvement");
    lv_obj_set_style_text_color(lbl_motion, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(lbl_motion, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(lbl_motion, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lbl_motion);

    // ── Relay buttons row ──
    lv_obj_t *relay_row = lv_obj_create(scr);
    lv_obj_set_size(relay_row, 460, 80);
    lv_obj_set_style_bg_opa(relay_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(relay_row, 0, 0);
    lv_obj_set_style_pad_all(relay_row, 0, 0);
    lv_obj_set_style_pad_column(relay_row, 10, 0);
    lv_obj_set_flex_flow(relay_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(relay_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(relay_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(relay_row, LV_ALIGN_TOP_MID, 0, 395);

    for (int i = 0; i < RELAY_COUNT; i++) {
        btn_relay[i] = lv_btn_create(relay_row);
        lv_obj_set_size(btn_relay[i], 145, 70);
        lv_obj_set_style_bg_color(btn_relay[i], lv_color_hex(0x333333), 0);
        lv_obj_set_style_bg_opa(btn_relay[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn_relay[i], 12, 0);
        lv_obj_set_style_border_width(btn_relay[i], 0, 0);
        lv_obj_set_style_shadow_width(btn_relay[i], 0, 0);

        lbl_relay[i] = lv_label_create(btn_relay[i]);
        lv_label_set_text_fmt(lbl_relay[i], "Relais %d\nOFF", i + 1);
        lv_obj_set_style_text_color(lbl_relay[i], lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(lbl_relay[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_align(lbl_relay[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl_relay[i]);

        lv_obj_add_event_cb(btn_relay[i], relay_cb, LV_EVENT_SHORT_CLICKED,
                            (void *)(intptr_t)i);
    }
}

/* ── Setup ─────────────────────────────────────────────────── */

void setup() {
    Serial.begin(115200);
    Serial.println("HA_Wall_Panel starting...");

    // Init relay pins
    for (int i = 0; i < RELAY_COUNT; i++) {
        pinMode(relay_pins[i], OUTPUT);
        digitalWrite(relay_pins[i], LOW);
    }

    // Backlight ON
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    // Init display
    gfx->begin();
    gfx->fillScreen(0x0000);

    // Init touch
    Wire.begin(TP_SDA, TP_SCL);
    touch.begin();
    touch.setRotation(ROTATION_NORMAL);

    // Init LVGL
    lv_init();

    // LVGL buffer in PSRAM, fast memcpy to GFX framebuffer on flush
    buf1 = (lv_color_t *)ps_malloc(SCREEN_W * SCREEN_H * sizeof(lv_color_t));
    if (!buf1) {
        Serial.println("ERROR: PSRAM alloc failed");
        buf1 = (lv_color_t *)malloc(SCREEN_W * 100 * sizeof(lv_color_t));
        lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCREEN_W * 100);
    } else {
        lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCREEN_W * SCREEN_H);
    }

    // Setup display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_W;
    disp_drv.ver_res = SCREEN_H;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Setup touch input driver
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    // Build UI
    ui_init();

    // Start WiFi (non-blocking)
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("WiFi connecting to ");
    Serial.println(WIFI_SSID);

    // Setup MQTT
    mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    mqtt.setBufferSize(512);
    mqtt.setCallback(mqtt_callback);

    Serial.println("HA_Wall_Panel ready.");
}

/* ── Loop ──────────────────────────────────────────────────── */

void loop() {
    lv_timer_handler();

    unsigned long now = millis();

    // MQTT loop
    if (mqtt.connected()) {
        mqtt.loop();
    }

    // Update MQTT LED every 1s
    if (now - last_mqtt_led_update >= 1000) {
        last_mqtt_led_update = now;
        if (mqtt_led) {
            if (mqtt.connected()) {
                lv_led_set_color(mqtt_led, lv_color_hex(0x00ff88));
            } else {
                lv_led_set_color(mqtt_led, lv_color_hex(0xff4444));
            }
        }
    }

    // MQTT reconnect every 5s
    if (now - last_mqtt_reconnect >= 5000) {
        last_mqtt_reconnect = now;
        if (WiFi.status() == WL_CONNECTED && !mqtt.connected()) {
            mqtt_connect();
        }
    }

    // WiFi auto-reconnect every 10s
    if (now - last_wifi_check >= 10000) {
        last_wifi_check = now;
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi disconnected, reconnecting...");
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
    }

    delay(5);
}
