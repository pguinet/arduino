# Internet Monitor CYD — Design

**Date** : 2026-05-10
**Cible** : ESP32-2432S028 (Cheap Yellow Display) — TFT 320×240 + tactile + speaker GPIO26
**Sketch** : `sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino`

## Goal

Surveiller l'état de la connexion Internet en continu avec diagnostic en cascade pour identifier la couche défaillante (WiFi local / box / Internet / DNS), afficher l'état + statistiques sur l'écran, et alerter par bip sonore répétitif tant que la connexion est coupée. Boutons tactiles pour faire taire l'alarme temporairement.

## Architecture

Single sketch `.ino` compilé avec arduino-cli (cohérent avec les autres sketches CYD).

- **Carte** : ESP32-2432S028, FQBN `esp32:esp32:esp32`
- **Bibliothèques** :
  - `TFT_eSPI` (déjà configurée via `User_Setup.h`)
  - `XPT2046_Touchscreen` (déjà installée)
  - `ESP32Ping` (à ajouter à `libraries.txt`)
  - WiFi, time.h (built-in)

### Flux principal

1. **Boot** : connexion WiFi via `credentials.h`, sync NTP (timezone Europe/Paris), affichage initial "CHECKING…"
2. **Boucle de check** toutes les 5 s : cascade WiFi → ping passerelle → ping 8.8.8.8 → DNS resolve
3. **Rendu écran** : redessin partiel (champs qui changent) chaque cycle + horloge mise à jour chaque seconde
4. **Audio** : pattern bip répété toutes les 10 s tant que `state != OK` et silence inactif
5. **Touch** : poll à chaque cycle de boucle ; hit-test sur les zones boutons silence

## Cascade de checks

À chaque tick (5 s), dans l'ordre, sortie au premier échec :

```
1. WiFi.status() != WL_CONNECTED  → WIFI_DOWN, WiFi.reconnect()
2. ping(gateway, 1000ms)          → LAN_DOWN
3. ping(8.8.8.8, 1000ms)          → INTERNET_DOWN
4. WiFi.hostByName("cloudflare.com", 1000ms) → DNS_DOWN
5. tous OK                        → OK + push latence Internet dans ring buffer
```

### Hystérésis

- **2 échecs consécutifs** sur une couche → `DOWN`
- **1 succès** suffit pour repasser `OK` (asymétrique)
- Compteurs séparés par couche : `wifi_fail`, `lan_fail`, `inet_fail`, `dns_fail`
- Reset à 0 dès qu'une couche succède

### State machine

```
enum State { CHECKING, OK, WIFI_DOWN, LAN_DOWN, INTERNET_DOWN, DNS_DOWN };
```

`CHECKING` : état initial avant le premier cycle complet (pas d'alarme).

### Worst-case timing

4 × 1 s = 4 s par cycle. Tient dans le tick de 5 s. Si le tick déborde, on saute simplement le suivant.

## Tracking outage

- **À la transition OK → DOWN** : `outage_start_epoch = now()`
- **À la transition DOWN → OK** :
  - `last_outage_duration_s = now() - outage_start_epoch`
  - `last_outage_start_epoch = outage_start_epoch` (pour affichage "14:18")
  - `total_downtime_ms += last_outage_duration_s × 1000`
- **Uptime %** :
  ```
  current_outage_ms = (state != OK) ? (millis() - outage_start_local_ms) : 0
  uptime_pct = (total_uptime_ms - total_downtime_ms - current_outage_ms) / total_uptime_ms × 100
  ```

## Stats / historique

### Ring buffer latence

- 120 entrées (10 min à 5 s)
- Push à chaque cycle complet sans échec : valeur = ms ping Internet
- Marqueur d'échec : push `-1` (affiché barre rouge plein-cadre)
- Affichage : graphe de barres dans le panneau bas

## UI Layout (320×240)

```
┌────────────────────────────────────────────┐
│ Internet Monitor   14:23:05      [✓ OK]   │  header (24px)
├────────────────────────────────────────────┤
│ WiFi  [●] MyHome      -52dBm    3 ms      │
│ Box   [●] 192.168.1.1            2 ms     │  cascade (96px)
│ Net   [●] 8.8.8.8               18 ms     │
│ DNS   [●] cloudflare.com        24 ms     │
├────────────────────────────────────────────┤
│ Uptime 99.4%   Dern. coupure 14:18 (1m24)│
│ ┌────────────────────────────────────────┐│
│ │     ▂▃▂▂▃▂▂▂▂▂▂▃▂▂▂▂▂▂▂                ││  graphe latence (70px)
│ └────────────────────────────────────────┘│
├────────────────────────────────────────────┤
│ [ 5min ]    [ 30min ]    [ Permanent ]    │  boutons silence (40px)
└────────────────────────────────────────────┘
```

### Couleurs

- **Vert** (`#00B050`) : couche OK
- **Rouge** (`#E04040`) : couche KO
- **Gris** (`#606060`) : inconnu / inactif
- **Fond** : noir
- **Texte** : blanc / coloré selon couche

### Header

`[✓ OK]` vert si `state == OK`, `[!! WIFI]`, `[!! LAN]`, `[!! NET]`, `[!! DNS]` rouge sinon. Horloge en HH:MM:SS via NTP.

### Cascade

4 lignes, chacune avec :
- Étiquette (WiFi / Box / Net / DNS)
- LED `●` colorée selon état couche
- Détails contextuels (SSID/RSSI, IP gateway, IP cible, hostname)
- Latence en ms (ou `-` si KO)

### Footer / silence

État normal : 3 boutons tactiles `5min` / `30min` / `Permanent` (chacun ~100×36 px).

État silence actif : bandeau plein largeur `🔇 Silence 4m32s — tap pour réactiver`. Tap → annule.

## Audio + silence

### Patterns

- **Pattern DOWN** : 3 bips de 100 ms à 800 Hz, espacés de 80 ms (~540 ms total). Joué :
  - Une fois immédiatement à la transition `OK → DOWN`
  - Puis toutes les 10 s tant que `state != OK` ET silence inactif
- **Pattern UP** : 3 notes ascendantes 600 → 800 → 1000 Hz, 100 ms chacune. Joué **une fois** à la transition `DOWN → OK`. **Jamais réprimé par le silence** (bonne nouvelle).

### Silence

| Bouton | Effet |
|---|---|
| `5min` | `silence_until = now + 300s` |
| `30min` | `silence_until = now + 1800s` |
| `Permanent` | `silence_permanent = true` |

**Auto-clear** :
- À la transition `DOWN → OK` : `silence_until = 0`, `silence_permanent = false` (pour que la prochaine coupure réalerte)
- À l'expiration du timer pendant qu'on est encore down : silence levé, bips reprennent

**Bandeau** : durée restante mise à jour chaque seconde, `tap` n'importe où sur le bandeau → annule.

**Debounce** : 500 ms entre deux taps consécutifs.

## Touch

- Driver : `XPT2046_Touchscreen` (HSPI configuré pour CYD)
- Calibration : valeurs déjà éprouvées dans `Touch_Test` (à reprendre)
- Polling : `ts.touched()` à chaque tour de loop
- Hit-test : 3 rectangles boutons normaux + 1 rectangle bandeau silence

## Configuration

Dans `credentials.h` (déjà présent) : `WIFI_SSID`, `WIFI_PASSWORD`. Aucun secret nouveau requis.

Constantes en tête de fichier :
- `CHECK_INTERVAL_MS = 5000`
- `BEEP_INTERVAL_MS = 10000`
- `PING_TIMEOUT_MS = 1000`
- `LATENCY_HISTORY_SIZE = 120`
- `DNS_TARGET = "cloudflare.com"`
- `INTERNET_TARGET = IPAddress(8,8,8,8)`

## Edge cases

- **WiFi pas connecté au boot** : retry toutes les 5 s, écran montre `WIFI_DOWN` permanent jusqu'à connexion
- **NTP pas sync** : afficher `--:--:--` et "boot" pour `dern. coupure` ; les durées en ms (uptime) marchent quand même
- **Reboot pendant outage** : pas de persistance — stats reset, c'est OK pour ce use case
- **Silence "Permanent" + reboot** : reset à inactif (volontaire — au reboot on veut le buzzer fonctionnel)
- **Latence > 999 ms** : afficher `>999`
- **Tick débordant** (réseau très lent) : on rate juste un tick, pas de souci

## Out of scope

- Pas de persistance NVS (stats / silence)
- Pas de notification push (Telegram/MQTT) — pour plus tard si tu veux
- Pas de logs détaillés (seulement le ring buffer en RAM)
- Pas de configuration WiFi via portail captif (utilise `credentials.h`)

## Fichiers à créer / modifier

- `sketches/ESP32-2432S028/Internet_Monitor/Internet_Monitor.ino` (nouveau)
- `sketches/ESP32-2432S028/Internet_Monitor/credentials.h` → symlink vers `sketches/common/credentials.h`
- `libraries.txt` → ajouter `ESP32Ping`
- `CLAUDE.md` → ajouter Internet_Monitor dans la liste des sketches CYD
