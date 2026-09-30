/*
 * Claude_Dashboard - JC3248W535C
 *
 * Tableau de bord Claude Code : quotas du forfait, etat des sessions
 * (travaille / attente / permission) et contexte, recus d'un serveur
 * distant via MQTT/TLS. Bip et reveil de l'ecran quand une session
 * attend une reponse.
 *
 * Board: JC3248W535C (ESP32-S3 + LCD tactile 3.5")
 * FQBN: PlatformIO esp32-s3-devkitc-1
 *
 * @dependencies LVGL 8.3.x, ArduinoJson, PubSubClient
 */

#include <Arduino.h>
#include <lvgl.h>
#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"

void setup()
{
    Serial.begin(115200);

    // Paysage (480x320) : rotation 270 comme Transit_Tracker
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = EXAMPLE_LCD_QSPI_H_RES * EXAMPLE_LCD_QSPI_V_RES,
        .rotate = LV_DISP_ROT_270,
    };
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    bsp_display_lock(0);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x0f1419), 0);
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Claude Dashboard");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    bsp_display_unlock();
}

void loop()
{
    delay(1000);
}
