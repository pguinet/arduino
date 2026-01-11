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
├── CircuitPlayground-Express/   # Carte Adafruit
├── HW-364B/                     # ESP8266 + OLED
└── XIAO-ESP32-C6/               # Carte Seeed Studio
```

## Cartes supportées

| Carte | FQBN |
|-------|------|
| Circuit Playground Express | `adafruit:samd:adafruit_circuitplayground_m0` |
| HW-364B (ESP8266 + OLED) | `esp8266:esp8266:nodemcuv2` |
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
