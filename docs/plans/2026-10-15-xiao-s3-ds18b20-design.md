# DS18B20_MQTT - XIAO ESP32-S3 : conception

Capteur de température DS18B20 (sonde étanche) sur XIAO ESP32S3, publié dans
Home Assistant via MQTT Discovery. Alimentation USB en continu.

## Matériel

| Fil de la sonde | XIAO ESP32S3 |
|---|---|
| Rouge (VDD) | 3V3 |
| Noir (GND) | GND |
| Jaune (DATA) | D0 (GPIO1) |

- D0 : broche sans rôle au démarrage. D2 (GPIO3) est une broche de
  strapping, D6/D7 sont l'UART.
- Pas de résistance de tirage externe : `INPUT_PULLUP` (~45 kΩ) sur GPIO1
  avant d'ouvrir le bus. OneWire ne fait que basculer la broche entre entrée
  et sortie, le pull-up reste actif. Une 4,7 kΩ entre DATA et 3V3 corrige
  un bus instable, sans changer le code.

## Structure

`sketches/XIAO-ESP32-S3/DS18B20_MQTT/`, PlatformIO, plateforme pioarduino
(identique à `Claude_Dashboard`), carte `seeed_xiao_esp32s3`.

```
DS18B20_MQTT/
├── platformio.ini      env xiao + env native (tests) + cppcheck
├── src/main.cpp        WiFi, MQTT, lecture de la sonde
├── src/credentials.h   symlink vers sketches/common/credentials.h
├── lib/temp_logic/     logique pure, sans Arduino
├── test/               tests Unity natifs
└── README.md
```

Bibliothèques (`lib_deps`) : OneWire, DallasTemperature, PubSubClient,
ArduinoJson. Identifiants existants de `credentials.h` (`WIFI_SSID`,
`WIFI_PASSWORD`, `MQTT_SERVER`, `MQTT_PORT`, `MQTT_USER`,
`MQTT_PASS`) : rien de nouveau.

## Fonctionnement

- Identifiant : `xiao_ds18b20_<6 derniers chiffres hexa de la MAC>`.
- Mesure : 12 bits, toutes les 10 s, conversion non bloquante
  (`setWaitForConversion(false)`, lecture une fois les 750 ms écoulées).
- Publication : valeur arrondie à 0,1 °C, publiée si l'écart avec la
  dernière publication est d'au moins 0,2 °C ou si 5 min se sont écoulées.

| Sujet | Contenu | Retenu |
|---|---|---|
| `homeassistant/sensor/<id>/temperature/config` | découverte | oui |
| `xiao_ds18b20/<id>/temperature` | valeur, ex. `21.4` | non |
| `xiao_ds18b20/<id>/status` | `online`/`offline`, testament MQTT | oui |
| `xiao_ds18b20/<id>/probe` | `online`/`offline` selon la sonde | oui |

- Découverte : `availability` sur `status` et `probe`,
  `availability_mode: all`. Republiée à chaque connexion MQTT et à chaque
  `online` sur `homeassistant/status`.

## Erreurs

- Lecture rejetée : -127 °C (sonde absente), 85 °C sans valeur précédente
  proche (valeur de mise sous tension), hors de -55…125 °C.
- 3 lectures invalides d'affilée : `probe` passe `offline` ; première
  lecture valide : `online`.
- WiFi : un seul réseau (`WIFI_SSID`), `WiFi.begin()` au démarrage puis
  reconnexion automatique du core ; après 30 s sans WiFi, `WiFi.reconnect()`
  (non bloquant) couvre les raisons que le core abandonne (`AUTH_FAIL`…).
- MQTT : nouvel essai 5 s après la fin du précédent ; une tentative bloque
  jusqu'à ~8 s (3 s TCP + 5 s de réponse, plus la résolution DNS si
  `MQTT_SERVER` est un nom).
- Sonde soudée et fixée au radiateur : pas de branchement à chaud géré. Le
  passage en indisponible couvre les erreurs CRC (pull-up interne faible) et
  une soudure défaillante.

## Tests

Tests natifs Unity (`pio test -e native`) sur `temp_logic` :
- validation (-127, 85 avec et sans valeur précédente, bornes) ;
- arrondi, valeurs négatives comprises ;
- décision de publier (premier envoi, seuil 0,2 °C, 5 min, retour à zéro
  de `millis()`) ;
- état de la sonde (3 échecs, rétablissement) ;
- sujets et JSON de découverte (relu avec ArduinoJson).

Qualité : `pio check` (cppcheck) sans avertissement.

Sur la carte : moniteur série, `mosquitto_sub` sur `xiao_ds18b20/#` et
`homeassistant/sensor/xiao_ds18b20_+/#`, apparition dans Home Assistant,
fil DATA débranché → capteur indisponible.
