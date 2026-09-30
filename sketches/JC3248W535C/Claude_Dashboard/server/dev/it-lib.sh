# shellcheck shell=bash
# Fonctions communes aux tests d'intégration (sourcé par integration-test*.sh).
#
# Variables attendues : AGENT (nom du conteneur agent), SRV_DIR (dossier server/).
# Chaque script définit fetch_state (lit un message du topic, vide si rien).

# shellcheck disable=SC2034  # utilisées par les scripts qui sourcent ce fichier
MQTT_IMAGE=eclipse-mosquitto:2
PY_IMAGE=python:3.13-slim
FAILURES=0

pass() { echo "PASS  $*"; }
fail() { echo "FAIL  $*"; FAILURES=$((FAILURES + 1)); }

# Exécute une commande dans le conteneur de l'agent (entrée standard transmise).
in_agent() { docker exec -i "$AGENT" "$@"; }

hook() { echo "$1" | in_agent claude-dash-hook; }

# start_agent_container CONFIG_TOML [ARGS docker run...] : conteneur Python détaché
# (réseau hôte, uid courant, store et config dans /tmp du conteneur), paquet installé
# et config écrite en chmod 600. L'agent lui-même est lancé par start_agent.
start_agent_container() {
  local config=$1
  shift
  docker rm -f "$AGENT" >/dev/null 2>&1 || true
  docker run -d --name "$AGENT" --network host --user "$(id -u):$(id -g)" \
    -e HOME=/tmp -e PATH=/tmp/.local/bin:/usr/local/bin:/usr/bin:/bin \
    -e CLAUDE_DASH_DIR=/tmp/dash -e XDG_CONFIG_HOME=/tmp/.config \
    -v "$SRV_DIR":/app -w /app "$@" "$PY_IMAGE" sleep infinity >/dev/null
  printf '%s' "$config" | in_agent sh -c '
    pip install -q --user --no-warn-script-location -e . &&
    mkdir -p /tmp/.config/claude-dash &&
    cat > /tmp/.config/claude-dash/config.toml &&
    chmod 600 /tmp/.config/claude-dash/config.toml
  '
}

# PID de l'agent dans /tmp/agent.pid (exec : le PID du shell devient celui de l'agent).
start_agent() {
  docker exec -d "$AGENT" sh -c 'echo $$ >/tmp/agent.pid; exec claude-dash-agent >/tmp/agent.log 2>&1'
}

# stop_agent : SIGTERM à l'agent (comme systemctl stop), attend sa fin (10 s max).
# Retourne 0 si l'agent s'est arrêté.
stop_agent() {
  # shellcheck disable=SC2016  # script exécuté par le sh du conteneur
  in_agent sh -c '
    pid=$(cat /tmp/agent.pid)
    kill -TERM "$pid"
    for _ in $(seq 1 20); do
      kill -0 "$pid" 2>/dev/null || exit 0
      sleep 0.5
    done
    exit 1
  '
}

# agent_log_has TEXTE : vrai si le journal de l'agent contient TEXTE.
agent_log_has() { docker exec "$AGENT" grep -q -- "$1" /tmp/agent.log 2>/dev/null; }

show_agent_log() {
  echo "--- journal de l'agent ---"
  docker exec "$AGENT" cat /tmp/agent.log 2>/dev/null || true
}

# check_payload PAYLOAD EXPR : EXPR est une expression Python sur le JSON décodé `d`.
# EXPR vient uniquement des scripts (jamais du message MQTT) : l'eval est sans risque.
check_payload() {
  [ -n "$1" ] || return 1
  printf '%s' "$1" | in_agent python3 -c '
import json, sys
d = json.load(sys.stdin)
sys.exit(0 if eval(sys.argv[1], {}, {"d": d}) else 1)
' "$2" 2>/dev/null
}

# wait_for LIBELLE DELAI EXPR : attend jusqu'à DELAI s qu'un message lu par
# fetch_state vérifie EXPR.
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

report() {
  echo
  if [ "$FAILURES" -eq 0 ]; then
    echo "RÉSULTAT : PASS"
  else
    echo "RÉSULTAT : FAIL ($FAILURES étape(s) en échec)"
    exit 1
  fi
}
