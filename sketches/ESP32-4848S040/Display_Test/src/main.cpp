/*
 * Display_Test - ESP32-4848S040
 * Test qualite ecran sans LVGL, directement avec Arduino_GFX.
 */

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#define SCREEN_W 480
#define SCREEN_H 480
#define GFX_BL 38

Arduino_DataBus *bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED, 39, 48, 47, GFX_NOT_DEFINED);

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18, 17, 16, 21,
    11, 12, 13, 14, 0,
    8, 20, 3, 46, 9, 10,
    4, 5, 6, 7, 15,
    1, 10, 8, 50,
    1, 10, 8, 20,
    1, 12000000,
    false, 0, 0, 480 * 20);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    SCREEN_W, SCREEN_H, rgbpanel, 0, true,
    bus, GFX_NOT_DEFINED,
    st7701_type9_init_operations, sizeof(st7701_type9_init_operations));

void setup() {
    Serial.begin(115200);

    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    gfx->begin();
    gfx->fillScreen(BLACK);

    // Test 1: couleurs pures
    gfx->fillRect(0, 0, 160, 120, RED);
    gfx->fillRect(160, 0, 160, 120, GREEN);
    gfx->fillRect(320, 0, 160, 120, BLUE);

    // Test 2: degrade gris
    for (int x = 0; x < 480; x++) {
        uint8_t g = (x * 255) / 479;
        uint16_t color = gfx->color565(g, g, g);
        gfx->drawFastVLine(x, 120, 60, color);
    }

    // Test 3: texte a differentes tailles
    gfx->setTextColor(WHITE);
    gfx->setTextSize(1);
    gfx->setCursor(10, 200);
    gfx->println("Texte taille 1 - ABCDEFGHIJKLMNOP 0123456789");

    gfx->setTextSize(2);
    gfx->setCursor(10, 220);
    gfx->println("Texte taille 2 - ABCDEF");

    gfx->setTextSize(3);
    gfx->setCursor(10, 250);
    gfx->println("Texte taille 3");

    gfx->setTextSize(4);
    gfx->setCursor(10, 290);
    gfx->println("Taille 4");

    // Test 4: lignes fines (1px)
    for (int i = 0; i < 10; i++) {
        gfx->drawFastHLine(10, 340 + i * 4, 460, WHITE);
    }

    // Test 5: damier 1px
    for (int y = 390; y < 430; y++) {
        for (int x = 10; x < 470; x++) {
            if ((x + y) % 2 == 0) {
                gfx->drawPixel(x, y, WHITE);
            }
        }
    }

    // Test 6: cercle
    gfx->drawCircle(240, 460, 15, YELLOW);
    gfx->fillCircle(280, 460, 15, CYAN);

    Serial.println("Display test done");
}

void loop() {
    delay(1000);
}
