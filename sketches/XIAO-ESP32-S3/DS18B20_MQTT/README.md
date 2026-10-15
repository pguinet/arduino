# DS18B20_MQTT — XIAO ESP32-S3

Température d'une sonde DS18B20 publiée dans Home Assistant via MQTT
Discovery : le capteur apparaît tout seul, sans configuration côté Home
Assistant. Alimentation USB en continu ; sonde fixée sur un radiateur.

## Matériel

- Seeed Studio XIAO ESP32S3 (version de base), **avec son antenne**
- Sonde DS18B20 étanche (3 fils), soudée sur la carte

| Fil de la sonde | XIAO ESP32S3 |
|---|---|
| Rouge (VDD) | 3V3 |
| Noir (GND) | GND |
| Jaune (DATA) | D0 (GPIO1) |

**Antenne obligatoire.** La XIAO ESP32S3 n'a pas d'antenne intégrée : sans
l'antenne souple clipsée sur le connecteur U.FL, elle ne voit pas le réseau
(`WiFi toujours absent (statut 1)`, soit `WL_NO_SSID_AVAIL`).

**Pas de résistance de tirage.** Le bus 1-Wire utilise le pull-up interne de
l'ESP32-S3 (~45 kΩ), activé avant `sensors.begin()`. Il suffit pour le câble
d'environ 1 m d'une sonde étanche, mais pas pour plusieurs mètres : en cas de
lectures à -127 °C, souder une 4,7 kΩ entre DATA et 3V3, sans rien changer au
code.

### Boîtier

`case/` contient un boîtier imprimable en deux pièces (fond et couvercle,
STL), non modifiées. Le modèle est prévu pour les XIAO C3, C6 et S3 ; prévoir
le passage du câble de la sonde et la place de l'antenne.

[ESP32 Case - C6, C3, S3 including different lids and bases](https://www.printables.com/model/1644207-esp32-case-c6-c3-s3-including-different-lids-and-b)
par **Ofilant**, sous licence
[Creative Commons Attribution (CC BY)](https://creativecommons.org/licenses/by/4.0/).

## Configuration

Identifiants dans `src/credentials.h`, lien symbolique vers
`sketches/common/credentials.h` (modèle : `credentials.h.example`) :
`WIFI_SSID`, `WIFI_PASSWORD`, `MQTT_SERVER`, `MQTT_PORT`, `MQTT_USER`,
`MQTT_PASS`. Rien de spécifique à ce projet.

## Compiler, flasher, observer

```bash
cd sketches/XIAO-ESP32-S3/DS18B20_MQTT
PIO=~/.platformio/penv/bin/pio
$PIO run -e xiao                    # compiler
$PIO run -e xiao -t upload          # flasher (/dev/ttyACM0)
```

Toutes les cartes ESP32-S3 ont le même identifiant USB (`303a:1001`). Pour
vérifier que c'est bien la XIAO qui est branchée (8 MB de flash) :

```bash
~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip auto -p /dev/ttyACM0 flash_id
```

Moniteur série depuis Claude Code (USB natif : lancer dans la même commande
que l'upload, cf. `CLAUDE.md`) :

```bash
$PIO run -e xiao -t upload && sleep 47 | timeout 45 ../../../bin/arduino-cli \
  monitor -p /dev/ttyACM0 -c baudrate=115200 --quiet --timestamp
```

Démarrage normal : `Sondes detectees : 1`, `Identifiant : xiao_ds18b20_…`,
`WiFi connecte a …`, `MQTT connecte`, `Decouverte publiee (… octets) : OK`,
puis `Lecture : …` toutes les 10 s et `Publie : …`.

## Fonctionnement

- **Identifiant** : `xiao_ds18b20_` + les 3 derniers octets de la MAC (lue
  dans l'eFuse). Chaque XIAO devient un appareil distinct.
- **Mesure** : 12 bits, toutes les 10 s, conversion non bloquante (750 ms).
- **Publication** : arrondie à 0,1 °C, envoyée si l'écart avec la dernière
  publication atteint 0,2 °C, ou toutes les 5 min. Republiée à chaque
  reconnexion MQTT et au redémarrage de Home Assistant.
- **Lectures rejetées** : -127 °C (sonde muette, CRC faux), hors de
  -55…125 °C, et 85 °C (valeur de mise sous tension) sauf si la lecture
  précédente dépassait 75 °C.
- **Sonde indisponible** après 3 lectures invalides d'affilée, disponible à
  nouveau dès la première lecture valide.
- **WiFi** : reconnexion par le core ; après 30 s sans réseau,
  `WiFi.reconnect()` couvre les cas que le core abandonne (`AUTH_FAIL` d'une
  box qui redémarre).
- **MQTT** : nouvel essai 5 s après la fin du précédent ; une tentative bloque
  au plus ~8 s (3 s TCP + 5 s de réponse), plus la résolution DNS quand
  `MQTT_SERVER` est un nom (`.local` : mDNS).

| Sujet | Contenu | Retenu |
|---|---|---|
| `homeassistant/sensor/<id>/temperature/config` | découverte | oui |
| `xiao_ds18b20/<id>/temperature` | valeur, ex. `21.4` | non |
| `xiao_ds18b20/<id>/status` | `online` / `offline` (testament MQTT) | oui |
| `xiao_ds18b20/<id>/probe` | `online` / `offline` selon la sonde | oui |

Le capteur n'est disponible dans Home Assistant que si `status` **et**
`probe` valent `online` (`availability_mode: all`).

## Tests et qualité

```bash
$PIO test -e native                 # tests Unity de lib/temp_logic (24)
$PIO check -e xiao --skip-packages  # cppcheck
```

## Dépannage

| Symptôme | Cause probable |
|---|---|
| `WiFi toujours absent (statut 1)` | antenne non branchée, ou réseau hors de portée |
| `Lecture : -127.0000 C (rejetee)` | DATA mal soudé, ou pull-up insuffisant (ajouter 4,7 kΩ) |
| `Sondes detectees : 0` | sonde non alimentée ou DATA pas sur D0 |
| Appareil fantôme dans Home Assistant | sujets retenus d'un ancien identifiant : publier un message vide retenu sur ses 3 sujets retenus |

## Fichiers

| Chemin | Rôle |
|---|---|
| `src/main.cpp` | sonde, WiFi, MQTT (code dépendant du matériel) |
| `lib/temp_logic/temp_logic.*` | validation, arrondi, décision de publier, état de la sonde |
| `lib/temp_logic/ha_discovery.*` | identifiant, sujets, JSON de découverte |
| `test/` | tests natifs Unity |
| `case/` | boîtier imprimable (STL) |
