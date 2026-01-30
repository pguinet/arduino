# Fichiers extraits de la documentation vendeur

Ce document liste tous les fichiers de code extraits de la documentation vendeur JC3248W535C et maintenant versionnés dans le dépôt.

## Source originale

**Documentation vendeur** : `doc/JC3248W535EN/1-Demo/Demo_Arduino/DEMO_LVGL/`

**Date d'extraction** : 31 janvier 2025

**Timestamp fichiers vendeur** : 19 juillet 2024

## Fichiers extraits dans lib/

Tous ces fichiers ont été copiés depuis la doc vendeur vers le dossier `lib/` pour être versionnés.

### Drivers LCD

| Fichier | Taille | Description |
|---------|--------|-------------|
| `esp_lcd_axs15231b.c` | 23671 octets | Driver complet pour contrôleur LCD AXS15231B |
| `esp_lcd_axs15231b.h` | 8742 octets | Header du driver LCD |

**Fonctionnalités** :
- Séquence d'initialisation propriétaire (67 commandes)
- Support QSPI 4 lignes
- Format RGB565 big endian
- Fonctions : reset, init, draw_bitmap, invert_color, mirror, swap_xy

### Drivers Tactile

| Fichier | Taille | Description |
|---------|--------|-------------|
| `esp_lcd_touch.c` | 7197 octets | Driver tactile I2C AXS15231B |
| `esp_lcd_touch.h` | 11722 octets | Header du driver tactile |

**Fonctionnalités** :
- Communication I2C avec contrôleur tactile
- Support 1 point de contact
- Callbacks interruptions
- Transformation coordonnées selon rotation

### Board Support Package

| Fichier | Taille | Description |
|---------|--------|-------------|
| `esp_bsp.c` | 20445 octets | Fonctions d'initialisation de haut niveau |
| `esp_bsp.h` | 3557 octets | Header BSP |

**Fonctionnalités** :
- Initialisation complète LCD + tactile
- Gestion backlight PWM
- Configuration I2C
- Intégration LVGL
- Gestion Tear Effect (TE)

### Port LVGL

| Fichier | Taille | Description |
|---------|--------|-------------|
| `lv_port.c` | 21790 octets | Adaptation LVGL pour ESP32 |
| `lv_port.h` | 4630 octets | Header port LVGL |

**Fonctionnalités** :
- Tâche LVGL dédiée
- Timer LVGL (5ms)
- Buffers en PSRAM
- Support tactile
- Thread-safe (mutex)

### Configuration

| Fichier | Taille | Description |
|---------|--------|-------------|
| `lv_conf.h` | 26023 octets | Configuration complète LVGL |
| `display.h` | 4429 octets | Définitions et constantes LCD |
| `bsp_err_check.h` | 1625 octets | Macros de gestion d'erreur |

**Paramètres lv_conf.h** :
- `LV_COLOR_DEPTH` : 16 (RGB565)
- `LV_COLOR_16_SWAP` : 1 (big endian)
- Démos activées : widgets, stress

## Fichiers créés (non issus de la doc vendeur)

### Documentation

| Fichier | Description |
|---------|-------------|
| `README.md` | Documentation principale de la carte |
| `QUICKSTART.md` | Guide de démarrage rapide (5 min) |
| `GUIDE_INITIALISATION_AFFICHEUR.md` | Guide technique complet |
| `INSTALLATION_LVGL.md` | Guide installation bibliothèque LVGL |
| `FICHIERS_EXTRAITS.md` | Ce fichier |
| `lib/README.md` | Documentation des drivers dans lib/ |

### Sketches

| Fichier | Description |
|---------|-------------|
| `HelloWorld/HelloWorld.ino` | Premier sketch de test |
| `HelloWorld/README.md` | Documentation du sketch HelloWorld |

## Modifications apportées

Aucune modification n'a été apportée aux fichiers extraits. Ce sont des copies exactes des fichiers vendeur.

Les seuls ajouts sont :
- Documentation (fichiers .md)
- Sketch d'exemple HelloWorld.ino

## Licences

### Fichiers vendeur (lib/*.c, lib/*.h)

```
SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
SPDX-License-Identifier: Apache-2.0 ou CC0-1.0
```

Fichiers fournis par Espressif Systems dans la documentation vendeur.

### Fichiers créés

Documentation et sketches créés pour faciliter l'utilisation de la carte.

## Raison de l'extraction

La documentation vendeur complète est trop volumineuse pour être versionnée (~plusieurs centaines de MB avec les outils, exemples compilés, etc.).

Seuls les fichiers de code **nécessaires et suffisants** pour faire fonctionner l'afficheur ont été extraits et versionnés, permettant :
- ✅ De ne pas dépendre de la doc vendeur volumineuse
- ✅ De versionner le code essentiel
- ✅ De faciliter la collaboration
- ✅ De simplifier le déploiement

## Documentation vendeur complète

La documentation complète du vendeur (si disponible) se trouve dans :
```
doc/JC3248W535EN/
├── 1-Demo/
│   └── Demo_Arduino/
│       ├── DEMO_LVGL/          ← Source des fichiers extraits
│       ├── DEMO_PIC/           ← Exemple affichage images
│       ├── DEMO_MJPEG/         ← Exemple vidéo MJPEG
│       ├── DEMO_MP3/           ← Exemple audio MP3
│       └── libraries/
│           └── lvgl/           ← Bibliothèque LVGL complète
├── 4-Driver_IC_Data_Sheet/     ← Datasheets
├── 7-Character&Picture_Molding_Tool/  ← Outils
└── 8-Burn operation/           ← Outils de flash
```

**Note** : Le dossier `doc/` n'est **pas versionné** (ajouté au .gitignore).

## Vérification des fichiers

Pour vérifier que tous les fichiers nécessaires sont présents :

```bash
cd sketches/JC3248W535C/lib/
ls -1
```

**Attendu** :
```
bsp_err_check.h
display.h
esp_bsp.c
esp_bsp.h
esp_lcd_axs15231b.c
esp_lcd_axs15231b.h
esp_lcd_touch.c
esp_lcd_touch.h
lv_conf.h
lv_port.c
lv_port.h
README.md
```

Total : **12 fichiers** (11 fichiers de code + 1 README)

## Utilisation

Tous ces fichiers sont automatiquement inclus lors de la compilation d'un sketch qui référence :

```cpp
#include "../lib/display.h"
#include "../lib/esp_bsp.h"
#include "../lib/lv_port.h"
```

Arduino IDE compile automatiquement tous les fichiers .c du projet.

## Mise à jour

Si une nouvelle version de la documentation vendeur est disponible :

1. Comparer les fichiers avec ceux dans `lib/`
2. Identifier les changements
3. Tester les nouvelles versions
4. Mettre à jour si nécessaire
5. Documenter les changements

## Contact vendeur

Pour obtenir la documentation complète ou des mises à jour :
- Contacter le vendeur de la carte JC3248W535C
- Vérifier s'il existe des mises à jour firmware/drivers

## Versions

| Date | Version | Changements |
|------|---------|-------------|
| 2024-07-19 | Initiale | Fichiers originaux du vendeur |
| 2025-01-31 | v1.0 | Extraction et versioning des fichiers essentiels |
