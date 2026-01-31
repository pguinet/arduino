/*
 * System_Monitor - JC3248W535C
 *
 * Dashboard systeme avec jauges : RAM, PSRAM, uptime, CPU.
 *
 * Board: JC3248W535C (ESP32-S3 + LCD tactile 3.5")
 * FQBN: PlatformIO esp32-s3-devkitc-1
 *
 * @dependencies LVGL 8.3.x
 */

#include <Arduino.h>
#include <lvgl.h>
#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_chip_info.h"

#define LVGL_PORT_ROTATION_DEGREE (90)

// Colors
#define COLOR_BG        0x1a1a2e
#define COLOR_CARD      0x16213e
#define COLOR_TEXT      0xffffff
#define COLOR_ACCENT    0x00ff88
#define COLOR_RAM       0x4cc9f0
#define COLOR_PSRAM     0xf72585
#define COLOR_CPU       0xfca311

// UI elements
static lv_obj_t *arc_ram;
static lv_obj_t *arc_psram;
static lv_obj_t *label_ram_pct;
static lv_obj_t *label_ram_val;
static lv_obj_t *label_psram_pct;
static lv_obj_t *label_psram_val;
static lv_obj_t *label_uptime;
static lv_obj_t *label_cpu;
static lv_obj_t *label_chip;

// System info
static size_t total_ram = 0;
static size_t total_psram = 0;

static lv_obj_t* create_gauge_card(lv_obj_t *parent, const char *title, lv_color_t color,
                                    lv_obj_t **arc_out, lv_obj_t **pct_out, lv_obj_t **val_out)
{
    // Card container
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 150, 140);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 15, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_color(lbl_title, color, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 0);

    // Arc gauge
    lv_obj_t *arc = lv_arc_create(card);
    lv_obj_set_size(arc, 80, 80);
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

    // Percentage label (inside arc)
    lv_obj_t *lbl_pct = lv_label_create(card);
    lv_label_set_text(lbl_pct, "0%");
    lv_obj_set_style_text_color(lbl_pct, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(lbl_pct, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl_pct, LV_ALIGN_CENTER, 0, 3);
    *pct_out = lbl_pct;

    // Value label (bottom)
    lv_obj_t *lbl_val = lv_label_create(card);
    lv_label_set_text(lbl_val, "--- KB");
    lv_obj_set_style_text_color(lbl_val, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(lbl_val, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_val, LV_ALIGN_BOTTOM_MID, 0, -2);
    *val_out = lbl_val;

    return card;
}

static lv_obj_t* create_info_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 460, 110);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 15, 0);
    lv_obj_set_style_pad_all(card, 15, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Left column: Chip + CPU
    lv_obj_t *lbl_chip_title = lv_label_create(card);
    lv_label_set_text(lbl_chip_title, LV_SYMBOL_SETTINGS " Chip");
    lv_obj_set_style_text_color(lbl_chip_title, lv_color_hex(COLOR_CPU), 0);
    lv_obj_set_style_text_font(lbl_chip_title, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_chip_title, LV_ALIGN_TOP_LEFT, 0, 0);

    label_chip = lv_label_create(card);
    lv_label_set_text(label_chip, "ESP32-S3");
    lv_obj_set_style_text_color(label_chip, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label_chip, &lv_font_montserrat_14, 0);
    lv_obj_align(label_chip, LV_ALIGN_TOP_LEFT, 0, 22);

    lv_obj_t *lbl_cpu_title = lv_label_create(card);
    lv_label_set_text(lbl_cpu_title, LV_SYMBOL_CHARGE " CPU");
    lv_obj_set_style_text_color(lbl_cpu_title, lv_color_hex(COLOR_CPU), 0);
    lv_obj_set_style_text_font(lbl_cpu_title, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_cpu_title, LV_ALIGN_TOP_LEFT, 0, 50);

    label_cpu = lv_label_create(card);
    lv_label_set_text(label_cpu, "240 MHz");
    lv_obj_set_style_text_color(label_cpu, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label_cpu, &lv_font_montserrat_14, 0);
    lv_obj_align(label_cpu, LV_ALIGN_TOP_LEFT, 70, 50);

    // Right side: Uptime (big)
    lv_obj_t *lbl_uptime_title = lv_label_create(card);
    lv_label_set_text(lbl_uptime_title, LV_SYMBOL_LOOP " Uptime");
    lv_obj_set_style_text_color(lbl_uptime_title, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(lbl_uptime_title, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_uptime_title, LV_ALIGN_TOP_RIGHT, 0, 0);

    label_uptime = lv_label_create(card);
    lv_label_set_text(label_uptime, "00:00:00");
    lv_obj_set_style_text_color(label_uptime, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label_uptime, &lv_font_montserrat_48, 0);
    lv_obj_align(label_uptime, LV_ALIGN_RIGHT_MID, 0, 10);

    return card;
}

void update_display()
{
    // Get memory info
    size_t free_ram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    int ram_used_pct = 100 - (free_ram * 100 / total_ram);
    int psram_used_pct = 100 - (free_psram * 100 / total_psram);

    // Update RAM gauge
    lv_arc_set_value(arc_ram, ram_used_pct);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%", ram_used_pct);
    lv_label_set_text(label_ram_pct, buf);
    snprintf(buf, sizeof(buf), "%d / %d KB", (total_ram - free_ram) / 1024, total_ram / 1024);
    lv_label_set_text(label_ram_val, buf);

    // Update PSRAM gauge
    lv_arc_set_value(arc_psram, psram_used_pct);
    snprintf(buf, sizeof(buf), "%d%%", psram_used_pct);
    lv_label_set_text(label_psram_pct, buf);
    snprintf(buf, sizeof(buf), "%.1f / %.1f MB", (total_psram - free_psram) / 1048576.0, total_psram / 1048576.0);
    lv_label_set_text(label_psram_val, buf);

    // Update uptime
    unsigned long secs = millis() / 1000;
    unsigned long mins = secs / 60;
    unsigned long hours = mins / 60;
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", hours, mins % 60, secs % 60);
    lv_label_set_text(label_uptime, buf);

    // Update CPU frequency
    snprintf(buf, sizeof(buf), "%d MHz", getCpuFrequencyMhz());
    lv_label_set_text(label_cpu, buf);
}

void setup()
{
    Serial.begin(115200);
    Serial.println("System_Monitor - JC3248W535C");

    // Get total memory
    total_ram = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    Serial.printf("Total RAM: %d KB, Total PSRAM: %d KB\n", total_ram / 1024, total_psram / 1024);

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
    lv_obj_t *label_title = lv_label_create(lv_scr_act());
    lv_label_set_text(label_title, "System Monitor");
    lv_obj_set_style_text_color(label_title, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_24, 0);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 10);

    // Get chip info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    // Create cards: 2 gauges on top, info card below
    // RAM card (top left)
    lv_obj_t *card_ram = create_gauge_card(lv_scr_act(), "RAM Interne",
        lv_color_hex(COLOR_RAM), &arc_ram, &label_ram_pct, &label_ram_val);
    lv_obj_align(card_ram, LV_ALIGN_TOP_LEFT, 10, 45);

    // PSRAM card (top right)
    lv_obj_t *card_psram = create_gauge_card(lv_scr_act(), "PSRAM",
        lv_color_hex(COLOR_PSRAM), &arc_psram, &label_psram_pct, &label_psram_val);
    lv_obj_align(card_psram, LV_ALIGN_TOP_RIGHT, -10, 45);

    // Info card (bottom, full width)
    lv_obj_t *card_info = create_info_card(lv_scr_act());
    lv_obj_align(card_info, LV_ALIGN_BOTTOM_MID, 0, -40);

    // Set chip info
    char chip_buf[64];
    snprintf(chip_buf, sizeof(chip_buf), "ESP32-S3 %d cores", chip_info.cores);
    lv_label_set_text(label_chip, chip_buf);

    // Initial update
    update_display();

    bsp_display_unlock();

    Serial.println("Setup complete!");
}

void loop()
{
    static unsigned long last_update = 0;

    if (millis() - last_update > 1000) {
        bsp_display_lock(0);
        update_display();
        bsp_display_unlock();
        last_update = millis();
    }

    delay(50);
}
