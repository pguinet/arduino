"""Construit le snapshot publié sur MQTT à partir du store."""

from __future__ import annotations

from collections.abc import Callable
from typing import Any

STATES = ("working", "idle", "permission")
MAX_SESSIONS = 12
NO_PID_TTL = 12 * 3600
LIMIT_KEYS = ("h5", "h5_reset", "d7", "d7_reset")


def _as_int(value: Any) -> int:
    """Entier défensif : 0 pour toute valeur non numérique (fichier édité à la main...)."""
    if isinstance(value, bool):
        return 0
    if isinstance(value, int | float):
        return int(value)
    return 0


def _is_dead(sess: dict[str, Any], now: int, pid_alive: Callable[[int], bool]) -> bool:
    pid = sess.get("pid")
    if isinstance(pid, int) and not isinstance(pid, bool):
        return not pid_alive(pid)
    return now - _as_int(sess.get("updated")) > NO_PID_TTL


def _public(sess: dict[str, Any]) -> dict[str, Any]:
    """Ne garde que les champs publiables : jamais de pid, chemin ou autre."""
    state = sess.get("state")
    out: dict[str, Any] = {
        "id": str(sess["id"])[:8],
        "project": str(sess.get("project", "?"))[:32],
        "model": str(sess.get("model", ""))[:24],
        "state": state if state in STATES else "idle",
        "since": _as_int(sess.get("since")),
    }
    ctx = sess.get("ctx")
    if isinstance(ctx, int) and not isinstance(ctx, bool):
        out["ctx"] = ctx
    if sess.get("tool"):
        out["tool"] = str(sess["tool"])[:24]
    return out


def build_snapshot(
    host: str,
    sessions: list[dict[str, Any]],
    limits: dict[str, Any],
    now: int,
    pid_alive: Callable[[int], bool],
) -> tuple[dict[str, Any], list[str]]:
    """Retourne (snapshot publiable, ids des sessions mortes à purger)."""
    alive: list[dict[str, Any]] = []
    dead: list[dict[str, Any]] = []
    for sess in sessions:
        if "id" not in sess:
            continue
        (dead if _is_dead(sess, now, pid_alive) else alive).append(sess)
    alive.sort(key=lambda x: _as_int(x.get("updated")), reverse=True)

    snap: dict[str, Any] = {"host": host, "ts": now}
    pub_limits = {k: limits[k] for k in LIMIT_KEYS if k in limits}
    if pub_limits:
        snap["limits"] = pub_limits
    snap["sessions"] = [_public(x) for x in alive[:MAX_SESSIONS]]
    return snap, [str(x["id"]) for x in dead]
