#!/usr/bin/env bash
# Lance ruff, mypy et pytest dans un conteneur Python jetable.
set -euo pipefail
cd "$(dirname "$0")"
docker run --rm -v "$PWD":/app -w /app python:3.13-slim sh -c '
  pip install -q --root-user-action=ignore -e ".[dev]" &&
  ruff check . && ruff format --check . && mypy claude_dash && pytest -q "$@"
' -- "$@"
