# Guide d'initialisation de l'afficheur JC3248W535C

## Résumé des informations clés

Après analyse du code de démonstration fourni par le vendeur, voici tout ce qu'il faut savoir pour faire fonctionner l'afficheur AXS15231B (contrôleur LCD QSPI).

## Configuration matérielle

### Pins de l'afficheur LCD (interface QSPI)

| Signal | GPIO | Description |
|--------|------|-------------|
| CS | 45 | Chip Select |
| SCK | 47 | Serial Clock (QSPI) |
| SDA0 | 21 | Data 0 (QSPI) |
| SDA1 | 48 | Data 1 (QSPI) |
| SDA2 | 40 | Data 2 (QSPI) |
| SDA3 | 39 | Data 3 (QSPI) |
| TE | 38 | Tear Effect (synchronisation verticale) |
| BL | 1 | Backlight (PWM) |
| RST | -1 | Reset (non utilisé, reset software uniquement) |

### Pins du tactile (I2C)

| Signal | GPIO |
|--------|------|
| SDA | 4 |
| SCL | 8 |
| INT | 3 |
| RST | -1 (non utilisé) |

### Configuration de l'afficheur

- **Résolution** : 320 × 480 pixels
- **Format couleur** : RGB565 (16 bits par pixel, big endian)
- **Interface** : QSPI (4 lignes de données)
- **Contrôleur** : AXS15231B (custom driver fourni)

## Prérequis logiciels

### Version ESP32 Arduino Core

⚠️ **CRITIQUE** : ESP32 Arduino Core **v3.0.2** obligatoire

Les versions plus récentes ou plus anciennes peuvent ne pas fonctionner correctement.

### Bibliothèques requises

1. **LVGL** (version fournie dans la doc vendeur)
   - Library graphique pour l'interface utilisateur
   - Configuration dans `lv_conf.h`

2. **Driver custom AXS15231B**
   - Fichiers fournis dans la doc : `esp_lcd_axs15231b.c/h`
   - Driver pour le contrôleur LCD

3. **ESP LCD Touch AXS15231B**
   - Fichier fourni : `esp_lcd_touch.c/h`
   - Driver pour le tactile capacitif intégré

4. **ESP LCD Port (LVGL port)**
   - Fichier fourni : `lv_port.c/h`
   - Intégration LVGL avec ESP32

5. **ESP BSP (Board Support Package)**
   - Fichier fourni : `esp_bsp.c/h`
   - Fonctions d'initialisation de haut niveau

### Bibliothèques optionnelles (selon usage)

- **ESP32_JPEG_Library** : pour affichage d'images JPEG
- **ESP32-audioI2S** : pour lecture audio via le DAC NS4168
- **SD_MMC** : pour accès à la carte SD

## Séquence d'initialisation

### 1. Configuration SPI QSPI

```c
const spi_bus_config_t buscfg = {
    .sclk_io_num = 47,          // SCK
    .data0_io_num = 21,         // D0
    .data1_io_num = 48,         // D1
    .data2_io_num = 40,         // D2
    .data3_io_num = 39,         // D3
    .max_transfer_sz = 320 * 480 * 2,  // Buffer taille max
    .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
};
ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
```

### 2. Configuration LCD Panel IO

```c
const esp_lcd_panel_io_spi_config_t io_config = {
    .cs_gpio_num = 45,
    .dc_gpio_num = -1,  // Pas de DC en QSPI
    .spi_mode = 0,
    .pclk_hz = 40 * 1000 * 1000,  // 40 MHz
    .trans_queue_depth = 10,
    .lcd_cmd_bits = 32,
    .lcd_param_bits = 8,
    .flags = {
        .dc_as_cmd_phase = 0,
        .dc_low_on_data = 0,
        .octal_mode = 0,
        .quad_mode = 1,  // QSPI mode
        .sio_mode = 0,
    },
};
```

### 3. Initialisation du contrôleur AXS15231B

Le driver custom utilise une séquence d'initialisation très spécifique (67 commandes) :

**Points critiques** :
- Commandes spécifiques au contrôleur AXS15231B
- Timing précis avec delays
- Configuration gamma, power, etc.

Voir le tableau `lcd_init_cmds[]` dans `esp_bsp.c` lignes 34-67 pour la séquence complète.

### 4. Configuration LVGL

```c
bsp_display_cfg_t cfg = {
    .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
    .buffer_size = 320 * 480,  // Taille du buffer LVGL
    .rotate = LV_DISP_ROT_90,  // Rotation 90° (mode paysage)
};

bsp_display_start_with_config(&cfg);
bsp_display_backlight_on();
```

### 5. Gestion de la synchronisation Tear Effect (TE)

Le signal TE (GPIO 38) permet de synchroniser le rafraîchissement avec l'afficheur :

```c
// Configuration de l'interruption TE
const gpio_config_t te_detect_cfg = {
    .intr_type = GPIO_INTR_NEGEDGE,  // Détection front descendant
    .mode = GPIO_MODE_INPUT,
    .pin_bit_mask = BIT64(38),
    .pull_up_en = GPIO_PULLUP_ENABLE,
};
```

Ceci évite le tearing (déchirure d'image) lors de l'affichage.

## Configuration PSRAM

Pour de bonnes performances :
- **PSRAM activé** : OUI
- **PSRAM speed** : 120 MHz (pour vidéo MJPEG)
- **Buffer LVGL en PSRAM** : OUI

Configuration dans Arduino IDE :
- Tools > PSRAM > "QSPI PSRAM"
- Tools > PSRAM Speed > "120MHz"

## Structure minimale d'un sketch

```cpp
#include <Arduino.h>
#include <lvgl.h>
#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"

#define LVGL_PORT_ROTATION_DEGREE (90)

void setup() {
    Serial.begin(115200);
    Serial.println("Initialisation afficheur JC3248W535C");

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

    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    // Création de l'interface LVGL
    bsp_display_lock(0);

    // Exemple : créer un label
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Hello JC3248W535C!");
    lv_obj_center(label);

    bsp_display_unlock();

    Serial.println("Afficheur initialisé");
}

void loop() {
    delay(1000);
}
```

## Fichiers à copier depuis la doc vendeur

Pour créer ton propre sketch, tu dois copier ces fichiers depuis `doc/JC3248W535EN/1-Demo/Demo_Arduino/DEMO_LVGL/` :

**Fichiers essentiels** :
- `display.h` et `display.c` (vide, juste des defines)
- `esp_bsp.h` et `esp_bsp.c` ⭐ (initialisation principale)
- `lv_port.h` et `lv_port.c` ⭐ (port LVGL)
- `esp_lcd_axs15231b.h` et `esp_lcd_axs15231b.c` ⭐ (driver LCD)
- `esp_lcd_touch.h` et `esp_lcd_touch.c` ⭐ (driver tactile)
- `bsp_err_check.h` (macros d'erreur)
- `pincfg.h` (optionnel, définitions de pins)

**Dossier LVGL** :
- Copier tout le dossier `libraries/lvgl/` dans ton dossier `libraries/` Arduino

## Problèmes courants et solutions

### 1. Écran noir / pas d'affichage

**Causes possibles** :
- ❌ Backlight non activé → Appeler `bsp_display_backlight_on()`
- ❌ PSRAM non activé → Activer QSPI PSRAM dans les options
- ❌ Mauvaise séquence d'init → Vérifier que tous les fichiers sont copiés
- ❌ Mauvaise version ESP32 Core → Utiliser **v3.0.2**

### 2. Affichage corrompu / couleurs bizarres

**Causes possibles** :
- ❌ Format couleur incorrect → Doit être RGB565 big endian
- ❌ Buffer size incorrect → Doit être >= 320*480 pixels
- ❌ Problème de timing QSPI → Vérifier la fréquence (40 MHz max)

### 3. Tactile ne répond pas

**Causes possibles** :
- ❌ I2C non initialisé → `bsp_i2c_init()` doit être appelé
- ❌ Adresse I2C incorrecte → Contrôleur AXS15231B (driver custom)
- ❌ Interruption tactile non configurée → GPIO 3 pour INT

### 4. Erreur de compilation

**Causes possibles** :
- ❌ Fichiers manquants → Copier tous les fichiers listés ci-dessus
- ❌ LVGL mal configuré → Vérifier `lv_conf.h`
- ❌ Mauvaises options de compilation → Partition scheme "Huge APP"

## Configuration Arduino IDE recommandée

```
Board: "ESP32S3 Dev Module"
CPU Frequency: "240MHz (WiFi)"
Flash Frequency: "80MHz"
Flash Mode: "QIO"
Flash Size: "16MB (128Mb)"
Partition Scheme: "16M Flash (3MB APP/9.9MB FATFS)"
PSRAM: "QSPI PSRAM"
PSRAM Speed: "120MHz"
Upload Speed: "921600"
USB CDC On Boot: "Enabled"
USB DFD On Boot: "Disabled"
USB Firmware MSC On Boot: "Disabled"
Upload Mode: "UART0 / Hardware CDC"
USB Mode: "Hardware CDC and JTAG"
Core Debug Level: "None" ou "Info" pour debug
```

## Exemples fournis par le vendeur

Dans `doc/JC3248W535EN/1-Demo/Demo_Arduino/` :

1. **DEMO_LVGL** : Interface graphique LVGL (démo widgets)
2. **DEMO_PIC** : Affichage d'images JPEG depuis carte SD
3. **DEMO_MJPEG** : Lecture vidéo MJPEG depuis carte SD
4. **DEMO_MP3** : Lecture audio MP3 depuis carte SD

Tous ces exemples utilisent la même base d'initialisation décrite ci-dessus.

## Notes importantes

- Le contrôleur AXS15231B n'est **PAS un contrôleur standard** (pas ILI9341, ST7789, etc.)
- Il nécessite un **driver custom** fourni par le vendeur
- La séquence d'initialisation est **très spécifique** et ne peut pas être simplifiée
- Le tactile est intégré au contrôleur (même circuit I2C)

## Prochaines étapes

1. Créer un dossier de sketch avec tous les fichiers nécessaires
2. Configurer Arduino IDE avec les bonnes options
3. Compiler et uploader un exemple simple
4. Tester le backlight en premier
5. Tester l'affichage avec un simple label LVGL
6. Ajouter le tactile une fois l'affichage fonctionnel

Bonne chance ! 🚀
