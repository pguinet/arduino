# JC3248W535C - ESP32-S3 avec écran tactile 3.5"

Carte ESP32-S3 avec écran tactile capacitif IPS 3.5" (320×480) et contrôleur AXS15231B.

## Caractéristiques

### MCU
- **Processeur** : ESP32-S3-WROOM-1
- **Architecture** : Dual-core Xtensa LX7 @ 240MHz
- **Mémoire** :
  - Flash : 16MB
  - PSRAM : 8MB QSPI
- **Connectivité** : WiFi 802.11 b/g/n, Bluetooth 5.0 LE

### Écran LCD
- **Taille** : 3.5 pouces
- **Résolution** : 320 × 480 pixels
- **Type** : IPS TFT
- **Interface** : QSPI (4 lignes de données)
- **Contrôleur** : AXS15231B (custom)
- **Format couleur** : RGB565 (16 bits/pixel, big endian)
- **Backlight** : PWM (GPIO 1)

### Tactile
- **Type** : Capacitif
- **Interface** : I2C (intégré au contrôleur AXS15231B)
- **Points simultanés** : 1 (configuré)

### Audio
- **Codec** : NS4168 (amplificateur I2S)
- **Sortie** : Haut-parleur intégré
- **Interface** : I2S

### Carte SD
- **Interface** : MMC (1-bit)
- **Format supporté** : FAT32

### Batterie
- **Mesure** : ADC sur GPIO 5
- **Gestion** : Circuit de charge intégré

## Pinout détaillé

### LCD (QSPI)
| Signal | GPIO | Description |
|--------|------|-------------|
| CS | 45 | Chip Select |
| SCK | 47 | Serial Clock |
| SDA0 | 21 | Data line 0 |
| SDA1 | 48 | Data line 1 |
| SDA2 | 40 | Data line 2 |
| SDA3 | 39 | Data line 3 |
| TE | 38 | Tear Effect (V-sync) |
| Backlight | 1 | PWM backlight control |

### Tactile (I2C)
| Signal | GPIO |
|--------|------|
| SDA | 4 |
| SCL | 8 |
| INT | 3 |

### Audio (I2S)
| Signal | GPIO |
|--------|------|
| BCK | 42 |
| LRCK | 2 |
| DOUT | 41 |

### Carte SD (MMC)
| Signal | GPIO |
|--------|------|
| D0 | 13 |
| CLK | 12 |
| CMD | 11 |

### Batterie
| Signal | GPIO |
|--------|------|
| ADC | 5 |

## Configuration Arduino IDE

**IMPORTANT** : Utiliser ESP32 Arduino Core **v3.0.2** obligatoirement.

### Réglages recommandés
```
Board: "ESP32S3 Dev Module"
CPU Frequency: "240MHz (WiFi)"
Flash Size: "16MB (128Mb)"
Partition Scheme: "16M Flash (3MB APP/9.9MB FATFS)"
PSRAM: "QSPI PSRAM"
PSRAM Speed: "120MHz"
Upload Speed: "921600"
USB CDC On Boot: "Enabled"
```

## Structure du projet

```
JC3248W535C/
├── README.md                              # Ce fichier
├── GUIDE_INITIALISATION_AFFICHEUR.md      # Guide technique complet
├── INSTALLATION_LVGL.md                   # Guide d'installation LVGL
├── lib/                                   # Drivers et bibliothèques
│   ├── README.md                          # Documentation des drivers
│   ├── esp_bsp.c / .h                     # Board Support Package
│   ├── esp_lcd_axs15231b.c / .h           # Driver LCD custom
│   ├── esp_lcd_touch.c / .h               # Driver tactile
│   ├── lv_port.c / .h                     # Port LVGL pour ESP32
│   ├── display.h                          # Définitions LCD
│   ├── lv_conf.h                          # Configuration LVGL
│   └── bsp_err_check.h                    # Macros d'erreur
├── HelloWorld/                            # Premier sketch de test
│   ├── HelloWorld.ino
│   └── README.md
└── doc/                                   # Documentation vendeur (non versionnée)
    └── JC3248W535EN/
        └── 1-Demo/
            └── Demo_Arduino/
                ├── DEMO_LVGL/
                ├── DEMO_PIC/
                ├── DEMO_MJPEG/
                └── DEMO_MP3/
```

## Bibliothèques requises

### Essentielles

#### 1. LVGL (à installer)
**Bibliothèque graphique** pour l'interface utilisateur.

📥 **Installation** : Voir [INSTALLATION_LVGL.md](./INSTALLATION_LVGL.md)

Options d'installation :
- Copier depuis la doc vendeur (recommandé)
- Gestionnaire de bibliothèques Arduino
- Installation manuelle depuis GitHub

#### 2. Drivers custom (déjà dans lib/)
Tous les drivers nécessaires sont déjà extraits dans le dossier `lib/` :
- **esp_lcd_axs15231b** : Driver custom pour le contrôleur LCD
- **esp_lcd_touch_axs15231b** : Driver tactile
- **esp_bsp** : Board Support Package
- **lv_port** : Port LVGL pour ESP32

📖 **Documentation** : Voir [lib/README.md](./lib/README.md)

### Optionnelles
- **ESP32_JPEG_Library** : Affichage d'images JPEG
- **ESP32-audioI2S** : Lecture audio
- **SD_MMC** : Accès carte SD (incluse dans ESP32 Core)

## Documentation

- **[GUIDE_INITIALISATION_AFFICHEUR.md](./GUIDE_INITIALISATION_AFFICHEUR.md)** : Guide complet pour initialiser l'afficheur avec tous les détails techniques
- **doc/JC3248W535EN/** : Documentation complète du vendeur (non versionnée, trop volumineuse)
  - Exemples de code Arduino
  - Datasheets
  - Outils de développement

## Sketches disponibles

### HelloWorld
Premier sketch de test pour vérifier que l'afficheur fonctionne.

**Ce qu'il fait** :
- Initialise l'afficheur LCD et LVGL
- Affiche "Hello World!" en vert au centre de l'écran
- Teste le backlight et les drivers

**Utilisation** : Voir [HelloWorld/README.md](./HelloWorld/README.md)

## Exemples du vendeur

Les exemples du vendeur se trouvent dans `doc/JC3248W535EN/1-Demo/Demo_Arduino/` :

1. **DEMO_LVGL** : Interface graphique LVGL (widgets, stress test)
2. **DEMO_PIC** : Diaporama d'images JPEG depuis carte SD
3. **DEMO_MJPEG** : Lecture vidéo MJPEG depuis carte SD
4. **DEMO_MP3** : Lecteur audio MP3 depuis carte SD

## Problèmes connus

### Afficheur ne s'allume pas
- Vérifier que le backlight est activé : `bsp_display_backlight_on()`
- Vérifier la version ESP32 Core (doit être v3.0.2)
- Vérifier que PSRAM est activé en mode QSPI

### Compilation échoue
- S'assurer que tous les fichiers du driver custom sont copiés
- Vérifier `lv_conf.h` dans le dossier LVGL
- Augmenter la partition APP si erreur de mémoire

### Tactile ne répond pas
- L'initialisation I2C doit être faite avant le tactile
- Vérifier que le driver custom `esp_lcd_touch_axs15231b` est utilisé

## Ressources

### Contrôleur LCD AXS15231B
- Contrôleur **non standard** (pas ILI9341/ST7789/etc.)
- Nécessite un driver custom fourni par le vendeur
- Séquence d'initialisation propriétaire (67 commandes)

### Particularités
- Le tactile est intégré au contrôleur LCD (même circuit I2C)
- Le signal TE (Tear Effect) permet d'éviter le tearing
- La PSRAM à 120MHz est recommandée pour la vidéo

## Notes de développement

- Toujours appeler `bsp_display_lock()` avant de modifier l'interface LVGL
- Toujours appeler `bsp_display_unlock()` après les modifications
- LVGL tourne dans une tâche séparée (configurée par `lvgl_port_init`)
- Le buffer LVGL doit être en PSRAM pour de bonnes performances

## Liens utiles

- [ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [LVGL Documentation](https://docs.lvgl.io/)
- [ESP-IDF LCD Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/lcd.html)
