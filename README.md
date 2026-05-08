# Arduino Projects

Petits programmes de démonstration pour différentes cartes Arduino et compatibles.

## Installation rapide

```bash
./setup.sh
```

Ce script installe automatiquement :
- Arduino CLI (dans `bin/`)
- Les cores des cartes (ESP8266, ESP32, Adafruit SAMD, Arduino AVR)
- Toutes les bibliothèques listées dans `libraries.txt`

## Configuration WiFi

Le fichier `sketches/common/credentials.h` contient les identifiants WiFi et n'est pas versionné.

1. Créer le fichier à partir de l'exemple :

```bash
cp sketches/common/credentials.h.example sketches/common/credentials.h
nano sketches/common/credentials.h
```

2. Pour chaque projet nécessitant le WiFi, créer un lien symbolique **avec chemin absolu** :

```bash
cd sketches/HW-364B/MonProjet
ln -s /chemin/absolu/vers/sketches/common/credentials.h credentials.h
```

## Structure

```
sketches/
├── common/                      # Fichiers partagés (credentials.h)
├── CircuitPlayground-Express/   # Adafruit (LEDs, accel, micro, capteur temp, capacitif)
├── D1-R32/                      # WEMOS D1 R32 (ESP32 format UNO)
├── ESP32-2432S028/              # Cheap Yellow Display (TFT 320×240)
├── ESP32-4848S040/              # Guition (écran tactile 4" 480×480 + 3 relais)
├── HW-364B/                     # ESP8266 + OLED bicolore intégré
├── JC3248W535C/                 # ESP32-S3 + LCD tactile 3.5"
├── NodeMCU/                     # NodeMCU ESP8266
├── UNO-R3/                      # Arduino UNO R3 classique
├── UNO-R4-WiFi/                 # Arduino UNO R4 WiFi (Renesas + ESP32-S3)
└── XIAO-ESP32-C6/               # Seeed Studio
```

## Cartes supportées

| Carte | FQBN |
|-------|------|
| Arduino UNO R3 | `arduino:avr:uno` |
| Arduino UNO R4 WiFi | `arduino:renesas_uno:unor4wifi` |
| Circuit Playground Express | `adafruit:samd:adafruit_circuitplayground_m0` |
| ESP32-2432S028 (Cheap Yellow Display) | `esp32:esp32:esp32` |
| ESP32-4848S040 (Guition 4" 480×480) | PlatformIO `esp32-s3-devkitm-1` |
| HW-364B (ESP8266 + OLED) | `esp8266:esp8266:nodemcuv2` |
| JC3248W535C (ESP32-S3 + LCD tactile) | `esp32:esp32:esp32s3` |
| NodeMCU (ESP8266) | `esp8266:esp8266:nodemcuv2` |
| WEMOS D1 R32 (ESP32 format UNO) | `esp32:esp32:d1_uno32` |
| XIAO ESP32-C6 | `esp32:esp32:XIAO_ESP32C6` |

## Utilisation

Compiler un sketch :

```bash
./bin/arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 sketches/HW-364B/Bitcoin_Ticker
```

Uploader sur la carte :

```bash
./bin/arduino-cli upload --fqbn esp8266:esp8266:nodemcuv2 --port /dev/ttyUSB0 sketches/HW-364B/Bitcoin_Ticker
```

## Ajouter une bibliothèque

1. L'installer : `./bin/arduino-cli lib install "NomLib"`
2. L'ajouter à `libraries.txt`
3. L'ajouter dans le `@dependencies` du sketch concerné
