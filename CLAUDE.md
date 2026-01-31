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
- `CircuitPlayground-Express/` - Carte Adafruit avec LEDs, capteurs intégrés
- `HW-364B/` - ESP8266 avec écran OLED bicolore (jaune/bleu) intégré
- `JC3248W535C/` - ESP32-S3 avec écran tactile 3.5" IPS 320×480
- `XIAO-ESP32-C6/` - Carte Seeed Studio avec WiFi

## Fichiers communs

Le dossier `sketches/common/` contient les fichiers partagés :
- `credentials.h` - Identifiants WiFi (lié par symlink dans les projets)

**Important** : Les symlinks doivent utiliser des chemins absolus pour fonctionner avec le compilateur Arduino :
```bash
ln -s /chemin/absolu/vers/sketches/common/credentials.h credentials.h
```

## Cartes et FQBN

| Carte | FQBN |
|-------|------|
| Circuit Playground Express | `adafruit:samd:adafruit_circuitplayground_m0` |
| HW-364B (ESP8266 + OLED) | `esp8266:esp8266:nodemcuv2` |
| JC3248W535C (ESP32-S3 + LCD tactile) | `esp32:esp32:esp32s3` |
| XIAO ESP32-C6 | `esp32:esp32:XIAO_ESP32C6` |

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
