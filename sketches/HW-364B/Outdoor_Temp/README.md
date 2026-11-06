# Outdoor_Temp — HW-364B

Afficheur de la température extérieure : un HW-364B (ESP8266 + OLED 0,96"
jaune/bleu) recyclé. La mesure vient de la sonde Zigbee du jardin (Sonoff
SNZB-02LD), relayée par Zigbee2MQTT ; aucune automatisation Home Assistant
n'est nécessaire.

## Écran

```
┌────────────────────────────┐
│ Exterieur   21:31  ▂▄▆█    │  bande jaune : titre, heure NTP, WiFi
├────────────────────────────┤
│         7.6 °C             │  bande bleue : température en gros
│      il y a 3 min          │  âge de la mesure, ou alerte
└────────────────────────────┘
```

Ligne du bas, par ordre de priorité :

| Texte | Signification |
|---|---|
| `WiFi : connexion...` / `MQTT : connexion...` | réseau ou broker indisponible (reconnexion auto, MQTT toutes les 5 s) |
| `En attente de la sonde` | aucune valeur reçue |
| `Sonde hors ligne` (inversé) | Zigbee2MQTT signale la sonde hors ligne (topic `availability`) |
| `Perimee (il y a 1 h 12)` (inversé) | aucun message depuis plus d'une heure |
| `Derniere valeur connue` | valeur retenue par le broker, reçue à la connexion : son âge est inconnu |
| `il y a 3 min` | valeur reçue en direct |

Le chiffre se décale de ±2 pixels toutes les 10 minutes pour limiter le
marquage de l'OLED, allumé en permanence.

## MQTT

| Topic | Contenu |
|---|---|
| `zigbee2mqtt/temperature_exterieure` | JSON, champ `temperature` (°C, au dixième). **Retenu** sur le broker |
| `zigbee2mqtt/temperature_exterieure/availability` | `{"state":"online"\|"offline"}`, retenu |

Le message de la sonde fait environ 450 octets (bloc `update` de Zigbee2MQTT) :
le tampon de PubSubClient est porté à 1024 octets, sinon il le jetterait sans
erreur. ArduinoJson ne garde que `temperature` grâce à un filtre.

PubSubClient n'expose pas le drapeau *retain* : le premier message reçu dans
les 3 s suivant l'abonnement est considéré comme la valeur retenue, d'âge
inconnu. Si la valeur est la même qu'avant une reconnexion, l'âge déjà connu
est conservé.

## Configuration

Dans `sketches/common/credentials.h` (lié par symlink dans `src/`) :

```c
#define AFFICHEUR_MQTT_SERVER "192.168.2.241"   // IP plutôt que zigbee.local (mDNS peu fiable)
#define AFFICHEUR_MQTT_USER "afficheur"         // compte Mosquitto dédié
#define AFFICHEUR_MQTT_PASS "..."
```

Sans ces trois définitions, le sketch se rabat sur `MQTT_SERVER`, `MQTT_USER`
et `MQTT_PASS`. `WIFI_SSID`, `WIFI_PASSWORD` et `MQTT_PORT` viennent du même
fichier.

```bash
ln -s /home/pascal/github/arduino/sketches/common/credentials.h src/credentials.h
```

## Compilation, tests, téléversement

```bash
cd sketches/HW-364B/Outdoor_Temp
~/.platformio/penv/bin/pio test -e native         # 22 tests de la logique (lib/outdoor_logic)
~/.platformio/penv/bin/pio run -e hw364b -t upload
```

Moniteur série (115200 bauds) depuis la racine du dépôt :

```bash
sleep 35 | timeout 30 ./bin/arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200 --quiet
```
