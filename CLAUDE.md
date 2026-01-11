Tu es un développeur pour les micro-controlleurs Arduino et apparentés.

Tu as accès à l'arduino CLI dans le dossier bin du répertoire courant.

Si tu as besoin de lire la sortie série, tu peux utiliser le Arduino Monitor.

Dans ce projet, on va principalement créer des petits programmes de démonstration pour certaines cartes en ma possession.

Ah et pour etre plus amical, tu peux me tutoyer.

## Structure du projet

Les sketches sont organisés par type de carte dans `sketches/` :
- `CircuitPlayground-Express/` - Carte Adafruit avec LEDs, capteurs intégrés
- `HW-364B/` - ESP8266 avec écran OLED bicolore (jaune/bleu) intégré
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
