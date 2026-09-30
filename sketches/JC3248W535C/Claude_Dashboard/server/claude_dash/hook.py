"""Hook Claude Code : met à jour l'état de la session dans le store."""

from __future__ import annotations

import json
import os
import sys
import time
from pathlib import PurePosixPath
from typing import Any

from claude_dash import store
from claude_dash.procs import find_claude_pid

WORKING = "working"
IDLE = "idle"
PERMISSION = "permission"


def _set_state(data: dict[str, Any], state: str, now: int) -> None:
    if data.get("state") != state:
        data["state"] = state
        data["since"] = now


def _is_permission(event: dict[str, Any]) -> bool:
    ntype = event.get("notification_type")
    if ntype is not None:
        return bool(ntype == "permission_prompt")
    return "permission" in str(event.get("message", "")).lower()


def _basename(path: str | None) -> str:
    return PurePosixPath(path).name if path else ""


def apply_event(
    data: dict[str, Any],
    event: dict[str, Any],
    now: int,
    pid: int | None,
    project_dir: str | None = None,
) -> bool:
    """Applique un événement de hook. Retourne False si la session doit être supprimée.

    `project_dir` (CLAUDE_PROJECT_DIR) fixe le projet ; à défaut, le cwd n'est utilisé
    qu'au premier événement, pour que le libellé ne suive pas les `cd` de Claude.
    """
    name = event.get("hook_event_name")
    if name == "SessionEnd":
        return False

    project = _basename(project_dir)
    if not project and "project" not in data:
        project = _basename(event.get("cwd"))
    if project:
        data["project"] = project
    if pid is not None:
        data["pid"] = pid

    if name == "SessionStart":
        # après un /compact la session continue : on garde l'état courant
        if event.get("source") != "compact" or "state" not in data:
            _set_state(data, IDLE, now)
    elif name == "UserPromptSubmit":
        _set_state(data, WORKING, now)
        data.pop("tool", None)
    elif name in ("PreToolUse", "PostToolUse"):
        _set_state(data, WORKING, now)
        if tool := event.get("tool_name"):
            data["tool"] = str(tool)
    elif name == "Notification" and event.get("notification_type") == "idle_prompt":
        # seul signal après un Esc sur une demande de permission (pas de Stop)
        _set_state(data, IDLE, now)
        data.pop("tool", None)
    elif name == "Notification" and _is_permission(event):
        _set_state(data, PERMISSION, now)
    elif name == "Stop":
        _set_state(data, IDLE, now)
        data.pop("tool", None)

    data["updated"] = now
    return True


def main() -> None:
    """Point d'entrée : ne doit jamais faire échouer Claude Code."""
    try:
        event = json.load(sys.stdin)
        session_id = str(event["session_id"])
        base = store.default_base()
        if event.get("hook_event_name") == "SessionEnd":
            store.remove_session(base, session_id)
        else:
            pid = find_claude_pid(os.getpid())
            now = int(time.time())
            project_dir = os.environ.get("CLAUDE_PROJECT_DIR")

            def mutate(data: dict[str, Any]) -> None:
                apply_event(data, event, now, pid, project_dir)

            store.update_session(base, session_id, mutate)
    except Exception as exc:  # un hook ne doit jamais bloquer Claude
        print(f"claude-dash-hook: {exc}", file=sys.stderr)
    sys.exit(0)
