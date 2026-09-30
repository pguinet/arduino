#!/usr/bin/env bash
# Test de install.sh dans un conteneur jetable (python:3.13-slim + jq), HOME fictif.
# Rien ne touche le ~/.claude, ~/.config ni le systemd de l'hôte ; le dépôt est monté
# en lecture seule.
#
#   ./dev/test-install.sh                                 fixture synthétique + install complète
#   CLAUDE_DASH_EXTRA_SETTINGS=~/.claude/settings.json ./dev/test-install.sh
#       rejoue aussi les tests de fusion sur une copie (lecture seule) d'un vrai settings.json
#
# Affiche PASS/FAIL par vérification, code de sortie non nul en cas d'échec.
set -euo pipefail

if [ "${1:-}" != "--inside" ]; then
  cd "$(dirname "$0")/.."
  extra=()
  if [ -n "${CLAUDE_DASH_EXTRA_SETTINGS:-}" ]; then
    extra=(-v "$(realpath "$CLAUDE_DASH_EXTRA_SETTINGS"):/fixtures/extra.json:ro")
  fi
  exec docker run --rm -v "$PWD":/src:ro "${extra[@]}" python:3.13-slim bash -c '
    set -e
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq >/dev/null && apt-get install -y -qq jq >/dev/null
    useradd -m tester
    exec su tester -s /bin/bash -c "bash /src/dev/test-install.sh --inside"
  '
fi

# ---------------------------------------------------------------- dans le conteneur
FAILURES=0
pass() { echo "PASS  $*"; }
fail() { echo "FAIL  $*"; FAILURES=$((FAILURES + 1)); }
check() { # check LIBELLE COMMANDE...
  local label=$1
  shift
  if "$@" >/dev/null 2>&1; then pass "$label"; else fail "$label"; fi
}

SRC=/tmp/src # copie inscriptible (pip construit dans l'arborescence source)
cp -r /src "$SRC"
rm -rf "$SRC"/*.egg-info "$SRC"/build
INSTALL="$SRC/install.sh"
SETTINGS="$HOME/.claude/settings.json"
HOOK="$HOME/.local/bin/claude-dash-hook"
EVENTS='["SessionStart","UserPromptSubmit","PreToolUse","PostToolUse","Notification","Stop","SessionEnd"]'
LOG=/tmp/install.log

# Commandes des hooks hors claude-dash, par événement (pour vérifier leur conservation).
foreign() {
  jq -S '(.hooks // {}) | map_values([.[] | .matcher as $m | .hooks[]?
          | select((.command // "") | test("claude-dash-hook$") | not)
          | {m: $m, h: .}]) | with_entries(select(.value != []))' "$1"
}

# Exactement une entrée claude-dash (chemin courant, sans matcher) par événement.
one_entry_per_event() {
  jq -e --arg cmd "$HOOK" --argjson ev "$EVENTS" '
    all($ev[] as $e | .hooks[$e];
      ([.[] | select(any(.hooks[]?; (.command // "") | test("claude-dash-hook$")))] as $ours
       | ($ours | length) == 1
         and ($ours[0] | has("matcher") | not)
         and $ours[0].hooks == [{"type": "command", "command": $cmd, "timeout": 5}]))
  ' "$SETTINGS"
}

no_claude_dash() { ! grep -q claude-dash-hook "$SETTINGS"; }
same_json() { [ "$(jq -S . "$1")" = "$(jq -S . "$2")" ]; }
same_outside_hooks() { [ "$(jq -S 'del(.hooks)' "$1")" = "$(jq -S 'del(.hooks)' "$2")" ]; }
same_foreign() { [ "$(foreign "$1")" = "$(foreign "$2")" ]; }
backups() { find "$HOME/.claude" -maxdepth 1 -name 'settings.json.bak-claude-dash-*' | wc -l; }
# Aucun fichier temporaire .settings.json.* laissé dans le dossier $1.
no_tmp_left() { ! find "$1" -maxdepth 1 -name '.settings.json.*' | grep -q .; }
reset_home() { rm -rf "$HOME/.claude" "$HOME/.config" "$HOME/.local"; mkdir -p "$HOME/.claude"; }

# Fusion puis désinstallation sur un settings.json donné.
merge_suite() {
  local name=$1 fixture=$2 orig=/tmp/orig.json sum
  echo "== Fusion des hooks : $name"
  reset_home
  cp "$fixture" "$SETTINGS"
  cp "$fixture" "$orig"
  check "[$name] --hooks-only réussit" "$INSTALL" --hooks-only
  check "[$name] une seule entrée claude-dash par événement (7)" one_entry_per_event
  check "[$name] hooks tiers conservés (commandes et matchers)" same_foreign "$orig" "$SETTINGS"
  check "[$name] reste du fichier inchangé" same_outside_hooks "$orig" "$SETTINGS"
  check "[$name] une sauvegarde identique à l'original" \
    bash -c "[ $(backups) -eq 1 ] && cmp -s '$orig' $HOME/.claude/settings.json.bak-claude-dash-*"
  sum=$(sha256sum "$SETTINGS")
  "$INSTALL" --hooks-only >"$LOG" 2>&1
  check "[$name] 2e passage : fichier inchangé" bash -c "[ '$sum' = \"\$(sha256sum '$SETTINGS')\" ]"
  check "[$name] 2e passage : pas de nouvelle sauvegarde" bash -c "[ $(backups) -eq 1 ]"
  check "[$name] 2e passage : message déjà à jour" grep -q "déjà à jour" "$LOG"

  check "[$name] --uninstall réussit" "$INSTALL" --uninstall
  check "[$name] désinstallation : plus aucune entrée claude-dash" no_claude_dash
  check "[$name] désinstallation : hooks tiers conservés" same_foreign "$orig" "$SETTINGS"
  check "[$name] désinstallation : reste du fichier inchangé" \
    same_outside_hooks "$orig" "$SETTINGS"
  if ! grep -q claude-dash-hook "$orig"; then
    check "[$name] install + uninstall = original" same_json "$orig" "$SETTINGS"
  fi
  sum=$(sha256sum "$SETTINGS")
  "$INSTALL" --uninstall >/dev/null 2>&1
  check "[$name] 2e désinstallation : fichier inchangé" \
    bash -c "[ '$sum' = \"\$(sha256sum '$SETTINGS')\" ]"
  check "[$name] install + uninstall rapprochés : 2 sauvegardes distinctes" \
    bash -c "[ $(backups) -eq 2 ]"
  check "[$name] aucun fichier temporaire laissé" no_tmp_left "$HOME/.claude"
}

merge_suite synthétique "$SRC/dev/fixtures/settings.json"
[ -f /fixtures/extra.json ] && merge_suite réel /fixtures/extra.json

echo "== Cas limites"
reset_home
bad='{"hooks": {"Stop": ['
printf '%s' "$bad" >"$SETTINGS"
check "JSON invalide : échec" bash -c "! '$INSTALL' --hooks-only 2>$LOG"
check "JSON invalide : erreur de jq affichée" grep -qi "parse error" "$LOG"
check "JSON invalide : fichier intact" bash -c "[ \"\$(cat '$SETTINGS')\" = '$bad' ]"
check "JSON invalide : aucune sauvegarde" bash -c "[ $(backups) -eq 0 ]"

reset_home
echo '{"hooks": []}' >"$SETTINGS"
check "hooks non objet : échec, fichier intact" \
  bash -c "! '$INSTALL' --hooks-only && [ \"\$(cat '$SETTINGS')\" = '{\"hooks\": []}' ]"

reset_home
echo '{"hooks": {"Stop": {"oops": 1}}}' >"$SETTINGS"
check "événement non tableau : échec" bash -c "! '$INSTALL' --hooks-only"

reset_home
echo '{"hooks": {"Stop": ["echo x"]}}' >"$SETTINGS"
check "entrée d'événement non objet : échec, fichier intact" \
  bash -c "! '$INSTALL' --hooks-only && grep -q 'echo x' '$SETTINGS' && ! grep -q claude-dash '$SETTINGS'"

reset_home
cp "$SRC/dev/fixtures/settings.json" "$SETTINGS"
concurrent='{"modifie": "ailleurs"}'
check "modification concurrente : échec" \
  env _CLAUDE_DASH_TEST_BEFORE_WRITE="echo '$concurrent' >'$SETTINGS'" \
  bash -c "! '$INSTALL' --hooks-only 2>$LOG"
check "modification concurrente : message clair" grep -q "modifié pendant" "$LOG"
check "modification concurrente : version concurrente conservée" \
  bash -c "[ \"\$(cat '$SETTINGS')\" = '$concurrent' ]"
check "modification concurrente : aucun fichier temporaire laissé" no_tmp_left "$HOME/.claude"

reset_home
"$INSTALL" --hooks-only >"$LOG" 2>&1
check "--hooks-only sans commande installée : avertissement" grep -q "non exécutable" "$LOG"

reset_home
rm -rf "$HOME/.claude"
check "settings.json absent : créé" "$INSTALL" --hooks-only
check "settings.json absent : 7 entrées" one_entry_per_event
check "désinstallation : retour à {}" bash -c "'$INSTALL' --uninstall && [ \"\$(jq -c . '$SETTINGS')\" = '{}' ]"
rm -f "$SETTINGS"
check "désinstallation sans settings.json : ne le crée pas" \
  bash -c "'$INSTALL' --uninstall && [ ! -e '$SETTINGS' ]"

reset_home
mkdir -p "$HOME/dotfiles"
cp "$SRC/dev/fixtures/settings.json" "$HOME/dotfiles/settings.json"
chmod 640 "$HOME/dotfiles/settings.json"
ln -s "$HOME/dotfiles/settings.json" "$SETTINGS"
"$INSTALL" --hooks-only >/dev/null 2>&1
check "lien symbolique conservé et cible mise à jour" \
  bash -c "[ -L '$SETTINGS' ] && grep -q claude-dash-hook '$HOME/dotfiles/settings.json'"
check "droits du fichier conservés" bash -c "[ \$(stat -c %a '$HOME/dotfiles/settings.json') = 640 ]"
check "lien symbolique : temporaires hors de ~/.claude et nettoyés" \
  bash -c "$(declare -f no_tmp_left); no_tmp_left '$HOME/dotfiles' && no_tmp_left '$HOME/.claude'"

reset_home
check "option inconnue : échec" bash -c "! '$INSTALL' --bogus"

echo "== Installation complète (sans systemd --user)"
reset_home
echo '{"hooks": ' >"$SETTINGS"
check "settings.json invalide : install échoue avant le venv" \
  bash -c "! '$INSTALL' && [ ! -e '$HOME/.local/share/claude-dash' ]"

reset_home
cp "$SRC/dev/fixtures/settings.json" "$SETTINGS"
mkdir -p "$HOME/.local/share/claude-dash/venv/bin"
echo "reste d'un venv cassé" >"$HOME/.local/share/claude-dash/venv/bin/python"
if "$INSTALL" >"$LOG" 2>&1; then pass "install.sh réussit"; else fail "install.sh réussit"; cat "$LOG"; fi
CONF="$HOME/.config/claude-dash/config.toml"
for cmd in claude-dash-hook claude-dash-statusline claude-dash-agent; do
  check "commande $cmd exécutable" test -x "$HOME/.local/bin/$cmd"
done
check "config créée en 600" bash -c "[ \$(stat -c %a '$CONF') = 600 ]"
check "config créée : à renseigner" grep -q "config créée : .*(à renseigner)" "$LOG"
check "config par défaut : Scaleway mTLS sans retain" \
  bash -c "grep -qx 'host = \"iot.fr-par.scw.cloud\".*' '$CONF' && grep -qx 'retain = false' '$CONF'"
set +e
XDG_CONFIG_HOME="$HOME/.config" "$HOME/.local/bin/claude-dash-agent" >"$LOG.agent" 2>&1
agent_rc=$?
set -e
check "agent : config d'exemple refusée (code 2, pas de boucle systemd)" \
  bash -c "[ $agent_rc = 2 ] && grep -q \"valeur d'exemple non renseignée : client_id\" '$LOG.agent'"
check "venv cassé recréé" test -x "$HOME/.local/share/claude-dash/venv/bin/python"
check "service copié" test -f "$HOME/.config/systemd/user/claude-dash-agent.service"
check "service : pas de relance en boucle sur erreur de config" \
  grep -qx "RestartPreventExitStatus=2" "$HOME/.config/systemd/user/claude-dash-agent.service"
check "absence de systemd signalée" grep -q "systemd --user indisponible" "$LOG"
check "rappel statusline affiché" grep -q "timeout 1 $HOME/.local/bin/claude-dash-statusline" "$LOG"
check "hooks fusionnés" one_entry_per_event
echo '{"hook_event_name":"SessionStart","session_id":"t1","cwd":"/x/demo"}' | "$HOOK"
check "le hook installé écrit la session" \
  jq -e '.state == "idle" and .project == "demo"' "$HOME/.claude/dashboard/sessions/t1.json"
check "la statusline installée s'exécute" \
  bash -c "echo '{}' | timeout 5 '$HOME/.local/bin/claude-dash-statusline'"

"$INSTALL" >"$LOG" 2>&1 || true
check "install avec placeholders : config signalée non renseignée" \
  grep -q "encore non renseignée" "$LOG"
# valeurs actives renseignées ; les votre_ des blocs commentés restent
sed -i -e 's/votre_device_id_serveur/broker.example/' -e 's/votre_hostname/srv-test/' "$CONF"
chmod 644 "$CONF"
sum=$(sha256sum "$SETTINGS")
if "$INSTALL" >"$LOG" 2>&1; then pass "2e install.sh réussit"; else fail "2e install.sh réussit"; cat "$LOG"; fi
check "2e install : config conservée" grep -q broker.example "$CONF"
check "2e install : placeholders commentés ignorés" bash -c "! grep -q 'non renseignée' '$LOG'"
check "2e install : config remise en 600" bash -c "[ \$(stat -c %a '$CONF') = 600 ]"
check "2e install : settings.json inchangé" bash -c "[ '$sum' = \"\$(sha256sum '$SETTINGS')\" ]"
check "2e install : rappel des sessions à redémarrer" grep -q "/hooks" "$LOG"

# statusLine pointant vers un script qui appelle déjà le collecteur
# shellcheck disable=SC2016  # contenu littéral du script
printf '#!/bin/sh\ninput=$(cat)\nprintf "%%s" "$input" | timeout 1 claude-dash-statusline\n' \
  >"$HOME/.claude/sl.sh"
jq '.statusLine.command = "bash ~/.claude/sl.sh"' "$SETTINGS" >/tmp/s.json
cat /tmp/s.json >"$SETTINGS"
"$INSTALL" >"$LOG" 2>&1 || true
check "statusline déjà branchée : détectée dans le script" grep -q "déjà présent ($HOME/.claude/sl.sh)" "$LOG"
check "statusline déjà branchée : pas de rappel" bash -c "! grep -q 'AU PREMIER PLAN' '$LOG'"
sed -i '/claude-dash-statusline/d' "$HOME/.claude/sl.sh"
"$INSTALL" >"$LOG" 2>&1 || true
check "statusline non branchée : rappel nommant le script" \
  grep -q "ajoute à $HOME/.claude/sl.sh" "$LOG"

# lien d'activation laissé par un enable antérieur
mkdir -p "$HOME/.config/systemd/user/default.target.wants"
ln -sf ../claude-dash-agent.service \
  "$HOME/.config/systemd/user/default.target.wants/claude-dash-agent.service"

if "$INSTALL" --uninstall >"$LOG" 2>&1; then pass "désinstallation complète réussit"; else
  fail "désinstallation complète réussit"; cat "$LOG"; fi
check "venv supprimé" test ! -e "$HOME/.local/share/claude-dash"
check "commandes supprimées" bash -c "! ls '$HOME'/.local/bin/claude-dash-* 2>/dev/null"
check "service supprimé" test ! -e "$HOME/.config/systemd/user/claude-dash-agent.service"
check "lien default.target.wants supprimé" \
  test ! -L "$HOME/.config/systemd/user/default.target.wants/claude-dash-agent.service"
check "service non arrêtable signalé" grep -q "service non arrêté" "$LOG"
check "config conservée" test -f "$CONF"
check "hooks retirés" no_claude_dash
check "2e désinstallation réussit" "$INSTALL" --uninstall

echo
if [ "$FAILURES" -eq 0 ]; then
  echo "RÉSULTAT : PASS"
else
  echo "RÉSULTAT : FAIL ($FAILURES vérification(s) en échec)"
  exit 1
fi
