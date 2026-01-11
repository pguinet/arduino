#!/bin/bash
#
# Script d'installation pour les projets Arduino
# Installe arduino-cli, les cores et les bibliothèques
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Installation Arduino ==="
echo ""

# 1. Arduino CLI
echo "[1/4] Arduino CLI..."
if [ -f "bin/arduino-cli" ]; then
    echo "      Déjà installé: $(./bin/arduino-cli version | head -1)"
else
    echo "      Téléchargement..."
    mkdir -p bin
    cd bin
    curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
    cd ..
    echo "      Installé: $(./bin/arduino-cli version | head -1)"
fi
echo ""

# 2. Configuration des URLs de boards
echo "[2/4] Configuration des sources..."
./bin/arduino-cli config set board_manager.additional_urls \
    https://arduino.esp8266.com/stable/package_esp8266com_index.json \
    https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json \
    https://adafruit.github.io/arduino-board-index/package_adafruit_index.json
./bin/arduino-cli core update-index
echo ""

# 3. Installation des cores
echo "[3/4] Installation des cores..."
CORES=(
    "arduino:avr"
    "esp8266:esp8266"
    "esp32:esp32"
    "adafruit:samd"
)

for core in "${CORES[@]}"; do
    if ./bin/arduino-cli core list | grep -q "^${core}"; then
        echo "      $core (déjà installé)"
    else
        echo "      $core (installation...)"
        ./bin/arduino-cli core install "$core"
    fi
done
echo ""

# 4. Installation des bibliothèques
echo "[4/4] Installation des bibliothèques..."
if [ -f "libraries.txt" ]; then
    while IFS= read -r line || [ -n "$line" ]; do
        # Ignorer les commentaires et lignes vides
        line=$(echo "$line" | sed 's/#.*//' | xargs)
        if [ -n "$line" ]; then
            if ./bin/arduino-cli lib list | grep -q "^${line}"; then
                echo "      $line (déjà installée)"
            else
                echo "      $line (installation...)"
                ./bin/arduino-cli lib install "$line"
            fi
        fi
    done < "libraries.txt"
else
    echo "      Fichier libraries.txt non trouvé"
fi
echo ""

# 5. Credentials
echo "=== Configuration ==="
if [ ! -f "sketches/common/credentials.h" ]; then
    echo ""
    echo "ATTENTION: Le fichier credentials.h n'existe pas."
    echo "Crée-le avec :"
    echo "  cp sketches/common/credentials.h.example sketches/common/credentials.h"
    echo "  nano sketches/common/credentials.h"
    echo ""
fi

echo ""
echo "=== Installation terminée ==="
echo ""
echo "Pour compiler un sketch :"
echo "  ./bin/arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 sketches/HW-364B/MonSketch"
echo ""
echo "Pour uploader :"
echo "  ./bin/arduino-cli upload --fqbn esp8266:esp8266:nodemcuv2 --port /dev/ttyUSB0 sketches/HW-364B/MonSketch"
echo ""
