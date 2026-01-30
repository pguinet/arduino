/*
 * HelloWorld - JC3248W535C
 *
 * Premier sketch de test pour l'afficheur JC3248W535C.
 * Affiche un simple message "Hello World!" au centre de l'écran.
 *
 * Board: ESP32S3 Dev Module
 * FQBN: esp32:esp32:esp32s3
 *
 * Configuration Arduino IDE requise :
 * - ESP32 Arduino Core v3.0.2
 * - PSRAM: "QSPI PSRAM"
 * - PSRAM Speed: "120MHz"
 * - Partition Scheme: "16M Flash (3MB APP/9.9MB FATFS)"
 * - CPU Frequency: "240MHz (WiFi)"
 *
 * @dependencies LVGL (fournie dans libraries/)
 *
 * IMPORTANT : Les fichiers lib/*.c et lib/*.h doivent être
 * dans le dossier ../lib/ pour que la compilation fonctionne.
 */

#include <Arduino.h>
#include <lvgl.h>
#include "../lib/display.h"
#include "../lib/esp_bsp.h"
#include "../lib/lv_port.h"

/**
 * Configuration de la rotation de l'écran :
 *      - 0: 0 degré (portrait)
 *      - 90: 90 degrés (paysage)
 *      - 180: 180 degrés (portrait inversé)
 *      - 270: 270 degrés (paysage inversé)
 */
#define LVGL_PORT_ROTATION_DEGREE (90)

void setup()
{
    String title = "HelloWorld - JC3248W535C";

    Serial.begin(115200);
    Serial.println(title + " - Démarrage");

    Serial.println("Initialisation de l'afficheur...");

    // Configuration de l'afficheur
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = EXAMPLE_LCD_QSPI_H_RES * EXAMPLE_LCD_QSPI_V_RES,
#if LVGL_PORT_ROTATION_DEGREE == 90
        .rotate = LV_DISP_ROT_90,
#elif LVGL_PORT_ROTATION_DEGREE == 270
        .rotate = LV_DISP_ROT_270,
#elif LVGL_PORT_ROTATION_DEGREE == 180
        .rotate = LV_DISP_ROT_180,
#elif LVGL_PORT_ROTATION_DEGREE == 0
        .rotate = LV_DISP_ROT_NONE,
#endif
    };

    // Initialisation de l'afficheur et de LVGL
    bsp_display_start_with_config(&cfg);

    // Allumer le backlight
    bsp_display_backlight_on();

    Serial.println("Création de l'interface...");

    /* Verrouiller le mutex LVGL (les APIs LVGL ne sont pas thread-safe) */
    bsp_display_lock(0);

    // Créer un label au centre de l'écran
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Hello World!\n\nJC3248W535C\nESP32-S3");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);

    // Augmenter la taille de la police
    static lv_style_t style_large;
    lv_style_init(&style_large);
    lv_style_set_text_font(&style_large, &lv_font_montserrat_32);
    lv_obj_add_style(label, &style_large, 0);

    // Changer la couleur du texte
    lv_obj_set_style_text_color(label, lv_color_hex(0x00FF00), 0);

    /* Déverrouiller le mutex */
    bsp_display_unlock();

    Serial.println(title + " - Initialisé avec succès !");
    Serial.println("L'afficheur devrait maintenant afficher 'Hello World!'");
}

void loop()
{
    // LVGL tourne dans sa propre tâche, rien à faire dans loop()
    delay(1000);
}
