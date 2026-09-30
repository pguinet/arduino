# claude-dash — agent serveur

Agent Python qui alimente le tableau de bord Claude Code du JC3248W535C. Il tourne
sur le serveur Linux où tu utilises Claude Code et publie l'état de tes sessions
sur un broker MQTT (TLS) que l'écran écoute.

```
Serveur Linux distant                                   Cloud            Maison
┌──────────────────────────────────────────┐
│ Claude Code (N sessions)                 │
│  ├─ hooks ──────┐                        │
│  └─ statusline ─┤ écrivent               │
│                 ▼                        │
│  ~/.claude/dashboard/sessions/<id>.json  │
│                 │ scruté chaque seconde  │
│                 ▼                        │     MQTT/TLS       MQTT/TLS
│  claude-dash-agent (systemd --user) ─────┼──► broker ────► JC3248 (LVGL)
└──────────────────────────────────────────┘    8883
```

- **`claude-dash-hook`** : branché sur les hooks `SessionStart`, `UserPromptSubmit`,
  `PreToolUse`, `PostToolUse`, `Notification`, `Stop` et `SessionEnd` ; met à jour
  l'état de la session (`working`, `permission`, `idle`).
- **`claude-dash-statusline`** : appelé par ta statusline, récupère modèle, contexte
  et quotas.
- **`claude-dash-agent`** : démon qui agrège les sessions, purge celles dont le
  processus `claude` a disparu et publie un snapshot retained sur
  `claude-dash/<hostname>/state` (à chaque changement, au plus toutes les 2 s, et
  au moins toutes les 60 s).

## Prérequis

- Python ≥ 3.11 avec le module `venv` (Debian/Ubuntu : `apt install python3-venv`) ;
- `jq` ;
- `systemd --user` (pour le service) ;
- un compte sur un broker MQTT joignable en TLS (HiveMQ Cloud, EMQX…).

Aucun `sudo` n'est nécessaire.

## Installation

```bash
git clone <url du dépôt> <dépôt>
cd <dépôt>/sketches/JC3248W535C/Claude_Dashboard/server
./install.sh
```

Le script est idempotent : relance-le après un `git pull` pour mettre à jour. Il :

1. crée un venv dans `~/.local/share/claude-dash/venv` et y installe le paquet ;
2. lie les trois commandes dans `~/.local/bin/` ;
3. crée `~/.config/claude-dash/config.toml` (chmod 600) depuis
   `config.toml.example` s'il n'existe pas (jamais écrasé ensuite) ;
4. ajoute les hooks dans `~/.claude/settings.json` (voir ci-dessous) ;
5. installe `~/.config/systemd/user/claude-dash-agent.service`, l'active et le
   (re)démarre si la config est renseignée.

Sans `systemd --user` utilisable (conteneur, session sans bus utilisateur), le
service est copié mais pas activé : le script le signale et continue.

Les chemins `~/.config/claude-dash/` et `~/.config/systemd/user/` sont fixes :
`$XDG_CONFIG_HOME` est ignoré par le script, car l'agent lancé par `systemd --user`
lit toujours `~/.config/claude-dash/config.toml`. Si ton shell définit
`XDG_CONFIG_HOME`, un `claude-dash-agent` lancé à la main y cherchera sa config :
passe par le service.

Les sessions Claude Code déjà ouvertes ne voient pas les nouveaux hooks :
redémarre-les (ou vérifie avec `/hooks`).

Ensuite :

```bash
nano ~/.config/claude-dash/config.toml      # broker, compte, mot de passe
systemctl --user restart claude-dash-agent
loginctl enable-linger "$USER"              # l'agent tourne sans session SSH ouverte
journalctl --user -u claude-dash-agent -f   # vérifier "MQTT connecté"
```

### Hooks Claude Code

`install.sh` fusionne ses hooks dans `settings.json` avec `jq` sans toucher au reste
(autres hooks, `statusLine`, permissions, plugins…) :

- une entrée par événement, sans `matcher` (= tous les outils pour
  `PreToolUse`/`PostToolUse`) :
  `{"hooks": [{"type": "command", "command": "/home/<toi>/.local/bin/claude-dash-hook", "timeout": 5}]}`
  (chemin absolu) ;
- les anciennes entrées `claude-dash-hook` (quel que soit leur chemin) sont
  remplacées : pas de doublon au second passage ;
- le fichier n'est réécrit que s'il change, après une sauvegarde
  `settings.json.bak-claude-dash-AAAAMMJJ-HHMMSS-<ns>-<pid>` ; l'écriture est atomique
  (fichier temporaire à côté de la cible puis `mv`), un lien symbolique est suivi
  (dotfiles) et les droits sont conservés ;
- `jq` reformate le fichier (indentation de 2 espaces) quand il le réécrit ;
- si `settings.json` n'est pas un JSON valide (ou si une entrée de hook n'est pas un
  objet), le script s'arrête sans rien modifier et affiche l'erreur ; s'il est
  modifié pendant l'opération (Claude Code qui l'enregistre), rien n'est écrit :
  relance le script.

`./install.sh --hooks-only` n'effectue que cette fusion. `CLAUDE_CONFIG_DIR` est
respecté si tu l'utilises.

### Statusline

Ajoute à ton script de statusline, juste après la lecture de stdin, un appel **au
premier plan** (surtout pas de `&`) :

```bash
input=$(cat)
printf '%s' "$input" | timeout 1 ~/.local/bin/claude-dash-statusline >/dev/null 2>&1
```

Lancé en arrière-plan, le collecteur serait réadopté par `systemd --user` et ne
retrouverait presque jamais le PID de Claude. Le `timeout 1` borne son coût.

Sans statusline, en voici une minimale (`~/.claude/statusline.sh`, `chmod +x`) :

```bash
#!/usr/bin/env bash
input=$(cat)
printf '%s' "$input" | timeout 1 ~/.local/bin/claude-dash-statusline >/dev/null 2>&1
echo "$input" | jq -r '.model.display_name'
```

à déclarer dans `settings.json` :

```json
"statusLine": { "type": "command", "command": "~/.claude/statusline.sh" }
```

## Configuration

`~/.config/claude-dash/config.toml` (voir `config.toml.example`) :

```toml
[mqtt]
host = "votre_host"
port = 8883
username = "votre_user"
password = "votre_mot_de_passe"
tls = true
# ca_certs = "/etc/ssl/certs/ca-certificates.crt"   # défaut : CA système
topic_prefix = "claude-dash"

[agent]
# hostname = "srv-dev"
```

- Le fichier contient un mot de passe : il doit rester en **chmod 600**, l'agent
  refuse de démarrer sinon (`install.sh` remet ces droits à chaque passage).
- **Le nom d'hôte doit être unique sur le broker** : il sert au client id
  `claude-dash-<hostname>` et au topic `claude-dash/<hostname>/state`. Deux agents
  avec le même nom se déconnectent mutuellement et écrasent le même topic. Par
  défaut c'est le nom court de la machine ; si deux serveurs partagent le même,
  renseigne `[agent] hostname` sur l'un d'eux (`A-Z a-z 0-9 . _ -`, 32 max).
- `tls = false` n'est prévu que pour un Mosquitto local de test (`dev/`).
- Emplacement fixe : `~/.config/claude-dash/config.toml` (voir « Installation »).
- Une config invalide fait sortir l'agent avec le code 2 : le service n'est alors pas
  relancé en boucle (`RestartPreventExitStatus=2`) ; corrige puis
  `systemctl --user restart claude-dash-agent`.

### Broker : comptes et ACL recommandées

Un compte par rôle, avec les droits minimaux :

| Compte | Droits |
|---|---|
| serveur (`claude-dash-srv`, un par serveur idéalement) | publish sur `claude-dash/<hostname>/#` |
| écran (`claude-dash-screen`) | subscribe sur `claude-dash/#` (lecture seule) |

Ainsi, un mot de passe serveur compromis ne permet pas d'usurper un autre serveur,
et celui stocké dans l'écran ne permet pas de publier de faux états.

## Dépannage

```bash
systemctl --user status claude-dash-agent
journalctl --user -u claude-dash-agent -f
# snapshot publié (retained) :
mosquitto_sub -h <broker> -p 8883 -u <compte écran> -P '<mdp>' \
  --capath /etc/ssl/certs -t 'claude-dash/#' -v -C 1
ls ~/.claude/dashboard/sessions/        # état brut des sessions
```

- `config.toml doit être en chmod 600` : `chmod 600 ~/.config/claude-dash/config.toml`.
- `MQTT refusé (Not authorized)` : identifiants ou ACL du broker.
- Rien dans le journal après un redémarrage de session SSH : `loginctl enable-linger`.
- Session fantôme sur l'écran : elle disparaît dès que le processus `claude` est
  mort (vérifié chaque seconde).

## Désinstallation

```bash
./install.sh --uninstall
```

Retire les entrées `claude-dash-hook` de `settings.json` (sauvegarde préalable, le
reste est conservé), arrête et supprime le service, le venv et les commandes. Sans
`systemd --user` joignable, le service est désactivé (liens supprimés) mais pas
arrêté : le script le signale (`pkill -f claude-dash-agent`). La
config (`~/.config/claude-dash/`, qui contient le mot de passe), l'état des sessions
(`~/.claude/dashboard/`) et l'appel dans ta statusline sont conservés : supprime-les
à la main. Relancer la commande est sans effet.

## Développement

```bash
./run-checks.sh              # ruff, mypy, pytest (Docker)
./dev/integration-test.sh    # chaîne complète avec un Mosquitto local (Docker)
./dev/test-install.sh        # install.sh dans un conteneur jetable (HOME fictif)
CLAUDE_DASH_EXTRA_SETTINGS=~/.claude/settings.json ./dev/test-install.sh
                             # + fusion sur une copie en lecture seule de ton settings.json
```
