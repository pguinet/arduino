Tu es un développeur pour les micro-controlleurs Arduino et apparentés.

Tu as accès à l'arduino CLI dans le dossier bin du répertoire courant.

## ⚠️ Port série - Accès direct interdit

**Bug connu** : Claude Code ne ferme pas correctement les file descriptors vers les ports série. Les commandes bash qui accèdent directement au port série **bloquent définitivement la session**.

**Interdit** :
- `cat /dev/ttyUSB0`
- `stty -F /dev/ttyUSB0 ...`
- Tout accès bash direct au port série

**Pour lire la sortie série** : Utilise uniquement l'arduino-cli monitor avec un timeout :
```bash
timeout 30 ./bin/arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

## Git

**Projet personnel** : Il s'agit d'un projet personnel. Ne pas demander de numéro Jira pour les commits git. Utilise des messages de commit descriptifs sans préfixe.

Dans ce projet, on va principalement créer des petits programmes de démonstration pour certaines cartes en ma possession.

Ah et pour etre plus amical, tu peux me tutoyer.

## Conventions de code

### En-tête de sketch

Chaque sketch doit avoir un en-tête standardisé :

```c
/*
 * NomDuSketch - NomDeLaCarte
 *
 * Description courte du sketch.
 *
 * Board: Nom complet de la carte
 * FQBN: le:fqbn:complet
 *
 * @dependencies Lib1, Lib2, Lib3
 */
```

- Le champ `@dependencies` liste les bibliothèques externes requises (séparées par des virgules)
- Si aucune bibliothèque externe n'est requise, utiliser `@dependencies (aucune)`
- Les bibliothèques intégrées aux cores (WiFi, Wire, SPI...) ne sont pas listées

### Gestion des bibliothèques

- Le fichier `libraries.txt` à la racine liste toutes les bibliothèques du projet
- Quand tu ajoutes une nouvelle bibliothèque, mets à jour ce fichier
- Le script `setup.sh` installe automatiquement toutes les bibliothèques

## Structure du projet

Les sketches sont organisés par type de carte dans `sketches/` :
- `CircuitPlayground-Express/` - Carte Adafruit avec LEDs, capteurs, pads capacitifs
- `ESP32-4848S040/` - ESP32-S3 avec écran tactile 4" IPS 480×480 et 3 relais (Guition)
- `ESP32-2432S028/` - ESP32 avec écran tactile 2.8" TFT 320×240 (Cheap Yellow Display)
- `HW-364B/` - ESP8266 avec écran OLED bicolore (jaune/bleu) intégré
- `JC3248W535C/` - ESP32-S3 avec écran tactile 3.5" IPS 320×480
- `NodeMCU/` - NodeMCU ESP8266 (ESP-12E)
- `XIAO-ESP32-C6/` - Carte Seeed Studio avec WiFi

## Fichiers communs

Le dossier `sketches/common/` contient les fichiers partagés :
- `credentials.h` - Identifiants WiFi et MQTT (lié par symlink dans les projets)
- `credentials.h.example` - Template versionné avec placeholders

**Important** : Les symlinks doivent utiliser des chemins absolus pour fonctionner avec le compilateur Arduino :
```bash
ln -s /chemin/absolu/vers/sketches/common/credentials.h credentials.h
```

## Sécurité - Credentials

**INTERDIT** : Ne JAMAIS écrire de mots de passe, clés API ou secrets dans le code ou dans les fichiers versionnés.

- Les credentials vont dans `credentials.h` (ignoré par git)
- Mettre à jour `credentials.h.example` avec des placeholders si on ajoute de nouveaux paramètres
- Vérifier que tout nouveau fichier sensible est dans `.gitignore`

## Cartes et FQBN

| Carte | FQBN |
|-------|------|
| Circuit Playground Express | `adafruit:samd:adafruit_circuitplayground_m0` |
| ESP32-4848S040 (Guition 4" 480×480) | PlatformIO `esp32-s3-devkitm-1` |
| ESP32-2432S028 (Cheap Yellow Display) | `esp32:esp32:esp32` |
| HW-364B (ESP8266 + OLED) | `esp8266:esp8266:nodemcuv2` |
| NodeMCU (ESP8266) | `esp8266:esp8266:nodemcuv2` |
| JC3248W535C (ESP32-S3 + LCD tactile) | `esp32:esp32:esp32s3` |
| XIAO ESP32-C6 | `esp32:esp32:XIAO_ESP32C6` |

## Circuit Playground Express

Carte Adafruit avec 10 NeoPixels, accéléromètre, micro, capteur de température, speaker et pads capacitifs.

**Pads capacitifs** : A1 à A7 uniquement (A0 = speaker, pas capacitif). Seuil au repos ~200-250, utiliser un calibrage automatique.

**Mapping pads → LEDs** (USB en haut) :
| Pad | LEDs |
|-----|------|
| A7 | 3, 4 |
| A1 | 6, 5 |
| A3 | 8, 9 |
| A4 | 0, 1 |

**Compilation** :
```bash
./bin/arduino-cli compile --fqbn adafruit:samd:adafruit_circuitplayground_m0 sketches/CircuitPlayground-Express/<projet>/<projet>.ino
./bin/arduino-cli upload --fqbn adafruit:samd:adafruit_circuitplayground_m0 --port /dev/ttyACM0 sketches/CircuitPlayground-Express/<projet>/<projet>.ino
```

**Sketches disponibles** :
- `AccelDemo/` - Démo de l'accéléromètre
- `LedDemo/` - Animation arc-en-ciel sur les NeoPixels
- `MicroDemo/` - VU-mètre basique avec le microphone
- `TempDemo/` - Démo du capteur de température
- `Sound_VUMeter/` - VU-mètre avancé avec 3 modes d'affichage et peak hold
- `Simon_Game/` - Jeu de mémoire Simon avec pads capacitifs et sons
- `Touch_Test/` - Test des pads capacitifs (valeurs brutes sur série)

## NodeMCU

Carte NodeMCU 1.0 (ESP-12E Module) basée sur ESP8266.

**MCU** : ESP8266EX (Xtensa LX106 80/160MHz, WiFi, Flash 4MB)

**Pins utiles** :
- A0 : entrée analogique (0-1V, diviseur interne pour 0-3.3V)
- D1 (GPIO5), D2 (GPIO4) : I2C par défaut
- D5-D8 : SPI

**Compilation** :
```bash
./bin/arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 sketches/NodeMCU/<projet>/<projet>.ino
./bin/arduino-cli upload --fqbn esp8266:esp8266:nodemcuv2 --port /dev/ttyUSB0 sketches/NodeMCU/<projet>/<projet>.ino
```

**Sketches disponibles** :
- `Servo_Potentiometer/` - Contrôle d'un servo-moteur avec un potentiomètre

## HW-364B

Carte ESP8266 avec écran OLED 0.96" intégré (SSD1306).

**Écran bicolore** : 16 pixels jaunes en haut, reste en bleu.

**Pins OLED (I2C software)** :
- SDA → GPIO14 (D5)
- SCL → GPIO12 (D6)
- Adresse : 0x3C

**Sketches disponibles** :
- `OLED_Demo` - Animation basique, uptime
- `WiFi_Scanner` - Scan des réseaux WiFi avec signal
- `NTP_Clock` - Horloge NTP + configuration web (timezone, format)
- `Mini_Dashboard` - Dashboard interactif web ↔ OLED (sliders, toggles, messages)
- `Bitcoin_Ticker` - Cours du Bitcoin en USD via API CoinGecko (maj toutes les 60s)
- `Zigbee_Monitor` - Affichage des capteurs Zigbee via MQTT (Zigbee2MQTT)

## JC3248W535C

Carte ESP32-S3 avec écran tactile capacitif 3.5" IPS (320×480). Vendeur : Guition / DIYmalls.

**Documentation** :
- [Manuel PDF officiel](https://device.report/m/83db5d9ce65e7142ed6ae1aebe9697ac3d0c9b10640264803888a4397eddb7b1.pdf)
- [Guide de setup F1ATB](https://f1atb.fr/esp32-s3-3-5-inch-capacitive-touch-ips-display-setup/)
- [Repo NorthernMan54/JC3248W535EN](https://github.com/NorthernMan54/JC3248W535EN) - Base technique utilisée

**MCU** : ESP32-S3-WROOM-1 (dual-core Xtensa LX7 240MHz, WiFi, Bluetooth 5, PSRAM 8MB)

**Écran LCD** (contrôleur AXS15231B, interface QSPI) :
| Signal | GPIO |
|--------|------|
| CS | 45 |
| SCK | 47 |
| SDA0 | 21 |
| SDA1 | 48 |
| SDA2 | 40 |
| SDA3 | 39 |
| TE | 38 |
| Backlight | 1 |

**Tactile** (I2C) :
| Signal | GPIO |
|--------|------|
| SDA | 4 |
| SCL | 8 |
| INT | 3 |

**Audio** (I2S via NS4168) :
| Signal | GPIO |
|--------|------|
| BCK | 42 |
| LRCK | 2 |
| DOUT | 41 |

**Carte SD** (MMC) :
| Signal | GPIO |
|--------|------|
| D0 | 13 |
| CLK | 12 |
| CMD | 11 |

**Batterie ADC** : GPIO5

**Framework** : PlatformIO avec pioarduino (Arduino sur ESP-IDF 5.1).

**Compilation/Upload** :
```bash
cd sketches/JC3248W535C/<projet>
pio run                              # Compiler
pio run -t upload                    # Uploader
pio device monitor                   # Monitor série
```

**Bibliothèques** :
- LVGL 8.4 pour l'interface graphique
- Drivers intégrés : esp_lcd_axs15231b, esp_bsp, esp_lcd_touch

**Sketches disponibles** :
- `TouchTest/` - Test du tactile avec affichage des coordonnées en temps réel
- `System_Monitor/` - Dashboard système avec jauges RAM, PSRAM, uptime
- `WiFi_Scanner/` - Scanner WiFi avec liste tactile et signal coloré
- `Bus_Tracker/` - Suivi des bus via API PRIM Île-de-France Mobilités
- `SD_Browser/` - Explorateur de carte SD avec infos techniques

## ESP32-4848S040

Carte ESP32-S3 avec écran tactile capacitif 4" IPS (480×480) et 3 relais. Fabricant : Guition (JCZN/DIYmalls).

**MCU** : ESP32-S3-WROOM-1U-N16R8 (dual-core Xtensa LX7 240MHz, WiFi, BLE 5, Flash 16MB, PSRAM 8MB OPI)

**USB** : CH340 (pas USB CDC natif) → port `/dev/ttyUSB0`

**Écran LCD** (contrôleur ST7701S, interface RGB 16-bit parallèle + SPI pour init) :
| Signal | GPIO |
|--------|------|
| DE | 18 |
| VSYNC | 17 |
| HSYNC | 16 |
| PCLK | 21 |
| R0-R4 | 11, 12, 13, 14, 0 |
| G0-G5 | 8, 20, 3, 46, 9, 10 |
| B0-B4 | 4, 5, 6, 7, 15 |
| Backlight | 38 |
| SPI CS (init) | 39 |
| SPI SCK (init) | 48 |
| SPI MOSI (init) | 47 |

**Tactile** (GT911, I2C) :
| Signal | GPIO |
|--------|------|
| SDA | 19 |
| SCL | 45 |
| INT | -1 (non connecté) |
| Adresse | 0x5D |

**Relais** (3, partagés avec audio I2S — choisir l'un ou l'autre) :
| Relais | GPIO |
|--------|------|
| Relay 1 | 40 |
| Relay 2 | 2 |
| Relay 3 | 1 |

**Carte SD** (SPI, partagé avec display SPI init) :
| Signal | GPIO |
|--------|------|
| CS | 42 |
| MISO | 41 |
| MOSI | 47 |
| SCK | 48 |

**Framework** : PlatformIO avec pioarduino (Arduino sur ESP-IDF 5.1).

**Bibliothèques** : Arduino_GFX (ST7701S), LVGL 8.4, TAMC_GT911

**Compilation/Upload** :
```bash
cd sketches/ESP32-4848S040/<projet>
pio run                              # Compiler
pio run -t upload                    # Uploader
pio device monitor                   # Monitor série
```

**Sketches disponibles** :
- `System_Dashboard/` - Dashboard système avec jauges RAM/PSRAM, WiFi, contrôle 3 relais tactile
- `HA_Wall_Panel/` - Panneau mural Home Assistant : lumières + 3 relais via MQTT Discovery
- `Bus_Tracker/` - Suivi des bus via API PRIM Île-de-France Mobilités

## ESP32-2432S028

Carte ESP32 avec écran tactile TFT 2.8" (320×240), aussi connue sous le nom "Cheap Yellow Display" (CYD).

**Documentation** :
- [Random Nerd Tutorials - CYD Pinout](https://randomnerdtutorials.com/esp32-cheap-yellow-display-cyd-pinout-esp32-2432s028r/)
- [GitHub witnessmenow/ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display)

**MCU** : ESP32-WROOM-32 (dual-core Xtensa 240MHz, WiFi, Bluetooth 4.2, Flash 4MB)

**Écran LCD** (contrôleur ILI9341, SPI HSPI) :
| Signal | GPIO |
|--------|------|
| MOSI | 13 |
| MISO | 12 |
| SCLK | 14 |
| CS | 15 |
| DC | 2 |
| Backlight | 21 |

**Tactile** (contrôleur XPT2046, SPI VSPI) :
| Signal | GPIO |
|--------|------|
| CLK | 25 |
| MOSI | 32 |
| MISO | 39 |
| CS | 33 |
| IRQ | 36 |

**Carte SD** (SPI) :
| Signal | GPIO |
|--------|------|
| SCK | 18 |
| MOSI | 23 |
| MISO | 19 |
| CS | 5 |

**LED RGB** (active LOW) : R=GPIO4, G=GPIO16, B=GPIO17

**Autres** :
- LDR (capteur lumière) : GPIO34
- Speaker : GPIO26
- Bouton BOOT : GPIO0

**Connecteurs extension** :
- P3 : GPIO35 (input only), GPIO22, GPIO21, GND
- CN1 : GPIO22, GPIO27, 3V3, GND

**Bibliothèques recommandées** :
- TFT_eSPI (écran ILI9341)
- XPT2046_Touchscreen (tactile)

**Configuration TFT_eSPI** : Le fichier `User_Setup.h` doit être copié dans la bibliothèque :
```bash
cp sketches/ESP32-2432S028/User_Setup.h ~/Arduino/libraries/TFT_eSPI/User_Setup.h
```

**Compilation** :
```bash
./bin/arduino-cli compile --fqbn esp32:esp32:esp32 sketches/ESP32-2432S028/<projet>/<projet>.ino
./bin/arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyUSB0 sketches/ESP32-2432S028/<projet>/<projet>.ino
```

**Sketches disponibles** :
- `Display_Test/` - Test de l'écran TFT avec formes, couleurs et texte
- `Touch_Test/` - Test du tactile avec dessin et palette de couleurs
- `RGB_LED_Test/` - Test de la LED RGB avec défilement et effet arc-en-ciel
- `Crypto_Tracker/` - Cours des cryptos avec graphique 7j, navigation tactile, rotation auto
