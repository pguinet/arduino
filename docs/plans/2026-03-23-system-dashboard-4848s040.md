# System Dashboard ESP32-4848S040 — Plan d'implémentation

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:executing-plans to implement this plan task-by-task.

**Goal:** Créer un dashboard système interactif pour la carte ESP32-4848S040C_I_Y_3 (Guition 4" 480×480) avec jauges mémoire, infos WiFi, contrôle des 3 relais par boutons tactiles.

**Architecture:** PlatformIO + Arduino framework. Arduino_GFX pour le driver écran ST7701S (RGB parallèle), TAMC_GT911 pour le tactile, LVGL 8.x pour l'interface. Un seul fichier `main.cpp` avec les helpers display/touch intégrés (pas de BSP custom comme la JC3248W535C).

**Tech Stack:** ESP32-S3, Arduino_GFX, LVGL 8.4, TAMC_GT911, WiFi ESP32

---

### Task 0: Scaffolding du projet PlatformIO

**Files:**
- Create: `sketches/ESP32-4848S040/System_Dashboard/platformio.ini`
- Create: `sketches/ESP32-4848S040/System_Dashboard/include/lv_conf.h`
- Create: `sketches/ESP32-4848S040/System_Dashboard/src/main.cpp` (squelette vide)

**Step 1: Créer `platformio.ini`**

```ini
; System_Dashboard - ESP32-4848S040
; Dashboard système avec jauges RAM/PSRAM, WiFi, contrôle relais

[env:esp32s3]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/51.03.07/platform-espressif32.zip
board = esp32-s3-devkitm-1
framework = arduino
upload_port = /dev/ttyUSB0
monitor_port = /dev/ttyUSB0
monitor_speed = 115200

board_build.mcu = esp32s3
board_build.f_cpu = 240000000L
board_build.arduino.memory_type = qio_opi
board_build.flash_mode = qio
board_build.psram_type = opi
board_upload.flash_size = 16MB

build_flags =
    -DARDUINO_USB_MODE=0
    -DARDUINO_USB_CDC_ON_BOOT=0
    -DBOARD_HAS_PSRAM
    -DLV_CONF_INCLUDE_SIMPLE
    -I${PROJECT_DIR}/include

lib_deps =
    moononournation/GFX Library for Arduino@^1.5.6
    lvgl/lvgl@^8.4.0
    tamctec/TAMC_GT911@^1.0.2
```

**Step 2: Créer `include/lv_conf.h`**

Config LVGL 8.x pour écran 480×480 RGB parallèle :
- `LV_COLOR_DEPTH 16`, `LV_COLOR_16_SWAP 0` (RGB parallèle, pas de swap)
- `LV_MEM_SIZE 48*1024`
- `LV_TICK_CUSTOM 1` avec `millis()`
- `LV_DPI_DEF 175`
- Fonts : montserrat 12, 14, 16, 20, 24, 28, 32, 48
- Widgets : arc, bar, btn, label, switch, led + meter, chart
- Theme dark activé

**Step 3: Créer `src/main.cpp` squelette**

```cpp
/*
 * System_Dashboard - ESP32-4848S040
 *
 * Dashboard système interactif : jauges RAM/PSRAM, infos WiFi,
 * contrôle des 3 relais par boutons tactiles.
 *
 * Board: ESP32-4848S040C_I_Y_3 (Guition 4" 480x480 IPS)
 * FQBN: PlatformIO esp32-s3-devkitm-1
 *
 * @dependencies Arduino_GFX, LVGL 8.4, TAMC_GT911
 */

#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    Serial.println("System_Dashboard - ESP32-4848S040");
}

void loop() {
    delay(100);
}
```

**Step 4: Vérifier que ça compile**

```bash
cd sketches/ESP32-4848S040/System_Dashboard && pio run
```

**Step 5: Commit**

```bash
git add sketches/ESP32-4848S040/System_Dashboard/
git commit -m "ESP32-4848S040: scaffolding System_Dashboard (PlatformIO)"
```

---

### Task 1: Driver écran ST7701S + backlight

**Files:**
- Modify: `src/main.cpp`

**Step 1: Ajouter l'init Arduino_GFX pour ST7701S RGB parallèle**

```cpp
#include <Arduino_GFX_Library.h>

#define GFX_BL 38

// SPI bus pour init ST7701S (software SPI, libère les pins pour SD plus tard)
Arduino_DataBus *bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED, 39 /* CS */, 48 /* SCK */, 47 /* MOSI */, GFX_NOT_DEFINED);

// Panel RGB 16-bit parallèle
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
    11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0 /* R4 */,
    8 /* G0 */, 20 /* G1 */, 3 /* G2 */, 46 /* G3 */, 9 /* G4 */, 10 /* G5 */,
    4 /* B0 */, 5 /* B1 */, 6 /* B2 */, 7 /* B3 */, 15 /* B4 */,
    1, 10, 8, 50,   // hsync: polarity, front_porch, pulse_width, back_porch
    1, 10, 8, 20,   // vsync: polarity, front_porch, pulse_width, back_porch
    0, 12000000);    // pclk_active_neg, prefer_speed

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    480, 480, rgbpanel, 0, true,
    bus, GFX_NOT_DEFINED,
    st7701_type9_init_operations, sizeof(st7701_type9_init_operations));
```

**Step 2: Init dans setup()**

```cpp
gfx->begin();
pinMode(GFX_BL, OUTPUT);
digitalWrite(GFX_BL, HIGH);
gfx->fillScreen(BLACK);
gfx->setTextColor(WHITE);
gfx->setTextSize(3);
gfx->setCursor(100, 200);
gfx->println("Display OK!");
```

**Step 3: Compiler et tester**

```bash
pio run && pio run -t upload
```

Résultat attendu : écran allumé avec "Display OK!" en blanc sur noir.

**Step 4: Commit**

```bash
git commit -am "ESP32-4848S040: driver écran ST7701S fonctionnel"
```

---

### Task 2: Driver tactile GT911 + intégration LVGL

**Files:**
- Modify: `src/main.cpp`

**Step 1: Ajouter le touch GT911**

```cpp
#include <Wire.h>
#include <TAMC_GT911.h>

#define TP_SDA 19
#define TP_SCL 45
#define TP_INT -1
#define TP_RST -1

TAMC_GT911 touch(TP_SDA, TP_SCL, TP_INT, TP_RST, 480, 480);
```

**Step 2: Initialiser LVGL avec display driver et touch input**

```cpp
#include <lvgl.h>

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf;

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    touch.read();
    if (touch.isTouched) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touch.points[0].x;
        data->point.y = touch.points[0].y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}
```

Setup LVGL :
- Buffer PSRAM plein écran (480×480×2 = 460KB)
- Register display driver + input driver
- `lv_timer_handler()` dans loop()

**Step 3: Test rapide : label LVGL + toucher**

Afficher "Touch me!" et quand on touche, afficher les coordonnées (comme le TouchTest de la JC3248W535C).

**Step 4: Compiler, uploader, tester**

```bash
pio run && pio run -t upload
```

**Step 5: Commit**

```bash
git commit -am "ESP32-4848S040: tactile GT911 + LVGL fonctionnels"
```

---

### Task 3: UI du dashboard — jauges RAM/PSRAM + infos système

**Files:**
- Modify: `src/main.cpp`

**Step 1: Créer le layout**

Écran 480×480 carré, layout :
```
┌──────────────────────────────┐
│     System Dashboard         │
├──────────────┬───────────────┤
│   RAM        │   PSRAM       │
│   [arc]      │   [arc]       │
│   xx%        │   xx%         │
│   xxx/xxx KB │  x.x/x.x MB  │
├──────────────┴───────────────┤
│ ⚡ ESP32-S3 2 cores  240 MHz │
│ 🔄 Uptime: 00:00:00         │
├──────────────────────────────┤
│  WiFi: SSID     IP: x.x.x.x │
│  RSSI: -xx dBm              │
└──────────────────────────────┘
```

**Step 2: Reprendre les composants du System_Monitor JC3248W535C**

- `create_gauge_card()` pour RAM et PSRAM (adapter les tailles pour 480×480)
- Card infos système (chip, CPU, uptime)
- Ajouter section WiFi (SSID, IP, RSSI)

**Step 3: Fonction `update_display()` rafraîchie toutes les secondes**

- RAM/PSRAM via `heap_caps_get_free_size()`
- Uptime via `millis()`
- WiFi status via `WiFi.status()`, `WiFi.RSSI()`, `WiFi.localIP()`

**Step 4: Compiler et tester**

```bash
pio run && pio run -t upload
```

**Step 5: Commit**

```bash
git commit -am "ESP32-4848S040: UI dashboard avec jauges et infos système"
```

---

### Task 4: Connexion WiFi + affichage état

**Files:**
- Modify: `src/main.cpp`
- Read: `sketches/common/credentials.h`

**Step 1: Ajouter WiFi avec credentials**

```cpp
#include <WiFi.h>
#include "credentials.h"  // symlink vers common/credentials.h
```

Connexion non-bloquante dans setup. Affichage "Connexion..." puis SSID/IP/RSSI une fois connecté.

**Step 2: Créer le symlink credentials.h**

```bash
ln -s /home/pascal/github/arduino/sketches/common/credentials.h \
      sketches/ESP32-4848S040/System_Dashboard/src/credentials.h
```

**Step 3: Mettre à jour l'affichage WiFi dans `update_display()`**

- Connecté : SSID, IP, RSSI avec barre de signal colorée
- Déconnecté : "Non connecté" en rouge, tentative de reconnexion

**Step 4: Compiler, uploader, tester**

**Step 5: Commit**

```bash
git commit -am "ESP32-4848S040: connexion WiFi avec affichage état"
```

---

### Task 5: Contrôle des 3 relais par boutons tactiles

**Files:**
- Modify: `src/main.cpp`

**Step 1: Init GPIO relais**

```cpp
#define RELAY_1 40
#define RELAY_2 2
#define RELAY_3 1

bool relay_state[3] = {false, false, false};
const int relay_pins[3] = {RELAY_1, RELAY_2, RELAY_3};
```

**Step 2: Créer 3 boutons toggle LVGL**

Gros boutons (140×80) en bas de l'écran, avec :
- Label "Relais 1/2/3"
- État ON (vert) / OFF (gris)
- Switch LVGL ou bouton coloré
- Callback qui toggle le GPIO + met à jour le visuel

```
┌──────────────────────────────┐
│  [R1 OFF]  [R2 OFF]  [R3 ON]│
└──────────────────────────────┘
```

**Step 3: Ajouter les callbacks**

```cpp
static void relay_btn_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    relay_state[idx] = !relay_state[idx];
    digitalWrite(relay_pins[idx], relay_state[idx] ? HIGH : LOW);
    // Update button style
}
```

**Step 4: Compiler, uploader, tester les relais**

On devrait entendre les relais cliquer quand on touche les boutons.

**Step 5: Commit**

```bash
git commit -am "ESP32-4848S040: contrôle des 3 relais par boutons tactiles"
```

---

### Task 6: Mise à jour CLAUDE.md + polish final

**Files:**
- Modify: `CLAUDE.md` — ajouter section ESP32-4848S040

**Step 1: Ajouter la doc de la carte dans CLAUDE.md**

Section complète avec : MCU, pinout écran/touch/relais/SD, compilation PlatformIO, sketches disponibles.

**Step 2: Ajouter le .gitignore PlatformIO**

```
.pio/
```

**Step 3: Test final complet**

- Compiler proprement (`pio run`)
- Upload + vérifier tous les composants visuellement

**Step 4: Commit final**

```bash
git commit -am "Ajouter section ESP32-4848S040 dans CLAUDE.md"
```
