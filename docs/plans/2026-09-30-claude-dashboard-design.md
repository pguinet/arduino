# Claude_Dashboard (JC3248W535C) — Design

Date : 2026-09-30

## Objectif

Tableau de bord Claude Code sur le JC3248W535C (ESP32-S3, écran 3.5" tactile) qui
affiche l'état des sessions Claude Code tournant sur un **serveur Linux distant** :

- quotas du forfait (fenêtre 5 h et hebdomadaire) ;
- activité de chaque session (projet, modèle, état, dernier outil, durée dans l'état) ;
- remplissage du contexte par session ;
- alerte visuelle et sonore quand une session attend l'utilisateur.

Hors périmètre (YAGNI) : répondre à une permission depuis l'écran, historique ou
graphiques d'usage, coût en tokens.

## Architecture

```
Serveur Linux distant                                   Cloud            Maison
┌──────────────────────────────────────────┐
│ Claude Code (N sessions)                 │
│  ├─ hooks ──────┐                        │
│  └─ statusline ─┤ écrivent               │
│                 ▼                        │
│  ~/.claude/dashboard/sessions/<id>.json  │
│                 │ inotify                │
│                 ▼                        │     MQTT/TLS       MQTT/TLS
│  claude-dash-agent (systemd --user) ─────┼──► broker ────► JC3248 (LVGL)
└──────────────────────────────────────────┘    8883
```

Le broker MQTT est un broker **cloud dédié**, distinct du broker local de la maison.

### Collecte (serveur)

- **`dash-hook`** : script unique branché sur `SessionStart`, `UserPromptSubmit`,
  `PreToolUse`, `Notification`, `Stop`, `SessionEnd`. Lit le JSON du hook sur stdin,
  met à jour l'état de la session et note le PID du processus `claude` ancêtre
  (détection des sessions mortes).
- **Statusline** : la statusline existante transmet en plus son JSON d'entrée
  (`rate_limits.five_hour/seven_day`, `context_window.used_percentage`, `model`,
  `workspace.current_dir`, `session_id`) au même mécanisme. Son affichage ne change pas.
- Écritures atomiques (fichier temporaire + `rename`), < 50 ms, silencieuses en cas
  d'erreur : le dashboard ne doit jamais gêner Claude Code.

### États d'une session

| État | Déclencheur |
|---|---|
| `working` | `UserPromptSubmit`, `PreToolUse` |
| `permission` | `Notification` de type `permission_prompt` |
| `idle` | `Stop` (tour terminé, attente de consigne) |
| (retirée) | `SessionEnd`, ou PID disparu |

### Agent

Démon Python (`claude-dash-agent`, service systemd `--user`) :

- surveille `~/.claude/dashboard/sessions/` ;
- une seule connexion MQTT/TLS persistante (`paho-mqtt`) ;
- publie un snapshot **retained** sur `claude-dash/<hostname>/state` à chaque
  changement (au plus toutes les 2 s) et au moins toutes les 60 s (heartbeat) ;
- purge les sessions dont le PID n'existe plus ;
- les quotas sont globaux au compte : on garde la valeur la plus récente reçue.

### Format du snapshot

```json
{
  "host": "srv-dev", "ts": 1790000000,
  "limits": { "h5": 42, "h5_reset": 1790003600, "d7": 18, "d7_reset": 1790400000 },
  "sessions": [
    { "id": "a1b2c3", "project": "arduino", "model": "Opus 5.5",
      "state": "permission", "since": 1789999950, "ctx": 37, "tool": "Bash" }
  ]
}
```

Taille < 2,5 Ko pour 12 sessions (texte ramené en ASCII). Le JC3248 s'abonne à `claude-dash/+/state`
(plusieurs serveurs possibles).

## Écran (JC3248)

Paysage 480×320 (rotation 270°, comme Transit_Tracker), LVGL 8, Montserrat intégrée
(libellés sans accents).

```
┌──────────────────────────────────────────────────────────┐
│ Claude Code        srv-dev          ● MQTT   14:32       │
├──────────────────────────────────────────────────────────┤
│ 5h  ████████░░░░░░░░  42%   reset 15:30                  │
│ 7j  ███░░░░░░░░░░░░░  18%   reset jeu 09:00              │
├──────────────────────────────────────────────────────────┤
│ ● arduino        Opus 5.5   PERMISSION  Bash     0:42    │
│    ctx ██████░░░░ 37%                                    │
│ ● api-backend    Sonnet     ATTENTE              5:10    │
│    ctx ███░░░░░░░ 21%                                    │
│ ● infra          Opus 5.5   TRAVAILLE  Edit      0:03    │
│    ctx ████████░░ 81%                                    │
└──────────────────────────────────────────────────────────┘
```

- Barres de quota : vert < 60 %, orange < 85 %, rouge au-delà ; heure de reset locale
  (NTP, Europe/Paris).
- Cartes triées par urgence (permission > idle > working), puis ancienneté de l'état ;
  défilement tactile au-delà de 3 sessions. Couleurs : rouge / orange / bleu.
- **Alerte** à la *transition* vers `permission` (deux notes aiguës) ou `idle` (une
  note grave) via le NS4168 (I2S) + rallumage du rétroéclairage. Jamais en boucle.
- **Veille** : écran éteint après 10 min sans session en attente ni toucher ; un
  toucher le rallume. Rétroéclairage on/off uniquement.
- **États dégradés** : pastille rouge si WiFi/MQTT coupé (reconnexion auto) ; bandeau
  « Serveur injoignable depuis X min » sans heartbeat depuis 3 min, données grisées ;
  « Aucune session active » sinon.
- Plusieurs `host` : le nom du serveur apparaît sur chaque carte.

## Sécurité

- TLS vérifié : CA racine du broker embarquée dans le firmware, pas de `setInsecure()`.
- Deux comptes MQTT avec ACL : serveur = publication seule sur `claude-dash/<host>/#`,
  écran = abonnement seul sur `claude-dash/#`.
- Données publiées minimales : nom du dossier projet (pas le chemin), modèle, état,
  nom d'outil. Jamais de prompts ni de commandes.
- Identifiants : `DASH_MQTT_*` dans `credentials.h` (+ placeholders dans
  `credentials.h.example`) ; côté serveur `~/.config/claude-dash/config.toml` (chmod 600).
- Broker cloud : vérifier le support de TLS 1.2 (ou utiliser les libs TLS 1.3
  recompilées, cf. `sketches/common/PRIM_TLS13_libs.md`).

## Organisation du code

```
sketches/JC3248W535C/Claude_Dashboard/
├── platformio.ini           # env esp32s3 + env native (tests)
├── include/ src/            # firmware (drivers repris de Transit_Tracker)
│   ├── main.cpp             # WiFi, MQTT, LVGL, audio
│   └── dash_model.h/.cpp    # logique pure : parsing JSON, tri, transitions
├── test/                    # tests Unity (pio test -e native)
└── server/
    ├── claude_dash/         # paquet Python : hook, statusline, agent
    ├── tests/               # pytest
    ├── install.sh           # hook + ligne statusline + service systemd --user
    └── README.md
```

Bibliothèques : `PubSubClient` (buffer 4 Ko), `ArduinoJson` côté ESP32 ;
`paho-mqtt` côté serveur. Mettre à jour `libraries.txt` et `CLAUDE.md`.

## Tests et qualité

- **Python** (Docker) : pytest sur la machine à états des hooks, l'agrégation, la purge
  des PID morts, le throttling, le format du snapshot ; `ruff` + `mypy`.
- **Firmware** : `dash_model` sans dépendance Arduino/LVGL, testé en natif (Unity) :
  parsing, tri par urgence, détection des transitions qui déclenchent le bip ;
  `pio check` (cppcheck).
- **Intégration** : Mosquitto en Docker + script qui publie des snapshots de test
  pour faire défiler tous les états sur l'écran.
