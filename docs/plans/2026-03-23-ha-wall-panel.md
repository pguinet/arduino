# HA Wall Panel — Plan d'implémentation

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:executing-plans to implement this plan task-by-task.

**Goal:** Panneau mural tactile Home Assistant via MQTT : contrôle de lumières HA + 3 relais locaux exposés comme switches HA via MQTT Discovery.

**Architecture:** Même base hardware que System_Dashboard (Arduino_GFX + LVGL 8.4 + TAMC_GT911). Communication MQTT via PubSubClient. Au boot, publie des configs MQTT Discovery pour les 3 relais. Souscrit aux états des lumières HA configurées. UI avec grille de boutons lumières + rangée relais.

**Tech Stack:** PlatformIO, Arduino_GFX v1.5.0, LVGL 8.4, TAMC_GT911, PubSubClient, WiFi ESP32

**Pièges connus (de System_Dashboard) :**
- `pclk_active_neg` = 1 (sinon décalage horizontal)
- Touch GT911 inversé : `(SCREEN_W-1) - x`, `(SCREEN_H-1) - y`
- Arduino_GFX épinglé à v1.5.0 (incompatibilité API 1.5.6+)
- `LV_EVENT_SHORT_CLICKED` au lieu de `LV_EVENT_CLICKED` (release flaky sans pin INT)
- USB CH340 : `ARDUINO_USB_CDC_ON_BOOT=0`, port `/dev/ttyUSB0`

---

### Task 0: Scaffolding — copier la base depuis System_Dashboard

**Files:**
- Create: `sketches/ESP32-4848S040/HA_Wall_Panel/platformio.ini`
- Create: `sketches/ESP32-4848S040/HA_Wall_Panel/.gitignore`
- Copy: `sketches/ESP32-4848S040/System_Dashboard/include/lv_conf.h` → `sketches/ESP32-4848S040/HA_Wall_Panel/include/lv_conf.h`
- Create: `sketches/ESP32-4848S040/HA_Wall_Panel/src/main.cpp` (squelette)
- Create: symlink `sketches/ESP32-4848S040/HA_Wall_Panel/src/credentials.h` → `sketches/common/credentials.h`

**Step 1: Créer `platformio.ini`**

Identique au System_Dashboard mais avec PubSubClient en plus :

```ini
; HA_Wall_Panel - ESP32-4848S040
; Panneau mural Home Assistant : lumières + relais via MQTT

[env:esp32s3]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/51.03.07/platform-espressif32.zip
board = esp32-s3-devkitm-1
framework = arduino
upload_port = /dev/ttyUSB0
monitor_port = /dev/ttyUSB0
monitor_speed = 115200

board_build.mcu = esp32s3
board_build.f_cpu = 240000000L
board_build.arduino.memory_type = qio_opi
board_build.flash_mode = qio
board_build.psram_type = opi
board_upload.flash_size = 16MB

build_flags =
    -DARDUINO_USB_MODE=0
    -DARDUINO_USB_CDC_ON_BOOT=0
    -DBOARD_HAS_PSRAM
    -DLV_CONF_INCLUDE_SIMPLE
    -I${PROJECT_DIR}/include

lib_deps =
    https://github.com/moononournation/Arduino_GFX.git#v1.5.0
    lvgl/lvgl@^8.4.0
    tamctec/TAMC_GT911@^1.0.2
    knolleary/PubSubClient@^2.8
```

**Step 2: Créer `.gitignore`**

```
.pio/
```

**Step 3: Copier `lv_conf.h`**

```bash
mkdir -p sketches/ESP32-4848S040/HA_Wall_Panel/include
cp sketches/ESP32-4848S040/System_Dashboard/include/lv_conf.h \
   sketches/ESP32-4848S040/HA_Wall_Panel/include/lv_conf.h
```

**Step 4: Créer symlink credentials.h**

```bash
ln -s /home/pascal/github/arduino/sketches/common/credentials.h \
      sketches/ESP32-4848S040/HA_Wall_Panel/src/credentials.h
```

**Step 5: Créer `src/main.cpp` squelette**

Reprendre les sections display + touch + LVGL init du System_Dashboard (lignes 1-48 pour display, 50-57 pour touch, 66-126 pour LVGL callbacks, setup display/touch/LVGL). Remplacer tout le code UI et métier. Le squelette doit compiler avec juste un écran noir et "HA Wall Panel" en titre.

```cpp
/*
 * HA_Wall_Panel - ESP32-4848S040
 *
 * Panneau mural Home Assistant : controle de lumieres HA
 * et 3 relais locaux via MQTT avec auto-discovery.
 *
 * Board: ESP32-4848S040C_I_Y_3 (Guition 4" 480x480 IPS)
 * FQBN: PlatformIO esp32-s3-devkitm-1
 *
 * @dependencies Arduino_GFX, LVGL 8.4, TAMC_GT911, PubSubClient
 */
```

**Step 6: Compiler**

```bash
cd sketches/ESP32-4848S040/HA_Wall_Panel && /home/pascal/.platformio/penv/bin/pio run
```

**Step 7: Commit**

```bash
git add sketches/ESP32-4848S040/HA_Wall_Panel/
git commit -m "ESP32-4848S040: scaffolding HA_Wall_Panel"
```

---

### Task 1: MQTT — connexion, reconnexion, LWT

**Files:**
- Modify: `src/main.cpp`

**Step 1: Ajouter PubSubClient + WiFi**

```cpp
#include <WiFi.h>
#include <PubSubClient.h>
#include "credentials.h"

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

#define MQTT_TOPIC_AVAILABILITY "wall_panel/availability"
#define MQTT_CLIENT_ID "wall_panel"
```

**Step 2: Fonction `mqtt_connect()`**

```cpp
void mqtt_connect() {
    if (mqtt.connected()) return;

    mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    mqtt.setBufferSize(512);  // Pour les payloads discovery JSON

    if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS,
                     MQTT_TOPIC_AVAILABILITY, 0, true, "offline")) {
        mqtt.publish(MQTT_TOPIC_AVAILABILITY, "online", true);
        // Les souscriptions seront ajoutées dans les tâches suivantes
        Serial.println("MQTT connected");
    }
}
```

**Step 3: WiFi + MQTT dans setup() et loop()**

- `WiFi.begin()` dans setup (non-bloquant)
- Dans loop : vérifier WiFi, appeler `mqtt_connect()` si WiFi OK, appeler `mqtt.loop()`
- Reconnexion WiFi toutes les 10s, MQTT toutes les 5s

**Step 4: Compiler et tester**

```bash
/home/pascal/.platformio/penv/bin/pio run
```

**Step 5: Commit**

```bash
git commit -am "HA_Wall_Panel: connexion MQTT avec LWT"
```

---

### Task 2: MQTT Discovery — exposer les 3 relais comme switches HA

**Files:**
- Modify: `src/main.cpp`

**Step 1: Définir les topics relais**

```cpp
#define RELAY_COUNT 3
const int relay_pins[RELAY_COUNT] = {40, 2, 1};
bool relay_state[RELAY_COUNT] = {false, false, false};

// Topics MQTT par relais
// Command: wall_panel/relay_X/set  (payload: "ON" / "OFF")
// State:   wall_panel/relay_X/state (payload: "ON" / "OFF")
```

**Step 2: Fonction `publish_discovery()`**

Appelée après chaque connexion MQTT. Publie un JSON de config pour chaque relais sur `homeassistant/switch/wall_panel/relay_X/config` :

```cpp
void publish_discovery() {
    for (int i = 0; i < RELAY_COUNT; i++) {
        char topic[80];
        snprintf(topic, sizeof(topic), "homeassistant/switch/wall_panel/relay_%d/config", i + 1);

        char payload[400];
        snprintf(payload, sizeof(payload),
            "{"
            "\"name\":\"Relais %d\","
            "\"unique_id\":\"wall_panel_relay_%d\","
            "\"command_topic\":\"wall_panel/relay_%d/set\","
            "\"state_topic\":\"wall_panel/relay_%d/state\","
            "\"availability_topic\":\"%s\","
            "\"payload_on\":\"ON\","
            "\"payload_off\":\"OFF\","
            "\"device\":{"
              "\"identifiers\":[\"wall_panel\"],"
              "\"name\":\"Wall Panel\","
              "\"model\":\"ESP32-4848S040\","
              "\"manufacturer\":\"Guition\""
            "}"
            "}",
            i + 1, i + 1, i + 1, i + 1, MQTT_TOPIC_AVAILABILITY);

        mqtt.publish(topic, payload, true);
    }
}
```

**Step 3: Souscrire aux commandes relais**

Dans `mqtt_connect()` après connexion réussie :
```cpp
mqtt.subscribe("wall_panel/relay_+/set");
```

**Step 4: Callback MQTT pour commandes relais**

```cpp
void mqtt_callback(char *topic, byte *payload, unsigned int length) {
    String msg;
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

    // Parser: wall_panel/relay_X/set
    for (int i = 0; i < RELAY_COUNT; i++) {
        char expected[40];
        snprintf(expected, sizeof(expected), "wall_panel/relay_%d/set", i + 1);
        if (strcmp(topic, expected) == 0) {
            relay_state[i] = (msg == "ON");
            digitalWrite(relay_pins[i], relay_state[i] ? HIGH : LOW);
            // Publier le nouvel état
            char state_topic[40];
            snprintf(state_topic, sizeof(state_topic), "wall_panel/relay_%d/state", i + 1);
            mqtt.publish(state_topic, relay_state[i] ? "ON" : "OFF", true);
            // Mettre à jour le bouton UI (sera implémenté Task 4)
            break;
        }
    }
}
```

Enregistrer le callback : `mqtt.setCallback(mqtt_callback);` avant `mqtt.connect()`.

**Step 5: Publier les états initiaux après discovery**

```cpp
void publish_relay_states() {
    for (int i = 0; i < RELAY_COUNT; i++) {
        char topic[40];
        snprintf(topic, sizeof(topic), "wall_panel/relay_%d/state", i + 1);
        mqtt.publish(topic, relay_state[i] ? "ON" : "OFF", true);
    }
}
```

Appeler `publish_discovery()` puis `publish_relay_states()` après chaque connexion MQTT.

**Step 6: Compiler et tester**

```bash
/home/pascal/.platformio/penv/bin/pio run
```

Upload et vérifier que les switches apparaissent dans HA (Settings → Devices → "Wall Panel").

**Step 7: Commit**

```bash
git commit -am "HA_Wall_Panel: MQTT Discovery pour 3 relais"
```

---

### Task 3: Souscription aux lumières HA

**Files:**
- Modify: `src/main.cpp`

**Step 1: Définir la config lumières**

```cpp
#define MAX_LIGHTS 6

struct LightEntity {
    const char *entity_id;   // ex: "salon"
    const char *name;        // ex: "Salon"
    bool state;              // ON/OFF
};

// À configurer selon ton installation HA
LightEntity lights[] = {
    {"salon", "Salon", false},
    {"cuisine", "Cuisine", false},
    {"chambre", "Chambre", false},
    {"bureau", "Bureau", false},
};
const int light_count = sizeof(lights) / sizeof(lights[0]);
```

Note : `entity_id` est le suffixe après `light.`. Le topic MQTT HA pour une lumière est typiquement `homeassistant/light/<entity_id>/state` ou via Zigbee2MQTT `zigbee2mqtt/<friendly_name>`.

**Step 2: Souscrire aux états des lumières**

Dans `mqtt_connect()` après connexion :

```cpp
for (int i = 0; i < light_count; i++) {
    char topic[80];
    snprintf(topic, sizeof(topic), "zigbee2mqtt/%s", lights[i].entity_id);
    mqtt.subscribe(topic);
}
```

**Step 3: Parser les messages d'état lumières dans le callback**

Les messages Zigbee2MQTT sont en JSON : `{"state":"ON","brightness":254,...}`

On parse juste le champ `state` avec une recherche simple (pas besoin d'ArduinoJson pour ça) :

```cpp
// Dans mqtt_callback, après le parsing relais :
for (int i = 0; i < light_count; i++) {
    char expected[80];
    snprintf(expected, sizeof(expected), "zigbee2mqtt/%s", lights[i].entity_id);
    if (strcmp(topic, expected) == 0) {
        lights[i].state = (strstr((char*)payload, "\"state\":\"ON\"") != NULL);
        // Mettre à jour le bouton UI (Task 4)
        break;
    }
}
```

**Step 4: Envoyer une commande toggle lumière**

```cpp
void toggle_light(int idx) {
    if (idx < 0 || idx >= light_count) return;

    char topic[80];
    snprintf(topic, sizeof(topic), "zigbee2mqtt/%s/set", lights[idx].entity_id);
    mqtt.publish(topic, lights[idx].state ? "{\"state\":\"OFF\"}" : "{\"state\":\"ON\"}");
}
```

**Step 5: Compiler**

```bash
/home/pascal/.platformio/penv/bin/pio run
```

**Step 6: Commit**

```bash
git commit -am "HA_Wall_Panel: souscription et contrôle lumières via MQTT"
```

---

### Task 4: UI LVGL — boutons lumières + relais

**Files:**
- Modify: `src/main.cpp`

**Step 1: Créer le layout**

Écran 480×480 :
```
┌──────────────────────────────────────┐
│          HA Wall Panel          [●]  │  y=0-45
├──────────┬──────────┬────────────────┤
│ 💡Salon  │ 💡Cuisine│               │  y=50-230  (grille 2 colonnes)
│   [ON]   │   [OFF]  │               │
├──────────┼──────────┤               │
│ 💡Chambre│ 💡Bureau │               │  y=230-390
│   [OFF]  │   [ON]   │               │
├──────────┴──────────┴────────────────┤
│  [Relais 1]  [Relais 2]  [Relais 3] │  y=400-475
└──────────────────────────────────────┘
```

**Step 2: Boutons lumières**

Grille 2×3 (ou 2×2 selon le nombre), chaque bouton ~220×160 :

```cpp
static lv_obj_t *btn_light[MAX_LIGHTS];
static lv_obj_t *lbl_light[MAX_LIGHTS];

void create_light_buttons(lv_obj_t *parent) {
    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_set_size(grid, 460, 340);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 5, 0);
    lv_obj_set_style_pad_row(grid, 10, 0);
    lv_obj_set_style_pad_column(grid, 10, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 50);

    for (int i = 0; i < light_count; i++) {
        btn_light[i] = lv_btn_create(grid);
        lv_obj_set_size(btn_light[i], 218, 155);
        lv_obj_set_style_radius(btn_light[i], 15, 0);
        lv_obj_set_style_shadow_width(btn_light[i], 0, 0);
        lv_obj_set_style_border_width(btn_light[i], 0, 0);
        // OFF state par défaut
        lv_obj_set_style_bg_color(btn_light[i], lv_color_hex(0x333333), 0);

        lbl_light[i] = lv_label_create(btn_light[i]);
        lv_label_set_text_fmt(lbl_light[i], LV_SYMBOL_POWER "\n%s\nOFF", lights[i].name);
        lv_obj_set_style_text_align(lbl_light[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(lbl_light[i], lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(lbl_light[i], &lv_font_montserrat_20, 0);
        lv_obj_center(lbl_light[i]);

        lv_obj_add_event_cb(btn_light[i], light_btn_cb, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)i);
    }
}
```

**Step 3: Callbacks boutons lumières**

```cpp
static void light_btn_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    toggle_light(idx);
}
```

**Step 4: Fonction `update_light_btn(int idx)`**

Met à jour l'apparence du bouton selon `lights[idx].state` :

```cpp
void update_light_btn(int idx) {
    if (idx < 0 || idx >= light_count || !btn_light[idx]) return;

    if (lights[idx].state) {
        lv_obj_set_style_bg_color(btn_light[idx], lv_color_hex(0xfca311), 0);  // Jaune chaud
        lv_obj_set_style_text_color(lbl_light[idx], lv_color_hex(0x1a1a2e), 0);
        lv_label_set_text_fmt(lbl_light[idx], LV_SYMBOL_POWER "\n%s\nON", lights[idx].name);
    } else {
        lv_obj_set_style_bg_color(btn_light[idx], lv_color_hex(0x333333), 0);
        lv_obj_set_style_text_color(lbl_light[idx], lv_color_hex(0x888888), 0);
        lv_label_set_text_fmt(lbl_light[idx], LV_SYMBOL_POWER "\n%s\nOFF", lights[idx].name);
    }
}
```

Appeler `update_light_btn(i)` depuis le callback MQTT quand l'état d'une lumière change.

**Step 5: Boutons relais (même pattern que System_Dashboard)**

Rangée de 3 boutons en bas (y=400), 145×70 chacun. Même callback que System_Dashboard mais qui publie aussi l'état MQTT :

```cpp
static void relay_btn_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= RELAY_COUNT) return;

    relay_state[idx] = !relay_state[idx];
    digitalWrite(relay_pins[idx], relay_state[idx] ? HIGH : LOW);

    // Publier sur MQTT
    char topic[40];
    snprintf(topic, sizeof(topic), "wall_panel/relay_%d/state", idx + 1);
    mqtt.publish(topic, relay_state[idx] ? "ON" : "OFF", true);

    // Update bouton UI
    update_relay_btn(idx);
}
```

**Step 6: Indicateur connexion MQTT**

Un petit cercle en haut à droite : vert si MQTT connecté, rouge sinon. Mis à jour dans loop().

```cpp
static lv_obj_t *led_mqtt;

// Dans ui_init:
led_mqtt = lv_led_create(lv_scr_act());
lv_obj_set_size(led_mqtt, 16, 16);
lv_obj_align(led_mqtt, LV_ALIGN_TOP_RIGHT, -15, 15);
lv_led_set_color(led_mqtt, lv_color_hex(0xff4444));
lv_led_on(led_mqtt);
```

**Step 7: Compiler**

```bash
/home/pascal/.platformio/penv/bin/pio run
```

**Step 8: Commit**

```bash
git commit -am "HA_Wall_Panel: UI complète avec boutons lumières et relais"
```

---

### Task 5: Intégration finale + CLAUDE.md

**Files:**
- Modify: `src/main.cpp` — polish, test complet
- Modify: `CLAUDE.md` — ajouter HA_Wall_Panel aux sketches disponibles

**Step 1: Test complet**

- Compiler (`pio run`)
- Uploader (`pio run -t upload`)
- Vérifier :
  - Écran affiche le dashboard avec boutons
  - LED MQTT passe au vert après connexion
  - Les 3 relais apparaissent dans HA (Settings → Devices → Wall Panel)
  - Toggle relais depuis HA → relais clique + bouton se met à jour
  - Toggle relais depuis l'écran → état se met à jour dans HA
  - Toggle lumière depuis l'écran → lumière s'allume/éteint
  - Toggle lumière depuis HA → bouton se met à jour sur l'écran

**Step 2: Mettre à jour CLAUDE.md**

Dans la section ESP32-4848S040, sketches disponibles, ajouter :
```
- `HA_Wall_Panel/` - Panneau mural Home Assistant : lumières + 3 relais via MQTT Discovery
```

**Step 3: Commit final**

```bash
git add sketches/ESP32-4848S040/HA_Wall_Panel/ CLAUDE.md
git commit -m "ESP32-4848S040: ajouter HA_Wall_Panel (panneau mural Home Assistant)"
```
