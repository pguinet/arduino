/*
 * WiFi_Scanner - JC3248W535C
 *
 * Scanner de reseaux WiFi avec interface tactile LVGL.
 * Liste scrollable, bouton scan, signal colore.
 *
 * Board: JC3248W535C (ESP32-S3 + LCD tactile 3.5")
 * FQBN: PlatformIO esp32-s3-devkitc-1
 *
 * @dependencies LVGL 8.3.x, WiFi
 */

#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"

#define LVGL_PORT_ROTATION_DEGREE (90)

// Colors
#define COLOR_BG        0x1a1a2e
#define COLOR_CARD      0x16213e
#define COLOR_TEXT      0xffffff
#define COLOR_ACCENT    0x00ff88
#define COLOR_EXCELLENT 0x00ff88  // > -50 dBm
#define COLOR_GOOD      0x4cc9f0  // -50 to -60
#define COLOR_FAIR      0xfca311  // -60 to -70
#define COLOR_WEAK      0xf72585  // < -70

// UI elements
static lv_obj_t *label_title;
static lv_obj_t *label_status;
static lv_obj_t *btn_scan;
static lv_obj_t *list_networks;
static lv_obj_t *spinner;

static bool scanning = false;

// Get color based on signal strength
static lv_color_t get_signal_color(int rssi)
{
    if (rssi > -50) return lv_color_hex(COLOR_EXCELLENT);
    if (rssi > -60) return lv_color_hex(COLOR_GOOD);
    if (rssi > -70) return lv_color_hex(COLOR_FAIR);
    return lv_color_hex(COLOR_WEAK);
}

// Get signal bars string
static const char* get_signal_bars(int rssi)
{
    if (rssi > -50) return LV_SYMBOL_WIFI;
    if (rssi > -60) return LV_SYMBOL_WIFI;
    if (rssi > -70) return LV_SYMBOL_WIFI;
    return LV_SYMBOL_WIFI;
}

// Perform WiFi scan and update list
static void do_scan()
{
    bsp_display_lock(0);

    // Show scanning state
    scanning = true;
    lv_label_set_text(label_status, "Scan en cours...");
    lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_scan, LV_OBJ_FLAG_HIDDEN);

    // Clear previous results
    lv_obj_clean(list_networks);

    bsp_display_unlock();

    Serial.println("Starting WiFi scan...");
    int n = WiFi.scanNetworks();
    Serial.printf("Scan complete: %d networks found\n", n);

    bsp_display_lock(0);

    // Hide spinner, show button
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_scan, LV_OBJ_FLAG_HIDDEN);

    if (n == 0) {
        lv_label_set_text(label_status, "Aucun reseau trouve");
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d reseaux trouves", n);
        lv_label_set_text(label_status, buf);

        // Add networks to list
        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            int rssi = WiFi.RSSI(i);
            bool encrypted = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;

            // Create list button
            lv_obj_t *btn = lv_list_add_btn(list_networks, NULL, "");
            lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_CARD), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
            lv_obj_set_style_pad_ver(btn, 8, 0);

            // Container for content
            lv_obj_t *cont = lv_obj_create(btn);
            lv_obj_remove_style_all(cont);
            lv_obj_set_size(cont, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            // Left side: lock icon + SSID
            lv_obj_t *left = lv_obj_create(cont);
            lv_obj_remove_style_all(left);
            lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
            lv_obj_set_style_pad_column(left, 8, 0);

            if (encrypted) {
                lv_obj_t *lock = lv_label_create(left);
                lv_label_set_text(lock, LV_SYMBOL_EYE_CLOSE);
                lv_obj_set_style_text_color(lock, lv_color_hex(0x888888), 0);
            }

            lv_obj_t *lbl_ssid = lv_label_create(left);
            lv_label_set_text(lbl_ssid, ssid.isEmpty() ? "(Hidden)" : ssid.c_str());
            lv_obj_set_style_text_color(lbl_ssid, lv_color_hex(COLOR_TEXT), 0);
            lv_obj_set_style_text_font(lbl_ssid, &lv_font_montserrat_16, 0);
            lv_label_set_long_mode(lbl_ssid, LV_LABEL_LONG_DOT);
            lv_obj_set_width(lbl_ssid, 280);

            // Right side: signal strength
            lv_obj_t *right = lv_obj_create(cont);
            lv_obj_remove_style_all(right);
            lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
            lv_obj_set_style_pad_column(right, 5, 0);

            char rssi_buf[16];
            snprintf(rssi_buf, sizeof(rssi_buf), "%d dBm", rssi);
            lv_obj_t *lbl_rssi = lv_label_create(right);
            lv_label_set_text(lbl_rssi, rssi_buf);
            lv_obj_set_style_text_color(lbl_rssi, get_signal_color(rssi), 0);
            lv_obj_set_style_text_font(lbl_rssi, &lv_font_montserrat_14, 0);

            lv_obj_t *lbl_wifi = lv_label_create(right);
            lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(lbl_wifi, get_signal_color(rssi), 0);
        }
    }

    scanning = false;
    bsp_display_unlock();

    WiFi.scanDelete();
}

// Button click callback
static void btn_scan_cb(lv_event_t *e)
{
    if (!scanning) {
        do_scan();
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.println("WiFi_Scanner - JC3248W535C");

    // Initialize WiFi in station mode
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    // Initialize display
    Serial.println("Initializing display...");
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = EXAMPLE_LCD_QSPI_H_RES * EXAMPLE_LCD_QSPI_V_RES,
#if LVGL_PORT_ROTATION_DEGREE == 90
        .rotate = LV_DISP_ROT_90,
#elif LVGL_PORT_ROTATION_DEGREE == 270
        .rotate = LV_DISP_ROT_270,
#elif LVGL_PORT_ROTATION_DEGREE == 180
        .rotate = LV_DISP_ROT_180,
#else
        .rotate = LV_DISP_ROT_NONE,
#endif
    };

    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    // Create UI
    Serial.println("Creating UI...");
    bsp_display_lock(0);

    // Background
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(COLOR_BG), 0);

    // Title
    label_title = lv_label_create(lv_scr_act());
    lv_label_set_text(label_title, LV_SYMBOL_WIFI " WiFi Scanner");
    lv_obj_set_style_text_color(label_title, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_24, 0);
    lv_obj_align(label_title, LV_ALIGN_TOP_LEFT, 15, 10);

    // Scan button
    btn_scan = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_scan, 100, 40);
    lv_obj_align(btn_scan, LV_ALIGN_TOP_RIGHT, -10, 5);
    lv_obj_set_style_bg_color(btn_scan, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_add_event_cb(btn_scan, btn_scan_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(btn_scan);
    lv_label_set_text(btn_label, "SCAN");
    lv_obj_set_style_text_color(btn_label, lv_color_hex(0x000000), 0);
    lv_obj_center(btn_label);

    // Spinner (hidden by default)
    spinner = lv_spinner_create(lv_scr_act(), 1000, 60);
    lv_obj_set_size(spinner, 40, 40);
    lv_obj_align(spinner, LV_ALIGN_TOP_RIGHT, -40, 5);
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);

    // Status label
    label_status = lv_label_create(lv_scr_act());
    lv_label_set_text(label_status, "Appuie sur SCAN");
    lv_obj_set_style_text_color(label_status, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(label_status, &lv_font_montserrat_14, 0);
    lv_obj_align(label_status, LV_ALIGN_TOP_LEFT, 15, 45);

    // Network list
    list_networks = lv_list_create(lv_scr_act());
    lv_obj_set_size(list_networks, 460, 240);
    lv_obj_align(list_networks, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(list_networks, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(list_networks, 0, 0);
    lv_obj_set_style_pad_row(list_networks, 5, 0);

    bsp_display_unlock();

    Serial.println("Setup complete!");

    // Auto-scan on startup
    do_scan();
}

void loop()
{
    delay(100);
}
