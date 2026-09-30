#!/usr/bin/env bash
# Installe (ou désinstalle) l'agent claude-dash pour l'utilisateur courant, sans sudo.
#
#   ./install.sh               installation complète (idempotente, relançable pour mettre à jour)
#   ./install.sh --hooks-only  fusionne seulement les hooks dans settings.json de Claude Code
#   ./install.sh --uninstall   retire hooks, service, venv et commandes (garde config et données)
#
# Variables : PYTHON (défaut python3), CLAUDE_CONFIG_DIR (défaut ~/.claude).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PYTHON="${PYTHON:-python3}"
VENV="$HOME/.local/share/claude-dash/venv"
BIN="$HOME/.local/bin"
# Chemins fixes (pas $XDG_CONFIG_HOME) : l'agent lancé par systemd --user lit ~/.config,
# et ~/.config/systemd/user est l'emplacement fiable des units utilisateur.
CONF_DIR="$HOME/.config/claude-dash"
UNIT_DIR="$HOME/.config/systemd/user"
UNIT=claude-dash-agent.service
SETTINGS="${CLAUDE_CONFIG_DIR:-$HOME/.claude}/settings.json"
HOOK="$BIN/claude-dash-hook"
COMMANDS=(claude-dash-hook claude-dash-statusline claude-dash-agent)
EVENTS='["SessionStart","UserPromptSubmit","PreToolUse","PostToolUse","Notification","Stop","SessionEnd"]'

# Filtres jq. Nos entrées sont reconnues à la commande (…/claude-dash-hook), quel que
# soit le chemin d'installation : seules elles sont retirées, le reste est conservé tel quel.
JQ_LIB='
def ours: (.command? // "") | type == "string" and test("(^|/)claude-dash-hook$");
def strip_group:
  if (.hooks? | type) == "array" and any(.hooks[]; ours)
  then .hooks |= map(select(ours | not)) | select(.hooks | length > 0)
  else . end;
def strip_all:
  if (.hooks | type) == "object"
  then .hooks |= with_entries(if (.value | type) == "array"
                               then .value |= map(strip_group) else . end)
  else . end;
'
# shellcheck disable=SC2016  # $events/$cmd sont des variables jq
JQ_INSTALL="$JQ_LIB"'
strip_all
| reduce $events[] as $ev (.;
    .hooks[$ev] = ((.hooks[$ev] // [])
      + [{"hooks": [{"type": "command", "command": $cmd, "timeout": 5}]}]))
'
# shellcheck disable=SC2016
JQ_UNINSTALL="$JQ_LIB"'
strip_all
| if (.hooks | type) == "object" then
    .hooks |= with_entries(select(.key as $k | ($events | index($k)) == null
                                  or .value != []))
    | if .hooks == {} then del(.hooks) else . end
  else . end
'

die() { echo "ERREUR : $*" >&2; exit 1; }
info() { echo ">> $*"; }

usage() { sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; }

require_jq() { command -v jq >/dev/null || die "jq requis (apt install jq)"; }

# check_settings [FICHIER] : objet JSON dont hooks est un objet de tableaux d'objets.
# Les erreurs de syntaxe de jq restent affichées.
check_settings() {
  local file=${1:-$SETTINGS}
  jq -e '
    type == "object"
    and ((.hooks // {}) | type == "object")
    and ([(.hooks // {})[] | type == "array"] | all)
    and ([(.hooks // {})[][] | type == "object"] | all)
  ' "$file" >/dev/null \
    || die "$SETTINGS invalide (JSON attendu : {\"hooks\": {\"<événement>\": [{…}]}}) : fichier laissé intact"
}

# edit_settings FILTRE : applique le filtre jq ; écrit seulement si le contenu change,
# après une sauvegarde. Écriture atomique : fichier temporaire dans le dossier de la
# cible réelle (lien symbolique résolu), mêmes droits, puis mv ; abandon si le fichier a
# été modifié entre-temps (Claude Code ouvert, autre éditeur).
edit_settings() {
  local filter=$1 target dir orig tmp backup
  require_jq
  if [ ! -e "$SETTINGS" ]; then
    mkdir -p "$(dirname "$SETTINGS")"
    echo '{}' >"$SETTINGS"
  fi
  target="$(readlink -f "$SETTINGS")"
  dir="$(dirname "$target")"
  orig="$(mktemp "$dir/.settings.json.orig.XXXXXX")"
  tmp="$(mktemp "$dir/.settings.json.new.XXXXXX")"
  # shellcheck disable=SC2064  # chemins figés volontairement
  trap "rm -f '$orig' '$tmp'" EXIT
  cp "$target" "$orig" # contenu lu : base de la fusion et de la détection de conflit
  check_settings "$orig"
  jq --arg cmd "$HOOK" --argjson events "$EVENTS" "$filter" "$orig" >"$tmp"
  jq -e 'type == "object"' "$tmp" >/dev/null || die "fusion jq invalide : $SETTINGS intact"
  if jq -e --slurpfile new "$tmp" '. == $new[0]' "$orig" >/dev/null; then
    info "hooks Claude Code déjà à jour ($SETTINGS)"
    return
  fi
  backup="$SETTINGS.bak-claude-dash-$(date +%Y%m%d-%H%M%S-%N)-$$"
  cp "$orig" "$backup"
  chmod --reference="$target" "$tmp"
  # test uniquement : point d'injection pour dev/test-install.sh (modification concurrente)
  if [ -n "${_CLAUDE_DASH_TEST_BEFORE_WRITE:-}" ]; then bash -c "$_CLAUDE_DASH_TEST_BEFORE_WRITE"; fi
  cmp -s "$orig" "$target" \
    || die "$SETTINGS a été modifié pendant l'opération : rien écrit, relance le script"
  mv -f "$tmp" "$target"
  info "hooks Claude Code mis à jour ($SETTINGS, sauvegarde $backup)"
}

merge_hooks() {
  edit_settings "$JQ_INSTALL"
  [ -x "$HOOK" ] || info "ATTENTION : $HOOK absent ou non exécutable (lancer ./install.sh sans option)"
}
remove_hooks() {
  if [ -e "$SETTINGS" ]; then edit_settings "$JQ_UNINSTALL"; else info "pas de $SETTINGS"; fi
}

# systemd --user utilisable (absent dans un conteneur ou sans session utilisateur) ?
have_user_systemd() {
  command -v systemctl >/dev/null && systemctl --user show-environment >/dev/null 2>&1
}

check_python() {
  command -v "$PYTHON" >/dev/null || die "$PYTHON introuvable (Python >= 3.11 requis)"
  "$PYTHON" -c 'import sys; sys.exit(sys.version_info < (3, 11))' \
    || die "Python >= 3.11 requis ($("$PYTHON" -V 2>&1)) ; essayer PYTHON=python3.12 $0"
  "$PYTHON" -c 'import ensurepip, venv' 2>/dev/null \
    || die "module venv indisponible (Debian/Ubuntu : apt install python3-venv)"
}

install_package() {
  check_python
  # --clear : repart de zéro si un venv cassé (bin/python absent) traîne
  [ -x "$VENV/bin/python" ] || "$PYTHON" -m venv --clear "$VENV"
  "$VENV/bin/pip" install -q --disable-pip-version-check --upgrade "$HERE"
  # même numéro de version : forcer la réinstallation du code (mise à jour du dépôt)
  "$VENV/bin/pip" install -q --disable-pip-version-check --force-reinstall --no-deps "$HERE"
  mkdir -p "$BIN"
  for cmd in "${COMMANDS[@]}"; do
    ln -sfn "$VENV/bin/$cmd" "$BIN/$cmd"
  done
  info "commandes installées dans $BIN (venv $VENV)"
}

# Retourne 0 si la config est prête (existante et sans placeholder).
install_config() {
  mkdir -p "$CONF_DIR"
  chmod 700 "$CONF_DIR"
  if [ ! -f "$CONF_DIR/config.toml" ]; then
    install -m 600 "$HERE/config.toml.example" "$CONF_DIR/config.toml"
    info "config créée : $CONF_DIR/config.toml (à renseigner)"
    return 1
  fi
  chmod 600 "$CONF_DIR/config.toml"
  # placeholders hors commentaires (l'exemple en garde dans ses blocs commentés)
  if sed 's/#.*//' "$CONF_DIR/config.toml" | grep -q 'votre_'; then
    info "config $CONF_DIR/config.toml encore non renseignée (placeholders votre_…)"
    return 1
  fi
  return 0
}

install_unit() {
  local config_ready=$1
  mkdir -p "$UNIT_DIR"
  cp "$HERE/$UNIT" "$UNIT_DIR/$UNIT"
  if ! have_user_systemd; then
    info "systemd --user indisponible : service copié dans $UNIT_DIR mais non activé."
    info "Sur une session normale : systemctl --user daemon-reload && systemctl --user enable --now $UNIT"
    return
  fi
  systemctl --user daemon-reload
  systemctl --user enable "$UNIT" 2>/dev/null || die "systemctl --user enable $UNIT a échoué"
  if [ "$config_ready" = yes ]; then
    systemctl --user restart "$UNIT"
    info "service $UNIT activé et (re)démarré"
  else
    info "service $UNIT activé ; démarre-le une fois la config renseignée :"
    info "  systemctl --user restart $UNIT"
  fi
}

# Rappel statusline : cherche claude-dash-statusline dans la commande statusLine ou dans
# le script qu'elle lance (premier mot désignant un fichier, ~ développé).
statusline_hint() {
  local cmd word script=""
  local -a words=()
  cmd="$(jq -r '.statusLine.command? // empty' "$SETTINGS" 2>/dev/null)" || cmd=""
  read -ra words <<<"$cmd" || true
  for word in "${words[@]}"; do
    word="${word/#\~/$HOME}"
    if [ -f "$word" ]; then script=$word; break; fi
  done
  if [[ "$cmd" == *claude-dash-statusline* ]] \
    || { [ -n "$script" ] && grep -q claude-dash-statusline "$script"; }; then
    echo "1. Statusline : appel à claude-dash-statusline déjà présent (${script:-statusLine})."
    return
  fi
  cat <<EOF
1. Statusline : ajoute à ${script:-ton script}, juste après la lecture de stdin (input=\$(cat)),
   un appel AU PREMIER PLAN (pas de &) :
     printf '%s' "\$input" | timeout 1 $BIN/claude-dash-statusline >/dev/null 2>&1
   (sans statusline : voir le README pour en créer une minimale)
EOF
}

do_install() {
  local config_ready=no
  require_jq
  [ ! -e "$SETTINGS" ] || check_settings # échouer avant d'installer quoi que ce soit
  install_package
  if install_config; then config_ready=yes; fi
  merge_hooks
  install_unit "$config_ready"
  echo
  echo "Installation terminée."
  statusline_hint
  cat <<EOF
2. Pour que l'agent tourne sans session SSH ouverte :
     loginctl enable-linger \$USER
3. Logs : journalctl --user -u claude-dash-agent -f
4. Redémarre les sessions Claude Code ouvertes (ou vérifie /hooks) pour activer les hooks.
EOF
}

do_uninstall() {
  require_jq
  remove_hooks
  if have_user_systemd; then
    systemctl --user disable --now "$UNIT" >/dev/null 2>&1 || true
  elif [ -e "$UNIT_DIR/$UNIT" ] || [ -L "$UNIT_DIR/default.target.wants/$UNIT" ]; then
    info "ATTENTION : systemd --user indisponible, service non arrêté (pkill -f claude-dash-agent)"
  fi
  rm -f "$UNIT_DIR/default.target.wants/$UNIT"
  if [ -f "$UNIT_DIR/$UNIT" ]; then
    rm -f "$UNIT_DIR/$UNIT"
    info "service $UNIT supprimé"
  fi
  have_user_systemd && systemctl --user daemon-reload
  for cmd in "${COMMANDS[@]}"; do
    # uniquement nos liens (vers le venv), jamais un fichier homonyme de l'utilisateur
    if [ -L "$BIN/$cmd" ] && [ "$(readlink "$BIN/$cmd")" = "$VENV/bin/$cmd" ]; then
      rm -f "$BIN/$cmd"
    fi
  done
  if [ -d "$VENV" ]; then
    rm -rf "$VENV"
    rmdir "$(dirname "$VENV")" 2>/dev/null || true
    info "venv supprimé"
  fi
  cat <<EOF

Désinstallation terminée. Conservés (à supprimer à la main si besoin) :
  - $CONF_DIR (contient le mot de passe MQTT)
  - ${CLAUDE_DASH_DIR:-$HOME/.claude/dashboard} (état des sessions)
  - l'appel à claude-dash-statusline dans ton script de statusline
  - les sauvegardes $SETTINGS.bak-claude-dash-*
EOF
}

case "${1:-}" in
  "") do_install ;;
  --hooks-only) merge_hooks ;;
  --uninstall) do_uninstall ;;
  -h | --help) usage ;;
  *) usage >&2; exit 2 ;;
esac
