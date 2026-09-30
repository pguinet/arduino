#!/usr/bin/env bash
# Test d'intégration mTLS sans retained : imite Scaleway IoT Hub (plan Shared).
#
# - PKI jetable (CA, broker, client « serveur », client « écran ») générée par openssl
#   dans un conteneur sous l'uid courant, dans un dossier temporaire supprimé à la fin ;
# - Mosquitto 2 en TLS avec certificat client obligatoire (require_certificate) ;
# - agent configuré comme pour Scaleway : certfile/keyfile, client_id = UUID,
#   retain = false, heartbeat = 5 s.
#
# Affiche PASS/FAIL par étape, code de sortie non nul en cas d'échec.
set -euo pipefail

cd "$(dirname "$0")"
SRV_DIR="$(cd .. && pwd)"
PORT="${CLAUDE_DASH_MQTTS_PORT:-18883}"
AGENT=claude-dash-it-mtls-agent
BROKER=claude-dash-it-mtls-broker
LIVE_SUB=claude-dash-it-mtls-sub
TOPIC=claude-dash/test-mtls/state
HEARTBEAT=5
CLIENT_ID=$(cat /proc/sys/kernel/random/uuid)
# shellcheck source-path=SCRIPTDIR source=it-lib.sh
. ./it-lib.sh

PKI=$(mktemp -d "${TMPDIR:-/tmp}/claude-dash-mtls.XXXXXX")

cleanup() {
  local rc=$?
  if [ "$FAILURES" -ne 0 ] || [ "$rc" -ne 0 ]; then
    show_agent_log
    echo "--- journal du broker ---"
    docker logs "$BROKER" 2>&1 | tail -30 || true
  fi
  docker rm -f "$AGENT" "$BROKER" "$LIVE_SUB" >/dev/null 2>&1 || true
  rm -rf "$PKI"
}
trap cleanup EXIT

# Client Mosquitto sous l'uid courant (lecture des clés en 600), PKI en lecture seule.
mqtt_cli() {
  docker run --rm --network host --user "$(id -u):$(id -g)" -v "$PKI":/pki:ro \
    "$MQTT_IMAGE" "$@"
}

# Abonné « écran » (certificat client) : options communes.
SCREEN_TLS=(-h 127.0.0.1 -p "$PORT" --cafile /pki/ca.pem --cert /pki/screen.pem
  --key /pki/screen.key)

# Dernier message reçu par l'abonné démarré avant les changements.
fetch_state() { docker logs "$LIVE_SUB" 2>/dev/null | tail -n 1; }

echo "== Génération de la PKI de test (dossier temporaire)"
docker run --rm --user "$(id -u):$(id -g)" -v "$PKI":/pki -w /pki "$PY_IMAGE" sh -euc '
  ec="-newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes"
  # shellcheck disable=SC2086
  openssl req -x509 $ec -days 1 -subj "/CN=claude-dash test CA" -keyout ca.key -out ca.pem \
    -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" 2>/dev/null
  leaf() {  # leaf NOM CN USAGE [SAN]
    ext="basicConstraints=CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=$3\nsubjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid\n"
    [ -n "${4:-}" ] && ext="${ext}subjectAltName=$4\n"
    printf "%b" "$ext" > "$1.ext"
    # shellcheck disable=SC2086
    openssl req $ec -subj "/CN=$2" -keyout "$1.key" -out "$1.csr" 2>/dev/null
    openssl x509 -req -in "$1.csr" -CA ca.pem -CAkey ca.key -CAcreateserial -days 1 \
      -extfile "$1.ext" -out "$1.pem" 2>/dev/null
  }
  leaf broker localhost serverAuth "IP:127.0.0.1,DNS:localhost"
  leaf server claude-dash-server clientAuth
  leaf screen claude-dash-screen clientAuth
  chmod 600 ./*.key
  rm -f ./*.csr ./*.ext ./*.srl
'
cat >"$PKI/mosquitto.conf" <<'EOF'
listener 8883
cafile /pki/ca.pem
certfile /pki/broker.pem
keyfile /pki/broker.key
require_certificate true
use_identity_as_username false
allow_anonymous true
EOF
pass "PKI générée : $(cd "$PKI" && echo ./*.pem)"

echo "== Démarrage de Mosquitto mTLS (port hôte $PORT)"
docker rm -f "$BROKER" >/dev/null 2>&1 || true
docker run -d --name "$BROKER" --user "$(id -u):$(id -g)" -p "127.0.0.1:$PORT:8883" \
  -v "$PKI":/pki:ro "$MQTT_IMAGE" mosquitto -c /pki/mosquitto.conf >/dev/null
for _ in $(seq 1 30); do
  if mqtt_cli mosquitto_pub "${SCREEN_TLS[@]}" -t claude-dash-it/ping -m x >/dev/null 2>&1; then
    break
  fi
  sleep 0.5
done

echo "== Préparation du conteneur agent (client id $CLIENT_ID)"
start_agent_container "$(
  cat <<EOF
[mqtt]
host = "127.0.0.1"
port = $PORT
tls = true
ca_certs = "/pki/ca.pem"
certfile = "/pki/server.pem"
keyfile = "/pki/server.key"
client_id = "$CLIENT_ID"
retain = false
[agent]
hostname = "test-mtls"
heartbeat = $HEARTBEAT
EOF
)" -v "$PKI":/pki:ro

echo "== Étape 1 : connexion mTLS de l'agent avec le client id configuré"
hook '{"hook_event_name":"SessionStart","session_id":"s1","cwd":"/x/arduino"}'
start_agent
connected=no
for _ in $(seq 1 30); do
  if docker logs "$BROKER" 2>&1 | grep -q "as $CLIENT_ID "; then connected=yes; break; fi
  sleep 0.5
done
if [ "$connected" = yes ]; then
  pass "broker : client $CLIENT_ID connecté avec son certificat"
else
  fail "agent non connecté au broker mTLS"
fi

echo "== Étape 2 : abonné démarré AVANT le changement"
docker rm -f "$LIVE_SUB" >/dev/null 2>&1 || true
docker run -d --name "$LIVE_SUB" --network host --user "$(id -u):$(id -g)" \
  -v "$PKI":/pki:ro "$MQTT_IMAGE" mosquitto_sub "${SCREEN_TLS[@]}" -t "$TOPIC" >/dev/null
sleep 1
hook '{"hook_event_name":"Notification","session_id":"s1","notification_type":"permission_prompt"}'
wait_for "état permission reçu en direct" 8 \
  '[(s["id"], s["project"], s["state"]) for s in d["sessions"]] == [("s1", "arduino", "permission")]' ||
  true

echo "== Étape 3 : aucun message retained sur le broker"
retained=$(mqtt_cli mosquitto_sub "${SCREEN_TLS[@]}" -t "$TOPIC" --retained-only -C 1 -W 3 \
  2>/dev/null || true)
if [ -z "$retained" ]; then pass "pas de retained"; else fail "message retained trouvé: $retained"; fi

echo "== Étape 4 : nouvel abonné après une période d'inactivité (heartbeat ${HEARTBEAT} s)"
sleep $((HEARTBEAT + 3))
start=$(date +%s)
payload=$(mqtt_cli mosquitto_sub "${SCREEN_TLS[@]}" -t "$TOPIC" -C 1 -W $((HEARTBEAT + 5)) \
  2>/dev/null || true)
elapsed=$(($(date +%s) - start))
if check_payload "$payload" '[s["state"] for s in d["sessions"]] == ["permission"]' &&
  [ "$elapsed" -le $((HEARTBEAT + 3)) ]; then
  pass "état reçu par heartbeat en ${elapsed} s"
else
  fail "rien de conforme en ${elapsed} s (dernier message: ${payload:-<aucun>})"
fi

echo "== Étape 5 : client sans certificat refusé"
set +e
nocert=$(mqtt_cli mosquitto_sub -h 127.0.0.1 -p "$PORT" --cafile /pki/ca.pem -t "$TOPIC" \
  -C 1 -W $((HEARTBEAT + 3)) 2>&1)
nocert_rc=$?
set -e
# Accepté, il recevrait le heartbeat avant l'expiration : échec + aucun JSON = refus.
if [ "$nocert_rc" -ne 0 ] && ! grep -q '"sessions"' <<<"$nocert"; then
  pass "connexion sans certificat refusée (rc=$nocert_rc: $(head -n 1 <<<"$nocert"))"
else
  fail "client sans certificat accepté (rc=$nocert_rc: $nocert)"
fi

echo "== Étape 6 : SessionEnd reçu en direct"
hook '{"hook_event_name":"SessionEnd","session_id":"s1"}'
wait_for "sessions vides reçues" 6 'd["sessions"] == []' || true

report
