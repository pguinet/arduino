"""What the desktop window shows and how it reacts to events, without Tk:
the Tk layer only draws a Screen and plays a Reaction."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, tzinfo
from enum import Enum

from claude_dash.desktop_config import Corner
from claude_dash.model import (
    STALE_AFTER_S,
    Alert,
    Dashboard,
    HostSnapshot,
    Row,
    State,
    color_for_percent,
    format_duration,
    state_color,
    state_label,
)

DAYS = ("lun", "mar", "mer", "jeu", "ven", "sam", "dim")  # datetime.weekday() order
_BELLS = {Alert.PERMISSION: 2, Alert.QUESTION: 2, Alert.IDLE: 1}


class Phase(Enum):
    CONNECTING = "connecting"
    CONNECTED = "connected"
    DISCONNECTED = "disconnected"
    FAILED = "failed"
    REFUSED = "refused"


@dataclass(frozen=True)
class ConnectionStatus:
    phase: Phase
    detail: str = ""


@dataclass(frozen=True)
class Quota:
    label: str
    pct: int
    pct_text: str
    color: str
    reset_text: str


@dataclass(frozen=True)
class Card:
    key: tuple[str, str]
    title: str
    model: str
    state_text: str
    state_color: str
    tool: str
    since: int
    ctx: int
    ctx_text: str
    ctx_color: str


@dataclass(frozen=True)
class Screen:
    host_label: str
    mqtt_ok: bool
    quotas: tuple[Quota, Quota]
    cards: tuple[Card, ...]
    message: str
    banner: str
    dimmed: bool


@dataclass(frozen=True)
class Reaction:
    rebuild: bool
    bells: int
    flash: frozenset[tuple[str, str]]


NOTHING = Reaction(rebuild=False, bells=0, flash=frozenset())
REBUILD = Reaction(rebuild=True, bells=0, flash=frozenset())


def _local(epoch: int, tz: tzinfo | None) -> datetime:
    return datetime.fromtimestamp(epoch, tz) if tz else datetime.fromtimestamp(epoch).astimezone()


def clock_text(now: int, tz: tzinfo | None = None) -> str:
    return _local(now, tz).strftime("%H:%M")


def duration_text(since: int, now: int) -> str:
    return format_duration(now - since) if since > 0 else ""


def _pct_text(pct: int) -> str:
    return f"{pct}%" if pct >= 0 else "--"


def _quota(label: str, pct: int, reset_at: int, with_day: bool, tz: tzinfo | None) -> Quota:
    reset_text = ""
    if pct >= 0 and reset_at > 0:
        when = _local(reset_at, tz)
        day = f"{DAYS[when.weekday()]} " if with_day else ""
        reset_text = f"reset {day}{when:%H:%M}"
    return Quota(label, pct, _pct_text(pct), color_for_percent(pct), reset_text)


def _card(row: Row, show_host: bool) -> Card:
    s = row.session
    return Card(
        key=(row.host, s.id),
        title=f"{s.project}@{row.host}" if show_host else s.project,
        model=s.model,
        state_text=state_label(s.state),
        state_color=state_color(s.state),
        tool="" if s.state is State.IDLE else s.tool,
        since=s.since,
        ctx=s.ctx,
        ctx_text=_pct_text(s.ctx),
        ctx_color=color_for_percent(s.ctx),
    )


def _status_text(status: ConnectionStatus, broker: str) -> str:
    texts = {
        Phase.CONNECTING: f"MQTT : connexion à {broker}…",
        Phase.CONNECTED: "MQTT : connecté, en attente d'un serveur (20 s max)",
        Phase.DISCONNECTED: f"MQTT : déconnecté ({status.detail}), reconnexion…",
        Phase.FAILED: f"MQTT : échec de connexion ({status.detail}), nouvel essai",
        Phase.REFUSED: f"MQTT : refusé par le broker ({status.detail})",
    }
    return texts[status.phase]


class Controller:
    """Feeds the dashboard model with events and builds what the window shows."""

    def __init__(self, broker: str) -> None:
        self._broker = broker
        self._dashboard = Dashboard()
        self._status = ConnectionStatus(Phase.CONNECTING)

    def on_snapshot(self, snap: HostSnapshot, now: int) -> Reaction:
        result = self._dashboard.apply(snap, now)
        return Reaction(result.changed, _BELLS.get(result.alert, 0), result.alerted)

    def on_removed(self, host: str) -> Reaction:
        return REBUILD if self._dashboard.remove_host(host) else NOTHING

    def on_status(self, status: ConnectionStatus) -> Reaction:
        if status == self._status:
            return NOTHING
        self._status = status
        return REBUILD

    def expire(self, now: int) -> Reaction:
        return REBUILD if self._dashboard.expire(now) else NOTHING

    def screen(self, now: int, tz: tzinfo | None = None) -> Screen:
        d = self._dashboard
        hosts = d.host_names()
        lim = d.limits(now)
        cards = tuple(_card(r, len(hosts) > 1) for r in d.rows())
        message = ""
        if not hosts:
            message = _status_text(self._status, self._broker)
        elif not cards:
            message = "Aucune session active"
        stale = d.stale_seconds(now)
        is_stale = stale > STALE_AFTER_S
        return Screen(
            host_label=hosts[0] if len(hosts) == 1 else "",
            mqtt_ok=self._status.phase is Phase.CONNECTED,
            quotas=(
                _quota("5h", lim.h5, lim.h5_reset, False, tz),
                _quota("7j", lim.d7, lim.d7_reset, True, tz),
            ),
            cards=cards,
            message=message,
            banner=f"Serveur injoignable depuis {stale // 60} min" if is_stale else "",
            dimmed=is_stale,
        )


def fit_cards(count: int, available: int, card_height: int, more_height: int) -> int:
    """Cards that fit in `available` pixels; the others collapse into one
    "+N autres sessions" line of `more_height`."""
    if count * card_height <= available:
        return count
    return max((available - more_height) // card_height, 0)


def initial_geometry(
    corner: Corner,
    screen_w: int,
    screen_h: int,
    width: int = 480,
    height: int = 320,
    margin: int = 20,
) -> str:
    x = margin if corner.endswith("left") else screen_w - width - margin
    y = margin if corner.startswith("top") else screen_h - height - margin
    return f"{width}x{height}+{x}+{y}"
