# Arduino Projects

Petits programmes de démonstration pour différentes cartes Arduino et compatibles.

## Installation

### Arduino CLI

Télécharger et installer l'Arduino CLI dans le dossier `bin/` :

```bash
mkdir -p bin
cd bin
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
cd ..
```

Ou télécharger manuellement depuis : https://arduino.github.io/arduino-cli/installation/

### Cores des cartes

Installer les cores nécessaires :

```bash
# Ajouter l'URL ESP8266
./bin/arduino-cli config set board_manager.additional_urls \
  https://arduino.esp8266.com/stable/package_esp8266com_index.json

# Mettre à jour l'index
./bin/arduino-cli core update-index

# Installer les cores
./bin/arduino-cli core install arduino:avr
./bin/arduino-cli core install esp8266:esp8266
./bin/arduino-cli core install esp32:esp32
./bin/arduino-cli core install adafruit:samd
```

### Bibliothèques

```bash
./bin/arduino-cli lib install U8g2
./bin/arduino-cli lib install ArduinoJson
```

### Credentials WiFi

Le fichier `sketches/common/credentials.h` contient les identifiants WiFi et n'est pas versionné.

1. Créer le fichier à partir de l'exemple :

```bash
cp sketches/common/credentials.h.example sketches/common/credentials.h
```

2. Éditer avec tes identifiants :

```c
#define WIFI_SSID "ton_ssid"
#define WIFI_PASSWORD "ton_mot_de_passe"
```

3. Pour chaque projet nécessitant le WiFi, créer un lien symbolique **avec chemin absolu** :

```bash
cd sketches/HW-364B/MonProjet
ln -s /chemin/absolu/vers/sketches/common/credentials.h credentials.h
```

## Structure

```
sketches/
├── common/                      # Fichiers partagés
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
