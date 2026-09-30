#!/usr/bin/env bash
# Lance ruff, mypy et pytest dans un conteneur Python jetable.
# Le conteneur tourne sous l'utilisateur hôte : aucun fichier root dans le dépôt.
set -euo pipefail
cd "$(dirname "$0")"
docker run --rm --user "$(id -u):$(id -g)" \
  -e HOME=/tmp -e PATH=/tmp/.local/bin:/usr/local/bin:/usr/bin:/bin \
  -v "$PWD":/app -w /app python:3.13-slim sh -c '
  pip install -q --user --no-warn-script-location -e ".[dev]" &&
  ruff check . && ruff format --check . && mypy claude_dash && pytest -q "$@"
' -- "$@"
