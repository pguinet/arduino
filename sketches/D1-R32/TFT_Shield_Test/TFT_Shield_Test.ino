/*
 * TFT_Shield_Test - WEMOS D1 R32 + Shield TFT 3.5"
 *
 * Test du shield TFT 3.5" (8-bit parallele) sur D1 R32.
 * Attention : la broche RS (DC) du shield est cablee sur A2,
 * qui sur D1 R32 standard correspond a GPIO 35 (input only).
 * Si l'ecran reste blanc, le mapping de cette carte ne convient pas.
 *
 * Board: WEMOS D1 R32 (ESP32)
 * FQBN: esp32:esp32:d1_uno32
 *
 * @dependencies LovyanGFX
 */

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9486 _panel_instance;
  lgfx::Bus_Parallel8 _bus_instance;

public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.freq_write = 16000000;
      cfg.pin_wr = 4;    // A1
      cfg.pin_rd = 2;    // A0
      cfg.pin_rs = 35;   // A2 (DC)  <- INPUT-ONLY si D1 R32 standard
      cfg.pin_d0 = 12;   // D8
      cfg.pin_d1 = 13;   // D9
      cfg.pin_d2 = 26;   // D2
      cfg.pin_d3 = 25;   // D3
      cfg.pin_d4 = 17;   // D4
      cfg.pin_d5 = 16;   // D5
      cfg.pin_d6 = 27;   // D6
      cfg.pin_d7 = 14;   // D7
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 34;   // A3 (input-only)
      cfg.pin_rst = 36;  // A4 (input-only)
      cfg.pin_busy = -1;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

LGFX tft;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("TFT shield test - D1 R32");

  tft.init();
  tft.setRotation(1);

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(3);
  tft.setCursor(20, 20);
  tft.println("Club Domontois");
  tft.setTextColor(TFT_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(20, 60);
  tft.println("D1 R32 + TFT 3.5");

  tft.fillRect(20, 100, 80, 80, TFT_RED);
  tft.fillRect(120, 100, 80, 80, TFT_GREEN);
  tft.fillRect(220, 100, 80, 80, TFT_BLUE);

  for (int i = 0; i < 30; i++) {
    int x = random(tft.width());
    int y = random(200, tft.height());
    int r = random(5, 25);
    tft.fillCircle(x, y, r, tft.color565(random(256), random(256), random(256)));
  }

  Serial.println("Setup done");
}

void loop() {
  delay(1000);
}
