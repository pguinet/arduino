# API PRIM en TLS 1.3 — libs arduino-esp32 recompilées

## Contexte

Depuis le **26/05/2026**, l'API PRIM Île-de-France Mobilités
(`prim.iledefrance-mobilites.fr`) n'accepte plus que **TLS 1.3**
(annonce du 07/05/2026 sur le Slack communautaire PRIM). Or les
bibliothèques **précompilées** d'arduino-esp32 (core 3.0.7, pioarduino
51.03.07, ESP-IDF 5.1) embarquent une mbedTLS **sans TLS 1.3**
(`# CONFIG_MBEDTLS_SSL_PROTO_TLS1_3 is not set`). Résultat : tous les
sketches qui interrogent PRIM échouaient (handshake `protocol_version`,
`HTTP -1`, aucun passage affiché).

## Solution retenue

Recompiler les **libs précompilées** d'arduino-esp32 pour l'esp32s3 en
activant TLS 1.3. Le code des sketches reste **100 % standard**
(`#include <WiFiClientSecure.h>`, `client.setInsecure()`,
`https.begin(client, url)`) : `ssl_client.cpp` natif gère déjà le SNI
(`mbedtls_ssl_set_hostname`) et ne plafonne pas la version → mbedTLS
négocie TLS 1.3 dès qu'il est compilé.

> Une tentative via wolfSSL (ESP32-EasyWolfSSL) a été abandonnée : lib
> trop jeune (TLS 1.2 forcé, pas de SNI, lecture chunked octet par octet,
> 2ᵉ handshake qui pend sous LVGL). mbedTLS natif règle tout proprement.

## Comment reconstruire les libs (Docker)

Outil : [`esp32-arduino-lib-builder`](https://github.com/espressif/esp32-arduino-lib-builder),
branche `release/v5.1` (= IDF 5.1 / arduino 3.0.7).

**Deux modifs dans le repo lib-builder avant build :**

1. `configs/defconfig.common` — activer TLS 1.3 et désactiver le dynamic
   buffer (en IDF 5.1, TLS 1.3 `depends on !MBEDTLS_DYNAMIC_BUFFER`).
   Remplacer les 3 lignes `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y` /
   `..._DYNAMIC_FREE_PEER_CERT=y` / `..._DYNAMIC_FREE_CONFIG_DATA=y` par :
   ```
   # CONFIG_MBEDTLS_DYNAMIC_BUFFER is not set
   CONFIG_MBEDTLS_SSL_PROTO_TLS1_3=y
   ```
   (`MBEDTLS_HKDF_C` est auto-sélectionné par TLS1_3.)

2. `tools/update-components.sh` — **épingler tinyusb à 0.18.0**. Le script
   d'origine clone `master` (non figé), qui a depuis supprimé
   `usbd_control.c` → erreur `No SOURCES given to target __idf_arduino_tinyusb`.
   0.18.0 est la version qui contient à la fois `usbd_control.c` et
   `dwc2_common.c` (les 15 fichiers référencés par le CMakeLists 3.0.7).
   ```bash
   TINYUSB_REPO_REF="0.18.0"
   git clone --branch "$TINYUSB_REPO_REF" --depth 1 "$TINYUSB_REPO_URL" "$TINYUSB_REPO_DIR"
   ```

**Build (image Docker officielle) :**
```bash
docker run --rm --entrypoint /bin/bash \
  -v $PWD:/opt/esp/lib-builder -w /opt/esp/lib-builder \
  espressif/esp32-arduino-lib-builder:release-v5.1 \
  -lc 'git config --global --add safe.directory "*"; ./build.sh -t esp32s3'
```
Sortie : `out/tools/esp32-arduino-libs/esp32s3/` (vérifier
`CONFIG_MBEDTLS_SSL_PROTO_TLS1_3=y` dans son `sdkconfig`).

## Installation dans pioarduino

Remplacer le dossier esp32s3 des libs précompilées (sauvegarder l'original) :
```bash
INST=~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3
cp -r "$INST" "$INST.orig"                 # backup TLS 1.2
rm -rf "$INST"
cp -r .../out/tools/esp32-arduino-libs/esp32s3 "$INST"
```

⚠️ **Un `pio pkg update` peut réécraser ces libs** → refaire l'install,
ou figer la version du package. La sauvegarde TLS 1.2 est dans `esp32s3.orig`.

Validé en runtime le 30/05/2026 sur JC3248W535C (Transit_Tracker,
bus + train) : HTTP 200, TLS 1.3, sans watchdog.
