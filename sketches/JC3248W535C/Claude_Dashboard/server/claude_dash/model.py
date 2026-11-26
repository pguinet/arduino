"""Dashboard model of the desktop client: Python port of the firmware's
lib/dash_model (parsing, multi-host aggregation, sorting, staleness, alerts).

Keep both implementations aligned: tests/test_model_*.py mirror the firmware's
native tests.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from enum import Enum, IntEnum
from typing import Any

STALE_AFTER_S = 180
# A host silent for longer (crashed agent, sleeping server) is forgotten.
FORGET_AFTER_S = 3600
# Beyond this many days (since = 0, clock not synchronised) durations read "999j+".
MAX_DAYS = 999

COLOR_BG = "#0f1419"
COLOR_CARD = "#1c2733"
COLOR_TRACK = "#2a3a4a"
COLOR_TEXT = "#ffffff"
COLOR_DIM = "#888888"
COLOR_WORKING = "#4cc9f0"
COLOR_IDLE = "#fca311"
COLOR_PERMISSION = "#f72585"
COLOR_QUESTION = "#b388ff"
COLOR_OK = "#4caf50"


class State(Enum):
    WORKING = "working"
    IDLE = "idle"
    PERMISSION = "permission"
    QUESTION = "question"


class Alert(IntEnum):
    """Order = priority: a higher alert replaces a pending one."""

    NONE = 0
    IDLE = 1
    QUESTION = 2
    PERMISSION = 3


@dataclass(frozen=True)
class Limits:
    h5: int = -1
    h5_reset: int = 0
    d7: int = -1
    d7_reset: int = 0
    updated: int = 0


@dataclass(frozen=True)
class Session:
    id: str
    project: str
    model: str
    tool: str
    state: State
    since: int
    ctx: int


@dataclass(frozen=True)
class HostSnapshot:
    host: str
    ts: int
    limits: Limits
    sessions: tuple[Session, ...]


def _is_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _percent(value: Any) -> int:
    if not _is_int(value) or value < 0:
        return -1
    return min(int(value), 100)


def _epoch(value: Any) -> int:
    return int(value) if _is_int(value) and value >= 0 else 0


def _text(value: Any) -> str:
    return value if isinstance(value, str) else ""


def _state(value: Any) -> State:
    for state in (State.WORKING, State.PERMISSION, State.QUESTION):
        if value == state.value:
            return state
    return State.IDLE


def _limits(raw: Any) -> Limits:
    if not isinstance(raw, dict):
        return Limits()
    return Limits(
        h5=_percent(raw.get("h5")),
        h5_reset=_epoch(raw.get("h5_reset")),
        d7=_percent(raw.get("d7")),
        d7_reset=_epoch(raw.get("d7_reset")),
        updated=_epoch(raw.get("updated")),
    )


def _session(raw: Any) -> Session | None:
    if not isinstance(raw, dict):
        return None
    session_id = raw.get("id")
    if not isinstance(session_id, str) or not session_id:
        return None
    return Session(
        id=session_id,
        project=_text(raw.get("project")),
        model=_text(raw.get("model")),
        tool=_text(raw.get("tool")),
        state=_state(raw.get("state")),
        since=_epoch(raw.get("since")),
        ctx=_percent(raw.get("ctx")),
    )


def parse_snapshot(payload: bytes | str) -> HostSnapshot | None:
    """Snapshot published by the agent, or None (invalid JSON, missing host)."""
    try:
        doc = json.loads(payload)
    except (ValueError, UnicodeDecodeError):
        return None
    if not isinstance(doc, dict):
        return None
    host = doc.get("host")
    if not isinstance(host, str) or not host:
        return None
    raw_sessions = doc.get("sessions")
    entries = raw_sessions if isinstance(raw_sessions, list) else []
    sessions = tuple(s for s in map(_session, entries) if s is not None)
    return HostSnapshot(
        host=host, ts=_epoch(doc.get("ts")), limits=_limits(doc.get("limits")), sessions=sessions
    )


_URGENCY = {State.PERMISSION: 0, State.QUESTION: 1, State.IDLE: 2, State.WORKING: 3}
_ALERTS = {
    State.PERMISSION: Alert.PERMISSION,
    State.QUESTION: Alert.QUESTION,
    State.IDLE: Alert.IDLE,
}


@dataclass(frozen=True)
class Row:
    session: Session
    host: str


@dataclass(frozen=True)
class ApplyResult:
    alert: Alert
    changed: bool  # the display must be rebuilt (a heartbeat where only ts moves: False)
    alerted: frozenset[tuple[str, str]]  # (host, session id) of the sessions that alerted


@dataclass
class _HostEntry:
    snap: HostSnapshot
    received_at: int


def _age(entry: _HostEntry, now: int) -> int:
    """From the server's ts when known (an old delayed message is stale on arrival),
    else from reception; clamped at 0 (clock skew)."""
    origin = entry.snap.ts if entry.snap.ts > 0 else entry.received_at
    return max(now - origin, 0)


def _shown_limits(lim: Limits) -> tuple[int, int, int, int]:
    # updated only picks the host, it is never displayed
    return (lim.h5, lim.h5_reset, lim.d7, lim.d7_reset)


def _same_content(a: HostSnapshot, b: HostSnapshot) -> bool:
    return a.sessions == b.sessions and _shown_limits(a.limits) == _shown_limits(b.limits)


def _window_valid(pct: int, reset_at: int, now: int) -> bool:
    return pct >= 0 and (reset_at <= 0 or reset_at > now)


class Dashboard:
    """Hosts in arrival order, each with its latest snapshot."""

    def __init__(self) -> None:
        self._hosts: list[_HostEntry] = []

    def _find(self, host: str) -> _HostEntry | None:
        return next((e for e in self._hosts if e.snap.host == host), None)

    def apply(self, snap: HostSnapshot, now: int) -> ApplyResult:
        entry = self._find(snap.host)
        alerted: set[tuple[str, str]] = set()
        alert = Alert.NONE
        # Previous snapshot already stale: its states were unreliable, no burst of
        # alerts when the server comes back.
        if entry is not None and _age(entry, now) <= STALE_AFTER_S:
            previous = {s.id: s.state for s in entry.snap.sessions}
            for cur in snap.sessions:
                old = previous.get(cur.id)
                if old is None or old is cur.state or cur.state not in _ALERTS:
                    continue
                alerted.add((snap.host, cur.id))
                alert = max(alert, _ALERTS[cur.state])
        shown_before = self.limits(now)
        diff = entry is None or not _same_content(entry.snap, snap)
        if entry is None:
            self._hosts.append(_HostEntry(snap, now))
        else:
            entry.snap, entry.received_at = snap, now
        changed = diff or shown_before != self.limits(now)
        return ApplyResult(alert, changed, frozenset(alerted))

    def remove_host(self, host: str) -> bool:
        entry = self._find(host)
        if entry is None:
            return False
        self._hosts.remove(entry)
        return True

    def expire(self, now: int) -> int:
        kept = [e for e in self._hosts if _age(e, now) <= FORGET_AFTER_S]
        removed = len(self._hosts) - len(kept)
        self._hosts = kept
        return removed

    def rows(self) -> list[Row]:
        """By urgency (permission > question > idle > working), then oldest first;
        since = 0 (unknown) leads its group. Stable."""
        rows = [Row(s, e.snap.host) for e in self._hosts for s in e.snap.sessions]
        return sorted(rows, key=lambda r: (_URGENCY[r.session.state], r.session.since))

    def limits(self, now: int) -> Limits:
        """Each window from the host whose quotas are freshest (limits.updated, else
        ts), skipping a window whose reset is past."""
        h5, h5_reset, d7, d7_reset = -1, 0, -1, 0
        best5 = best7 = -1
        for e in self._hosts:
            lim = e.snap.limits
            fresh = lim.updated if lim.updated > 0 else e.snap.ts
            if _window_valid(lim.h5, lim.h5_reset, now) and fresh > best5:
                best5, h5, h5_reset = fresh, lim.h5, lim.h5_reset
            if _window_valid(lim.d7, lim.d7_reset, now) and fresh > best7:
                best7, d7, d7_reset = fresh, lim.d7, lim.d7_reset
        return Limits(h5=h5, h5_reset=h5_reset, d7=d7, d7_reset=d7_reset)

    def host_names(self) -> list[str]:
        return [e.snap.host for e in self._hosts]

    def stale_seconds(self, now: int) -> int:
        return max((_age(e, now) for e in self._hosts), default=0)

    def session_count(self) -> int:
        return sum(len(e.snap.sessions) for e in self._hosts)


def format_duration(seconds: int) -> str:
    """'0:42', '5:10', '1h05', '2j', '999j+'."""
    s = max(seconds, 0)
    if s < 3600:
        return f"{s // 60}:{s % 60:02d}"
    if s < 86400:
        return f"{s // 3600}h{s % 3600 // 60:02d}"
    if s // 86400 <= MAX_DAYS:
        return f"{s // 86400}j"
    return f"{MAX_DAYS}j+"


def color_for_percent(pct: int) -> str:
    """Same thresholds as the terminal statusline: green < 60, orange < 85, pink."""
    if pct < 60:
        return COLOR_OK
    if pct < 85:
        return COLOR_IDLE
    return COLOR_PERMISSION


_LABELS = {
    State.PERMISSION: "PERMISSION",
    State.QUESTION: "QUESTION",
    State.IDLE: "ATTENTE",
    State.WORKING: "TRAVAILLE",
}
_COLORS = {
    State.PERMISSION: COLOR_PERMISSION,
    State.QUESTION: COLOR_QUESTION,
    State.IDLE: COLOR_IDLE,
    State.WORKING: COLOR_WORKING,
}


def state_label(state: State) -> str:
    return _LABELS[state]


def state_color(state: State) -> str:
    return _COLORS[state]
