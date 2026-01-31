/*
 * TouchTest - JC3248W535C
 *
 * Test basique du tactile : affiche les coordonnees X,Y en temps reel
 * et dessine un point a l'endroit touche.
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

#define LVGL_PORT_ROTATION_DEGREE (90)

// UI elements
static lv_obj_t *label_title;
static lv_obj_t *label_coords;
static lv_obj_t *label_status;
static lv_obj_t *touch_area;

// Touch event callback
static void touch_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);

    if (code == LV_EVENT_PRESSING) {
        lv_indev_t *indev = lv_indev_get_act();
        if (indev) {
            lv_point_t point;
            lv_indev_get_point(indev, &point);

            // Update coordinates
            char buf[32];
            snprintf(buf, sizeof(buf), "X: %3d  Y: %3d", point.x, point.y);
            lv_label_set_text(label_coords, buf);
            lv_obj_set_style_text_color(label_coords, lv_color_hex(0x00ff88), 0);

            lv_label_set_text(label_status, "Contact!");
            lv_obj_set_style_text_color(label_status, lv_color_hex(0x00ff88), 0);

            Serial.printf("Touch: X=%d Y=%d\n", point.x, point.y);
        }
    }
    else if (code == LV_EVENT_RELEASED) {
        lv_label_set_text(label_status, "Relache - touche encore!");
        lv_obj_set_style_text_color(label_status, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_color(label_coords, lv_color_hex(0xffffff), 0);
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.println("TouchTest - JC3248W535C");

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
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x1a1a2e), 0);

    // Title
    label_title = lv_label_create(lv_scr_act());
    lv_label_set_text(label_title, "TouchTest");
    lv_obj_set_style_text_color(label_title, lv_color_hex(0x00ff88), 0);
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_28, 0);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 20);

    // Coordinates display
    label_coords = lv_label_create(lv_scr_act());
    lv_label_set_text(label_coords, "X: ---  Y: ---");
    lv_obj_set_style_text_color(label_coords, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(label_coords, &lv_font_montserrat_48, 0);
    lv_obj_align(label_coords, LV_ALIGN_CENTER, 0, 0);

    // Status
    label_status = lv_label_create(lv_scr_act());
    lv_label_set_text(label_status, "Touche l'ecran!");
    lv_obj_set_style_text_color(label_status, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(label_status, &lv_font_montserrat_20, 0);
    lv_obj_align(label_status, LV_ALIGN_BOTTOM_MID, 0, -30);

    // Transparent touch area covering the whole screen
    touch_area = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(touch_area);
    lv_obj_set_size(touch_area, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(touch_area, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(touch_area, touch_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(touch_area, touch_event_cb, LV_EVENT_RELEASED, NULL);

    bsp_display_unlock();

    Serial.println("Setup complete - touch the screen!");
}

void loop()
{
    delay(100);
}
