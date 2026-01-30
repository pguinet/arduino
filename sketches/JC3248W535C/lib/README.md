# Bibliothèque de drivers pour JC3248W535C

Ce dossier contient tous les fichiers de code nécessaires pour faire fonctionner l'afficheur LCD et le tactile de la carte JC3248W535C.

## Fichiers inclus

### Drivers LCD

#### `esp_lcd_axs15231b.c` / `esp_lcd_axs15231b.h`
Driver custom pour le contrôleur LCD AXS15231B (interface QSPI).

**Fonctionnalités** :
- Initialisation du contrôleur avec séquence propriétaire (67 commandes)
- Support QSPI 4 lignes de données
- Gestion RGB565 big endian
- Fonctions de base : reset, init, draw_bitmap, invert_color, mirror, swap_xy

**API principale** :
```c
esp_err_t esp_lcd_new_panel_axs15231b(
    const esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_dev_config_t *panel_dev_config,
    esp_lcd_panel_handle_t *ret_panel
);
```

#### `display.h`
Définitions et constantes pour l'afficheur.

**Constantes importantes** :
- `EXAMPLE_LCD_QSPI_H_RES` : 320 (résolution horizontale)
- `EXAMPLE_LCD_QSPI_V_RES` : 480 (résolution verticale)
- `BSP_LCD_COLOR_FORMAT` : RGB565
- `BSP_LCD_BIGENDIAN` : 1 (big endian activé)
- `BSP_LCD_BITS_PER_PIXEL` : 16

### Drivers Tactile

#### `esp_lcd_touch.c` / `esp_lcd_touch.h`
Driver pour le tactile capacitif intégré au contrôleur AXS15231B.

**Fonctionnalités** :
- Communication I2C avec le contrôleur tactile
- Support d'un point de contact
- Callbacks pour les interruptions tactiles
- Transformation des coordonnées selon la rotation

**API principale** :
```c
esp_err_t esp_lcd_touch_new_i2c_axs15231b(
    const esp_lcd_panel_io_handle_t io,
    const esp_lcd_touch_config_t *config,
    esp_lcd_touch_handle_t *tp
);
```

### Board Support Package (BSP)

#### `esp_bsp.c` / `esp_bsp.h`
Fonctions de haut niveau pour initialiser tous les périphériques de la carte.

**Fonctionnalités** :
- Initialisation complète de l'afficheur LCD
- Initialisation du tactile
- Gestion du backlight (PWM)
- Configuration I2C
- Intégration avec LVGL
- Gestion du signal Tear Effect (TE) pour éviter le tearing

**API principale** :
```c
// Initialiser l'afficheur avec LVGL
lv_disp_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg);

// Contrôle du backlight
esp_err_t bsp_display_backlight_on(void);
esp_err_t bsp_display_backlight_off(void);
esp_err_t bsp_display_brightness_set(int brightness_percent);

// Verrouillage LVGL (thread-safe)
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock(void);

// Initialisation I2C
esp_err_t bsp_i2c_init(void);
```

**Configuration de l'afficheur** :
```c
typedef struct {
    lvgl_port_cfg_t lvgl_port_cfg;  // Config LVGL
    uint32_t buffer_size;            // Taille buffer (pixels)
    lv_disp_rot_t rotate;            // Rotation écran
} bsp_display_cfg_t;
```

**Pins utilisées** :
- LCD QSPI : CS=45, SCK=47, SDA0-3=21/48/40/39, TE=38, BL=1
- Tactile I2C : SDA=4, SCL=8, INT=3
- I2C : I2C_NUM_0 @ 400kHz

### Port LVGL

#### `lv_port.c` / `lv_port.h`
Adaptation de LVGL pour ESP32 avec support des périphériques.

**Fonctionnalités** :
- Tâche LVGL dédiée (priorité 4, stack 4096)
- Timer LVGL (tick 5ms)
- Gestion des buffers en PSRAM
- Support du tactile
- Callbacks de synchronisation

**API principale** :
```c
// Initialiser LVGL
esp_err_t lvgl_port_init(const lvgl_port_cfg_t *cfg);

// Ajouter l'afficheur à LVGL
lv_disp_t *lvgl_port_add_disp(const lvgl_port_display_cfg_t *disp_cfg);

// Ajouter le tactile à LVGL
lv_indev_t *lvgl_port_add_touch(const lvgl_port_touch_cfg_t *touch_cfg);

// Verrouillage thread-safe
bool lvgl_port_lock(uint32_t timeout_ms);
void lvgl_port_unlock(void);
```

### Configuration LVGL

#### `lv_conf.h`
Configuration complète de LVGL pour cette carte.

**Paramètres importants** :
- `LV_COLOR_DEPTH` : 16 (RGB565)
- `LV_COLOR_16_SWAP` : 1 (big endian)
- `LV_MEM_SIZE` : Défini selon la RAM disponible
- `LV_USE_PERF_MONITOR` : Activable pour debug
- Démos activées : `LV_USE_DEMO_WIDGETS`, `LV_USE_DEMO_STRESS`

### Utilitaires

#### `bsp_err_check.h`
Macros pour la gestion d'erreurs.

**Macros disponibles** :
```c
BSP_ERROR_CHECK_RETURN_ERR(x)   // Retourne l'erreur si échec
BSP_ERROR_CHECK_RETURN_NULL(x)  // Retourne NULL si échec
BSP_NULL_CHECK(x, ret)          // Vérifie NULL et retourne ret
```

## Utilisation dans un sketch

### Structure de dossiers requise

```
JC3248W535C/
├── lib/                    # Ce dossier
│   ├── *.c
│   └── *.h
└── MonSketch/
    └── MonSketch.ino
```

### Includes nécessaires

```cpp
#include <Arduino.h>
#include <lvgl.h>
#include "../lib/display.h"
#include "../lib/esp_bsp.h"
#include "../lib/lv_port.h"
```

### Initialisation minimale

```cpp
void setup() {
    // Configuration
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = EXAMPLE_LCD_QSPI_H_RES * EXAMPLE_LCD_QSPI_V_RES,
        .rotate = LV_DISP_ROT_90,  // Mode paysage
    };

    // Initialisation
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    // Utilisation LVGL (toujours avec lock/unlock)
    bsp_display_lock(0);
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Hello!");
    lv_obj_center(label);
    bsp_display_unlock();
}
```

## Bibliothèque LVGL requise

⚠️ **IMPORTANT** : Ces fichiers nécessitent la bibliothèque LVGL.

**Installation** :
1. Copier le dossier `doc/JC3248W535EN/1-Demo/Demo_Arduino/DEMO_LVGL/libraries/lvgl/`
2. Le placer dans le dossier `libraries/` de ton répertoire Arduino

Ou utiliser le gestionnaire de bibliothèques Arduino pour installer LVGL (version 8.x).

## Prérequis

- **ESP32 Arduino Core v3.0.2** (obligatoire)
- **PSRAM QSPI** activé
- **PSRAM Speed** : 120MHz
- **Partition** : Au moins 3MB APP

## Notes techniques

### Contrôleur AXS15231B

Le contrôleur AXS15231B n'est **PAS un contrôleur standard** :
- Pas un ILI9341, ST7789, ou autre contrôleur courant
- Nécessite une séquence d'initialisation propriétaire (67 commandes)
- Le tactile est intégré au même circuit I2C que le LCD

### Signal Tear Effect (TE)

Le signal TE sur GPIO 38 est utilisé pour la synchronisation verticale :
- Évite le tearing (déchirure d'image)
- Utilisé par le callback `bsp_display_sync_cb`
- Configure une interruption sur front descendant

### PSRAM

Les buffers LVGL sont alloués en PSRAM (SPIRAM) :
```c
.flags = {
    .buff_dma = false,
    .buff_spiram = true,
}
```

Ceci permet d'avoir de grands buffers sans saturer la RAM interne.

## Licence

Fichiers extraits de la documentation vendeur JC3248W535C.
Copyright (c) 2024 Espressif Systems (Shanghai) CO LTD.

## Source

Ces fichiers proviennent de :
`doc/JC3248W535EN/1-Demo/Demo_Arduino/DEMO_LVGL/`

Date d'extraction : 2024-07-19 (selon timestamps des fichiers vendeur)
