#!/usr/bin/env bash
# Test d'intégration de la chaîne hook -> store -> agent -> broker (Mosquitto local, sans TLS).
#
# - Mosquitto via docker compose (projet claude-dash-it, port hôte $CLAUDE_DASH_MQTT_PORT) ;
# - hooks et agent dans un conteneur python:3.13-slim (réseau hôte, uid courant,
#   store et config dans /tmp du conteneur : rien ne touche ~/.claude ni ~/.config) ;
# - assertions sur le message retained lu avec mosquitto_sub.
#
# Affiche PASS/FAIL par étape, code de sortie non nul en cas d'échec.
set -euo pipefail

cd "$(dirname "$0")"
SRV_DIR="$(cd .. && pwd)"
PORT="${CLAUDE_DASH_MQTT_PORT:-18830}"
export CLAUDE_DASH_MQTT_PORT="$PORT"
PROJECT=claude-dash-it
AGENT=claude-dash-it-agent
TOPIC=claude-dash/test/state
MQTT_IMAGE=eclipse-mosquitto:2
FAILURES=0

compose() { docker compose -p "$PROJECT" -f docker-compose.yml "$@"; }

cleanup() {
  local rc=$?
  if [ "$FAILURES" -ne 0 ] || [ "$rc" -ne 0 ]; then
    echo "--- journal de l'agent ---"
    docker exec "$AGENT" cat /tmp/agent.log 2>/dev/null || true
  fi
  docker rm -f "$AGENT" >/dev/null 2>&1 || true
  compose down --remove-orphans >/dev/null 2>&1 || true
}
trap cleanup EXIT

pass() { echo "PASS  $*"; }
fail() { echo "FAIL  $*"; FAILURES=$((FAILURES + 1)); }

# Exécute une commande dans le conteneur de l'agent (entrée standard transmise).
in_agent() { docker exec -i "$AGENT" "$@"; }

hook() { echo "$1" | in_agent claude-dash-hook; }

# Lit le message retained du topic (vide si absent ou broker injoignable).
fetch_state() {
  docker run --rm --network host "$MQTT_IMAGE" \
    mosquitto_sub -h 127.0.0.1 -p "$PORT" -t "$TOPIC" -C 1 -W 2 2>/dev/null || true
}

# check_payload PAYLOAD EXPR : EXPR est une expression Python sur le JSON décodé `d`.
# EXPR vient uniquement de ce script (jamais du message MQTT) : l'eval est sans risque.
check_payload() {
  [ -n "$1" ] || return 1
  printf '%s' "$1" | in_agent python3 -c '
import json, sys
d = json.load(sys.stdin)
sys.exit(0 if eval(sys.argv[1], {}, {"d": d}) else 1)
' "$2" 2>/dev/null
}

# wait_for LIBELLE DELAI EXPR : attend jusqu'à DELAI s que le message retained vérifie EXPR.
wait_for() {
  local label=$1 timeout=$2 expr=$3 start payload=""
  start=$(date +%s)
  while [ $(($(date +%s) - start)) -lt "$timeout" ]; do
    payload=$(fetch_state)
    if check_payload "$payload" "$expr"; then
      pass "$label ($(($(date +%s) - start)) s)"
      echo "      $payload"
      return 0
    fi
    sleep 0.5
  done
  fail "$label (rien de conforme en ${timeout} s)"
  echo "      dernier message: ${payload:-<aucun>}"
  return 1
}

echo "== Démarrage de Mosquitto (port hôte $PORT)"
compose up -d --quiet-pull
for _ in $(seq 1 30); do
  if docker run --rm --network host "$MQTT_IMAGE" \
    mosquitto_pub -h 127.0.0.1 -p "$PORT" -t claude-dash-it/ping -m x >/dev/null 2>&1; then
    break
  fi
  sleep 0.5
done

echo "== Préparation du conteneur agent"
docker rm -f "$AGENT" >/dev/null 2>&1 || true
docker run -d --name "$AGENT" --network host --user "$(id -u):$(id -g)" \
  -e HOME=/tmp -e PATH=/tmp/.local/bin:/usr/local/bin:/usr/bin:/bin \
  -e CLAUDE_DASH_DIR=/tmp/dash -e XDG_CONFIG_HOME=/tmp/.config \
  -v "$SRV_DIR":/app -w /app python:3.13-slim sleep infinity >/dev/null
in_agent sh -c "
  pip install -q --user --no-warn-script-location -e . &&
  mkdir -p /tmp/.config/claude-dash &&
  printf '[mqtt]\nhost = \"127.0.0.1\"\nport = $PORT\ntls = false\n[agent]\nhostname = \"test\"\n' \
    > /tmp/.config/claude-dash/config.toml &&
  chmod 600 /tmp/.config/claude-dash/config.toml
"

echo "== Étape 1 : SessionStart + permission_prompt, puis démarrage de l'agent"
hook '{"hook_event_name":"SessionStart","session_id":"s1","cwd":"/x/arduino"}'
hook '{"hook_event_name":"Notification","session_id":"s1","notification_type":"permission_prompt"}'
docker exec -d "$AGENT" sh -c 'exec claude-dash-agent >/tmp/agent.log 2>&1'
wait_for "état permission publié (retained)" 15 \
  '(d["host"] == "test" and [(s["id"], s["project"], s["state"]) for s in d["sessions"]]
    == [("s1", "arduino", "permission")])' || true

echo "== Étape 2 : Stop pendant que l'agent tourne"
hook '{"hook_event_name":"Stop","session_id":"s1"}'
wait_for "passage à idle propagé" 6 \
  '[(s["id"], s["state"]) for s in d["sessions"]] == [("s1", "idle")]' || true

echo "== Étape 3 : redémarrage du broker (10 s d'arrêt, retained perdu)"
compose stop mosquitto >/dev/null 2>&1
sleep 10
if [ -z "$(fetch_state)" ]; then pass "broker arrêté : topic injoignable"; else fail "broker toujours joignable"; fi
compose start mosquitto >/dev/null 2>&1
# Sans republication forcée à la reconnexion, rien n'arriverait avant le heartbeat (60 s).
wait_for "republication après reconnexion (avant le heartbeat)" 30 \
  '[(s["id"], s["state"]) for s in d["sessions"]] == [("s1", "idle")]' || true

echo "== Étape 4 : SessionEnd"
hook '{"hook_event_name":"SessionEnd","session_id":"s1"}'
wait_for "sessions vides publiées" 6 'd["sessions"] == []' || true

echo
if [ "$FAILURES" -eq 0 ]; then
  echo "RÉSULTAT : PASS"
else
  echo "RÉSULTAT : FAIL ($FAILURES étape(s) en échec)"
  exit 1
fi
