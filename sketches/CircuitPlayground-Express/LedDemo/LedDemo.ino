#include <Adafruit_CircuitPlayground.h>

uint8_t offset = 0;

void setup() {
  CircuitPlayground.begin();
  CircuitPlayground.setBrightness(30);
}

void loop() {
  // Animation arc-en-ciel rotatif
  for (int i = 0; i < 10; i++) {
    uint8_t hue = (i * 25 + offset) % 256;
    uint32_t color = CircuitPlayground.colorWheel(hue);
    CircuitPlayground.setPixelColor(i, color);
  }

  offset += 2;
  delay(50);
}
