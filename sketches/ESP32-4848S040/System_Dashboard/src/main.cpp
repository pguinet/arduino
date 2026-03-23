/*
 * System_Dashboard - ESP32-4848S040
 *
 * Dashboard systeme interactif : jauges RAM/PSRAM, infos WiFi,
 * controle des 3 relais par boutons tactiles.
 *
 * Board: ESP32-4848S040C_I_Y_3 (Guition 4" 480x480 IPS)
 * FQBN: PlatformIO esp32-s3-devkitm-1
 *
 * @dependencies Arduino_GFX, LVGL 8.4, TAMC_GT911
 */

#include <Arduino.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <TAMC_GT911.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#include "credentials.h"

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
    1, 10, 8, 60,   // hsync: polarity, front_porch, pulse_width, back_porch
    1, 10, 8, 20,   // vsync: polarity, front_porch, pulse_width, back_porch
    1, 12000000);    // pclk: active_neg, speed

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

#define RELAY_1 40
#define RELAY_2 2
#define RELAY_3 1

bool relay_state[3] = {false, false, false};
const int relay_pins[3] = {RELAY_1, RELAY_2, RELAY_3};

/* ── LVGL globals ──────────────────────────────────────────── */

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = NULL;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

/* ── UI elements ───────────────────────────────────────────── */

static lv_obj_t *arc_ram = NULL;
static lv_obj_t *lbl_ram_pct = NULL;
static lv_obj_t *lbl_ram_val = NULL;

static lv_obj_t *arc_psram = NULL;
static lv_obj_t *lbl_psram_pct = NULL;
static lv_obj_t *lbl_psram_val = NULL;

static lv_obj_t *lbl_chip = NULL;
static lv_obj_t *lbl_uptime = NULL;
static lv_obj_t *lbl_wifi = NULL;

static lv_obj_t *btn_relay[3] = {NULL, NULL, NULL};
static lv_obj_t *lbl_relay[3] = {NULL, NULL, NULL};

/* ── Update timer ──────────────────────────────────────────── */

static unsigned long last_update = 0;
static const unsigned long UPDATE_INTERVAL = 1000;

/* ── WiFi reconnect ────────────────────────────────────────── */

static unsigned long last_wifi_check = 0;
static const unsigned long WIFI_CHECK_INTERVAL = 10000;

/* ── LVGL display flush callback ───────────────────────────── */

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                           lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    gfx->draw16bitRGBBitmap(area->x1, area->y1,
                             (uint16_t *)&color_p->full, w, h);

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

/* ── Create gauge card ─────────────────────────────────────── */

static lv_obj_t* create_gauge_card(lv_obj_t *parent, const char *title, lv_color_t color,
                                    lv_obj_t **arc_out, lv_obj_t **pct_out, lv_obj_t **val_out)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 220, 160);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 15, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_color(lbl_title, color, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *arc = lv_arc_create(card);
    lv_obj_set_size(arc, 90, 90);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 5);
    *arc_out = arc;

    lv_obj_t *lbl_pct = lv_label_create(card);
    lv_label_set_text(lbl_pct, "0%");
    lv_obj_set_style_text_color(lbl_pct, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(lbl_pct, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl_pct, LV_ALIGN_CENTER, 0, 3);
    *pct_out = lbl_pct;

    lv_obj_t *lbl_val = lv_label_create(card);
    lv_label_set_text(lbl_val, "--- KB");
    lv_obj_set_style_text_color(lbl_val, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(lbl_val, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_val, LV_ALIGN_BOTTOM_MID, 0, -2);
    *val_out = lbl_val;

    return card;
}

/* ── Relay button callback ─────────────────────────────────── */

static void relay_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx > 2) return;

    relay_state[idx] = !relay_state[idx];
    digitalWrite(relay_pins[idx], relay_state[idx] ? HIGH : LOW);

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

/* ── Build the dashboard UI ───────────────────────────────── */

static void ui_init(void) {
    lv_obj_t *scr = lv_scr_act();

    // Dark background
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 10, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Title ──
    lv_obj_t *lbl_title = lv_label_create(scr);
    lv_label_set_text(lbl_title, "System Dashboard");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x00ff88), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 5);

    // ── Gauge cards container ──
    lv_obj_t *gauge_row = lv_obj_create(scr);
    lv_obj_set_size(gauge_row, 460, 170);
    lv_obj_set_style_bg_opa(gauge_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gauge_row, 0, 0);
    lv_obj_set_style_pad_all(gauge_row, 0, 0);
    lv_obj_set_style_pad_column(gauge_row, 10, 0);
    lv_obj_set_flex_flow(gauge_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gauge_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(gauge_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(gauge_row, LV_ALIGN_TOP_MID, 0, 45);

    // RAM gauge
    create_gauge_card(gauge_row, "RAM Interne", lv_color_hex(0x4cc9f0),
                      &arc_ram, &lbl_ram_pct, &lbl_ram_val);

    // PSRAM gauge
    create_gauge_card(gauge_row, "PSRAM", lv_color_hex(0xf72585),
                      &arc_psram, &lbl_psram_pct, &lbl_psram_val);

    // ── Info card ──
    lv_obj_t *info_card = lv_obj_create(scr);
    lv_obj_set_size(info_card, 460, 170);
    lv_obj_set_style_bg_color(info_card, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_bg_opa(info_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(info_card, 0, 0);
    lv_obj_set_style_radius(info_card, 15, 0);
    lv_obj_set_style_pad_all(info_card, 12, 0);
    lv_obj_clear_flag(info_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(info_card, LV_ALIGN_TOP_MID, 0, 220);

    // Chip info line
    lbl_chip = lv_label_create(info_card);
    lv_label_set_text(lbl_chip, LV_SYMBOL_SETTINGS "  ESP32-S3 2 cores    --- MHz");
    lv_obj_set_style_text_color(lbl_chip, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_font(lbl_chip, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_chip, LV_ALIGN_TOP_LEFT, 0, 0);

    // Uptime line
    lbl_uptime = lv_label_create(info_card);
    lv_label_set_text(lbl_uptime, LV_SYMBOL_LOOP "  Uptime: 00:00:00");
    lv_obj_set_style_text_color(lbl_uptime, lv_color_hex(0x00ff88), 0);
    lv_obj_set_style_text_font(lbl_uptime, &lv_font_montserrat_28, 0);
    lv_obj_align(lbl_uptime, LV_ALIGN_LEFT_MID, 0, 5);

    // WiFi line
    lbl_wifi = lv_label_create(info_card);
    lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI "  Connexion...");
    lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(lbl_wifi, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_wifi, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // ── Relay buttons row ──
    lv_obj_t *relay_row = lv_obj_create(scr);
    lv_obj_set_size(relay_row, 460, 80);
    lv_obj_set_style_bg_opa(relay_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(relay_row, 0, 0);
    lv_obj_set_style_pad_all(relay_row, 0, 0);
    lv_obj_set_style_pad_column(relay_row, 10, 0);
    lv_obj_set_flex_flow(relay_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(relay_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(relay_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(relay_row, LV_ALIGN_TOP_MID, 0, 395);

    for (int i = 0; i < 3; i++) {
        btn_relay[i] = lv_btn_create(relay_row);
        lv_obj_set_size(btn_relay[i], 140, 70);
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

        lv_obj_add_event_cb(btn_relay[i], relay_cb, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
    }
}

/* ── Update display values ─────────────────────────────────── */

static void update_display(void) {
    // ── RAM ──
    size_t ram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t ram_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t ram_used = ram_total - ram_free;
    int ram_pct = (ram_total > 0) ? (int)(ram_used * 100 / ram_total) : 0;

    lv_arc_set_value(arc_ram, ram_pct);
    lv_label_set_text_fmt(lbl_ram_pct, "%d%%", ram_pct);
    lv_label_set_text_fmt(lbl_ram_val, "%d/%d KB", (int)(ram_used / 1024), (int)(ram_total / 1024));

    // ── PSRAM ──
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_used = psram_total - psram_free;
    int psram_pct = (psram_total > 0) ? (int)(psram_used * 100 / psram_total) : 0;

    lv_arc_set_value(arc_psram, psram_pct);
    lv_label_set_text_fmt(lbl_psram_pct, "%d%%", psram_pct);
    // Use integer math (LV_SPRINTF_USE_FLOAT is off)
    int psram_used_kb = (int)(psram_used / 1024);
    int psram_total_kb = (int)(psram_total / 1024);
    lv_label_set_text_fmt(lbl_psram_val, "%d.%d/%d.%d MB",
                          psram_used_kb / 1024, (psram_used_kb % 1024) * 10 / 1024,
                          psram_total_kb / 1024, (psram_total_kb % 1024) * 10 / 1024);

    // ── Chip info ──
    uint32_t cpu_mhz = getCpuFrequencyMhz();
    lv_label_set_text_fmt(lbl_chip, LV_SYMBOL_SETTINGS "  ESP32-S3 2 cores    %lu MHz", (unsigned long)cpu_mhz);

    // ── Uptime ──
    unsigned long sec = millis() / 1000;
    unsigned long h = sec / 3600;
    unsigned long m = (sec % 3600) / 60;
    unsigned long s = sec % 60;
    lv_label_set_text_fmt(lbl_uptime, LV_SYMBOL_LOOP " %02lu:%02lu:%02lu", h, m, s);

    // ── WiFi ──
    if (WiFi.status() == WL_CONNECTED) {
        int rssi = WiFi.RSSI();
        lv_label_set_text_fmt(lbl_wifi, LV_SYMBOL_WIFI "  %s  IP: %s  %d dBm",
                              WiFi.SSID().c_str(),
                              WiFi.localIP().toString().c_str(),
                              rssi);
        lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0xaaaaaa), 0);
    } else {
        lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI "  Non connecte");
        lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0xff4444), 0);
    }
}

/* ── Setup ─────────────────────────────────────────────────── */

void setup() {
    Serial.begin(115200);
    Serial.println("System_Dashboard starting...");

    // Init relay pins
    for (int i = 0; i < 3; i++) {
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

    // Allocate draw buffer in PSRAM (full frame)
    buf1 = (lv_color_t *)ps_malloc(SCREEN_W * SCREEN_H * sizeof(lv_color_t));
    if (!buf1) {
        Serial.println("ERROR: PSRAM alloc failed, falling back to malloc");
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

    // Initial display update
    update_display();

    Serial.println("System_Dashboard ready.");
}

/* ── Loop ──────────────────────────────────────────────────── */

void loop() {
    lv_timer_handler();

    unsigned long now = millis();

    // Update display every second
    if (now - last_update >= UPDATE_INTERVAL) {
        last_update = now;
        update_display();
    }

    // WiFi auto-reconnect
    if (now - last_wifi_check >= WIFI_CHECK_INTERVAL) {
        last_wifi_check = now;
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi disconnected, reconnecting...");
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
    }

    delay(5);
}
