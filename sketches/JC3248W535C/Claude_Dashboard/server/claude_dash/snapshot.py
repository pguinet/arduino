"""Construit le snapshot publié sur MQTT à partir du store."""

from __future__ import annotations

import math
import unicodedata
from collections.abc import Callable
from typing import Any, TypeGuard

STATES = ("working", "idle", "permission")
MAX_SESSIONS = 12
NO_PID_TTL = 12 * 3600
LIMIT_KEYS = ("h5", "h5_reset", "d7", "d7_reset")
PCT_KEYS = ("h5", "d7")
PID_MAX = 4_194_304  # borne haute de /proc/sys/kernel/pid_max sous Linux 64 bits
HOST_MAX = 32
ID_MAX = 8
TIME_MAX = 2**32  # horodatages epoch publiés : uint32 côté firmware
TIME_KEYS = ("h5_reset", "d7_reset")


def _as_int(value: Any) -> int:
    """Entier défensif : 0 pour toute valeur non numérique (fichier édité à la main...)."""
    if isinstance(value, bool):
        return 0
    if isinstance(value, int):
        return value
    if isinstance(value, float) and math.isfinite(value):
        return int(value)
    return 0


def _as_time(value: Any) -> int:
    """Horodatage epoch borné à 0 <= v < 2**32, sinon 0 (évite un payload gonflé)."""
    t = _as_int(value)
    return t if 0 <= t < TIME_MAX else 0


def _clamp_pct(value: int) -> int:
    return max(0, min(100, value))


def _text(value: Any, n: int, default: str = "") -> str:
    """Texte ASCII imprimable (la fonte du firmware est ASCII), tronqué à n caractères.

    Évite aussi les échappements \\uXXXX de json.dumps qui gonfleraient le payload.
    """
    if not isinstance(value, str):
        return default
    folded = unicodedata.normalize("NFKD", value).encode("ascii", "ignore").decode("ascii")
    text = "".join(c for c in folded if c.isprintable()).strip()[:n].rstrip()
    return text or default


def _is_pid(value: Any) -> TypeGuard[int]:
    return isinstance(value, int) and not isinstance(value, bool) and 0 < value <= PID_MAX


def _is_dead(sess: dict[str, Any], now: int, pid_alive: Callable[[int], bool]) -> bool:
    pid = sess.get("pid")
    if _is_pid(pid):
        return not pid_alive(pid)
    return now - _as_int(sess.get("updated")) > NO_PID_TTL


def _public(sess: dict[str, Any]) -> dict[str, Any]:
    """Ne garde que les champs publiables : jamais de pid, chemin ou autre."""
    state = sess.get("state")
    out: dict[str, Any] = {
        "id": _text(sess["id"], ID_MAX, "?"),
        "project": _text(sess.get("project"), 32, "?"),
        "model": _text(sess.get("model"), 24),
        "state": state if state in STATES else "idle",
        "since": _as_time(sess.get("since")),
    }
    ctx = sess.get("ctx")
    if isinstance(ctx, int) and not isinstance(ctx, bool):
        out["ctx"] = _clamp_pct(ctx)
    tool = _text(sess.get("tool"), 24)
    if tool:
        out["tool"] = tool
    return out


def _public_limits(limits: Any) -> dict[str, int]:
    """Entiers uniquement ; pourcentages bornés à 0..100."""
    if not isinstance(limits, dict):
        return {}
    out: dict[str, int] = {}
    for key in LIMIT_KEYS:
        value = limits.get(key)
        if isinstance(value, int) and not isinstance(value, bool):
            out[key] = _clamp_pct(value) if key in PCT_KEYS else _as_time(value)
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
        if not isinstance(sess, dict) or "id" not in sess:
            continue
        (dead if _is_dead(sess, now, pid_alive) else alive).append(sess)
    alive.sort(key=lambda x: _as_int(x.get("updated")), reverse=True)

    snap: dict[str, Any] = {"host": _text(host, HOST_MAX, "?"), "ts": now}
    pub_limits = _public_limits(limits)
    if pub_limits:
        snap["limits"] = pub_limits
    snap["sessions"] = [_public(x) for x in alive[:MAX_SESSIONS]]
    return snap, [str(x["id"]) for x in dead]
