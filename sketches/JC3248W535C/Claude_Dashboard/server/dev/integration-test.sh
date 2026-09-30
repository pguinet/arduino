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
# shellcheck source-path=SCRIPTDIR source=it-lib.sh
. ./it-lib.sh

compose() { docker compose -p "$PROJECT" -f docker-compose.yml "$@"; }

cleanup() {
  local rc=$?
  if [ "$FAILURES" -ne 0 ] || [ "$rc" -ne 0 ]; then show_agent_log; fi
  docker rm -f "$AGENT" >/dev/null 2>&1 || true
  compose down --remove-orphans >/dev/null 2>&1 || true
}
trap cleanup EXIT

# Lit le message retained du topic (vide si absent ou broker injoignable).
fetch_state() {
  docker run --rm --network host "$MQTT_IMAGE" \
    mosquitto_sub -h 127.0.0.1 -p "$PORT" -t "$TOPIC" -C 1 -W 2 2>/dev/null || true
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
start_agent_container "$(printf '[mqtt]\nhost = "127.0.0.1"\nport = %s\ntls = false\n[agent]\nhostname = "test"\n' "$PORT")"

echo "== Étape 1 : SessionStart + permission_prompt, puis démarrage de l'agent"
hook '{"hook_event_name":"SessionStart","session_id":"s1","cwd":"/x/arduino"}'
hook '{"hook_event_name":"Notification","session_id":"s1","notification_type":"permission_prompt"}'
start_agent
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

echo "== Étape 5 : arrêt propre de l'agent (SIGTERM) : retained effacé"
if stop_agent; then pass "agent arrêté par SIGTERM"; else fail "agent toujours actif 10 s après SIGTERM"; fi
if agent_log_has "hôte retiré"; then pass "journal : hôte retiré"; else fail "journal : pas de retrait de l'hôte"; fi
if [ -z "$(fetch_state)" ]; then
  pass "plus de message retained sur le topic (message vide publié)"
else
  fail "message retained toujours présent après l'arrêt"
fi

report
