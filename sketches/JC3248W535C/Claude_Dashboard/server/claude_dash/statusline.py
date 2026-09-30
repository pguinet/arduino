"""Collecte des infos de la statusline Claude Code (quotas, contexte, modèle)."""

from __future__ import annotations

import json
import os
import sys
import time
from datetime import UTC, datetime
from pathlib import PurePosixPath
from typing import Any

from claude_dash import store
from claude_dash.procs import find_claude_pid


def parse_epoch(value: Any) -> int | None:
    """Normalise un epoch (int/float) ou une date ISO 8601 (UTC si sans fuseau)."""
    if isinstance(value, bool):
        return None
    if isinstance(value, int | float):
        return int(value)
    if isinstance(value, str):
        try:
            dt = datetime.fromisoformat(value)
        except ValueError:
            return None
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=UTC)
        return int(dt.timestamp())
    return None


def _pct(value: Any) -> int | None:
    if isinstance(value, int | float) and not isinstance(value, bool):
        return max(0, min(100, round(value)))
    return None


def _basename(path: Any) -> str:
    return PurePosixPath(str(path)).name if path else ""


def extract_limits(payload: dict[str, Any], now: int) -> dict[str, Any] | None:
    rl = payload.get("rate_limits")
    if not isinstance(rl, dict):
        return None
    out: dict[str, Any] = {}
    for src, key in (("five_hour", "h5"), ("seven_day", "d7")):
        window = rl.get(src)
        if not isinstance(window, dict):
            continue
        if (pct := _pct(window.get("used_percentage"))) is not None:
            out[key] = pct
        if (reset := parse_epoch(window.get("resets_at"))) is not None:
            out[f"{key}_reset"] = reset
    if not out:
        return None
    out["updated"] = now
    return out


def update_from_statusline(
    data: dict[str, Any], payload: dict[str, Any], now: int, pid: int | None
) -> None:
    """Même règle de projet que hook.py : `project_dir` prioritaire, sinon le
    répertoire courant uniquement si le projet n'est pas encore connu."""
    model = payload.get("model")
    if isinstance(model, dict) and model.get("display_name"):
        data["model"] = str(model["display_name"])
    ctx = payload.get("context_window")
    if isinstance(ctx, dict) and (pct := _pct(ctx.get("used_percentage"))) is not None:
        data["ctx"] = pct

    ws = payload.get("workspace")
    ws = ws if isinstance(ws, dict) else {}
    project = _basename(ws.get("project_dir"))
    if not project and "project" not in data:
        project = _basename(ws.get("current_dir") or payload.get("cwd"))
    if project:
        data["project"] = project

    if "state" not in data:
        data["state"] = "idle"
        data["since"] = now
    # le hook fait autorité sur le PID ; la statusline ne comble qu'un PID manquant
    if pid is not None and "pid" not in data:
        data["pid"] = pid
    data["updated"] = now


def main() -> None:
    """Lit le JSON de la statusline sur stdin. Silencieux, code retour toujours 0."""
    try:
        payload = json.load(sys.stdin)
        base = store.default_base()
        now = int(time.time())
        if limits := extract_limits(payload, now):
            store.write_limits(base, limits)
        session_id = str(payload["session_id"])
        pid = find_claude_pid(os.getpid())

        def mutate(data: dict[str, Any]) -> None:
            update_from_statusline(data, payload, now, pid)

        # sans PID, pas de nouvelle session (évite les fantômes, ex. après SessionEnd)
        if pid is not None or store.session_exists(base, session_id):
            store.update_session(base, session_id, mutate)
    except Exception as exc:  # la statusline ne doit jamais échouer bruyamment
        print(f"claude-dash-statusline: {exc}", file=sys.stderr)
    sys.exit(0)
