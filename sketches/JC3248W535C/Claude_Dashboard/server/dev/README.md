# Environnement de test Mosquitto

Broker MQTT local (sans TLS ni authentification ni persistance) et test
d'intégration de la chaîne complète hook → store → agent → broker.
Seul Docker est requis ; rien n'est écrit dans `~/.claude` ni `~/.config`.

## Test d'intégration

```bash
./dev/integration-test.sh            # depuis server/
CLAUDE_DASH_MQTT_PORT=1883 ./dev/integration-test.sh   # autre port hôte (défaut 18830)
```

Le script démarre Mosquitto (`docker compose -p claude-dash-it`), lance les
hooks et l'agent dans un conteneur `python:3.13-slim` (réseau hôte, uid courant,
store et config dans `/tmp` du conteneur, `tls = false`) puis vérifie le message
retained de `claude-dash/test/state` avec `mosquitto_sub` :

1. `SessionStart` + `Notification` `permission_prompt` → session `s1`, projet
   `arduino`, état `permission` ;
2. `Stop` pendant que l'agent tourne → état `idle` en quelques secondes ;
3. arrêt du broker 10 s puis redémarrage (retained perdu) → l'agent se reconnecte
   et republie aussitôt, bien avant le heartbeat de 60 s ;
4. `SessionEnd` → `sessions` vide.

Chaque étape affiche `PASS`/`FAIL` ; le code de sortie est non nul en cas
d'échec et le journal de l'agent est alors affiché. Conteneurs et réseau sont
supprimés à la sortie, même en cas d'erreur.

## Broker seul

```bash
docker compose -p claude-dash-it -f dev/docker-compose.yml up -d
docker run --rm --network host eclipse-mosquitto:2 \
  mosquitto_sub -h 127.0.0.1 -p 18830 -t 'claude-dash/#' -v
docker compose -p claude-dash-it -f dev/docker-compose.yml down
```

Config agent correspondante (`chmod 600`) :

```toml
[mqtt]
host = "127.0.0.1"
port = 18830
tls = false

[agent]
hostname = "test"
```

## Test de `install.sh`

```bash
./dev/test-install.sh                                  # depuis server/
CLAUDE_DASH_EXTRA_SETTINGS=~/.claude/settings.json ./dev/test-install.sh
```

Lance `install.sh` dans un conteneur `python:3.13-slim` (+ jq) sous un utilisateur
non root au `HOME` vide, dépôt monté en lecture seule : fusion et retrait des hooks
sur `fixtures/settings.json` (et, en option, sur une copie en lecture seule d'un
vrai `settings.json`), cas limites (JSON invalide, lien symbolique…), installation
complète sans systemd, réinstallation et désinstallation.
