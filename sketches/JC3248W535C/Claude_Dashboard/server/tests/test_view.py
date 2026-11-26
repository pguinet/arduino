from dataclasses import replace
from datetime import datetime
from zoneinfo import ZoneInfo

import pytest

from claude_dash.model import HostSnapshot, Limits, Session, State, parse_snapshot
from claude_dash.view import (
    ConnectionStatus,
    Controller,
    Phase,
    clock_text,
    duration_text,
    fit_cards,
    initial_geometry,
)

PARIS = ZoneInfo("Europe/Paris")
# Thursday 2026-11-26 10:40 in Paris
NOW = int(datetime(2026, 11, 26, 10, 40, tzinfo=PARIS).timestamp())


def at(day: int, hour: int, minute: int) -> int:
    return int(datetime(2026, 11, day, hour, minute, tzinfo=PARIS).timestamp())


def sess(sid: str, state: State, since: int = 0, **fields: object) -> Session:
    base = Session(id=sid, project="", model="", tool="", state=state, since=since, ctx=-1)
    return replace(base, **fields)  # type: ignore[arg-type]


def snap(
    host: str, sessions: list[Session], ts: int = 0, limits: Limits | None = None
) -> HostSnapshot:
    return HostSnapshot(host=host, ts=ts, limits=limits or Limits(), sessions=tuple(sessions))


def connected_controller() -> Controller:
    c = Controller("iot.fr-par.scw.cloud:443")
    c.on_status(ConnectionStatus(Phase.CONNECTED))
    return c


def test_waiting_screen_shows_connection_status() -> None:
    c = Controller("iot.fr-par.scw.cloud:443")
    s = c.screen(NOW, PARIS)
    assert s.message == "MQTT : connexion à iot.fr-par.scw.cloud:443…"
    assert not s.mqtt_ok
    c.on_status(ConnectionStatus(Phase.CONNECTED))
    s = c.screen(NOW, PARIS)
    assert s.mqtt_ok and s.message == "MQTT : connecté, en attente d'un serveur (20 s max)"
    c.on_status(ConnectionStatus(Phase.DISCONNECTED, "session taken over"))
    assert c.screen(NOW, PARIS).message == "MQTT : déconnecté (session taken over), reconnexion…"
    c.on_status(ConnectionStatus(Phase.FAILED, "TLS"))
    assert c.screen(NOW, PARIS).message == "MQTT : échec de connexion (TLS), nouvel essai"
    c.on_status(ConnectionStatus(Phase.REFUSED, "Not authorized"))
    assert c.screen(NOW, PARIS).message == "MQTT : refusé par le broker (Not authorized)"


def test_no_session_message_once_a_host_is_known() -> None:
    c = connected_controller()
    c.on_snapshot(snap("srv", []), NOW)
    s = c.screen(NOW, PARIS)
    assert s.message == "Aucune session active"
    assert s.cards == ()


def test_message_empty_when_cards_shown() -> None:
    c = connected_controller()
    c.on_snapshot(snap("srv", [sess("a", State.IDLE)]), NOW)
    assert c.screen(NOW, PARIS).message == ""


def test_single_host_label_and_titles_without_host() -> None:
    c = connected_controller()
    c.on_snapshot(
        snap("srv", [sess("a", State.PERMISSION, project="arduino", model="Opus 5.5")]), NOW
    )
    s = c.screen(NOW, PARIS)
    assert s.host_label == "srv"
    card = s.cards[0]
    assert (card.key, card.title, card.model) == (("srv", "a"), "arduino", "Opus 5.5")
    assert (card.state_text, card.state_color) == ("PERMISSION", "#f72585")


def test_multi_host_titles_with_host_and_empty_label() -> None:
    c = connected_controller()
    c.on_snapshot(snap("srv", [sess("a", State.IDLE, project="arduino")]), NOW)
    c.on_snapshot(snap("pc", [sess("b", State.WORKING, project="api")]), NOW)
    s = c.screen(NOW, PARIS)
    assert s.host_label == ""
    assert [card.title for card in s.cards] == ["arduino@srv", "api@pc"]


def test_cards_sorted_by_urgency() -> None:
    c = connected_controller()
    c.on_snapshot(snap("srv", [sess("w", State.WORKING), sess("p", State.PERMISSION)]), NOW)
    assert [card.key[1] for card in c.screen(NOW, PARIS).cards] == ["p", "w"]


def test_tool_hidden_when_idle() -> None:
    c = connected_controller()
    c.on_snapshot(
        snap("srv", [sess("a", State.IDLE, tool="Bash"), sess("b", State.WORKING, tool="Edit")]),
        NOW,
    )
    tools = {card.key[1]: card.tool for card in c.screen(NOW, PARIS).cards}
    assert tools == {"a": "", "b": "Edit"}


def test_context_gauge() -> None:
    c = connected_controller()
    c.on_snapshot(snap("srv", [sess("a", State.WORKING, ctx=71)]), NOW)
    card = c.screen(NOW, PARIS).cards[0]
    assert (card.ctx, card.ctx_text, card.ctx_color) == (71, "71%", "#fca311")


def test_quota_texts_and_colors() -> None:
    c = connected_controller()
    limits = Limits(h5=42, h5_reset=at(26, 17, 0), d7=90, d7_reset=at(30, 9, 0), updated=NOW)
    c.on_snapshot(snap("srv", [], ts=NOW, limits=limits), NOW)
    q5, q7 = c.screen(NOW, PARIS).quotas
    assert (q5.label, q5.pct, q5.pct_text, q5.color, q5.reset_text) == (
        "5h",
        42,
        "42%",
        "#4caf50",
        "reset 17:00",
    )
    assert (q7.label, q7.pct, q7.pct_text, q7.color, q7.reset_text) == (
        "7j",
        90,
        "90%",
        "#f72585",
        "reset lun 09:00",
    )


def test_unknown_quota_and_context() -> None:
    c = connected_controller()
    c.on_snapshot(snap("srv", [sess("a", State.IDLE)]), NOW)
    s = c.screen(NOW, PARIS)
    for q in s.quotas:
        assert (q.pct, q.pct_text, q.reset_text) == (-1, "--", "")
    assert s.cards[0].ctx_text == "--"


def test_quota_without_reset_has_no_reset_text() -> None:
    c = connected_controller()
    c.on_snapshot(snap("srv", [], limits=Limits(h5=10)), NOW)
    assert c.screen(NOW, PARIS).quotas[0].reset_text == ""


def test_stale_banner_and_dimmed() -> None:
    c = connected_controller()
    c.on_snapshot(snap("srv", [sess("a", State.IDLE)], ts=NOW), NOW)
    s = c.screen(NOW + 181 + 5 * 60, PARIS)
    assert s.banner == "Serveur injoignable depuis 8 min" and s.dimmed
    fresh = c.screen(NOW + 180, PARIS)
    assert fresh.banner == "" and not fresh.dimmed


def test_reaction_bells_and_flash() -> None:
    c = connected_controller()
    c.on_snapshot(snap("srv", [sess("a", State.WORKING), sess("b", State.WORKING)]), NOW)
    r = c.on_snapshot(snap("srv", [sess("a", State.QUESTION), sess("b", State.WORKING)]), NOW + 1)
    assert (r.rebuild, r.bells, r.flash) == (True, 2, frozenset({("srv", "a")}))
    r = c.on_snapshot(snap("srv", [sess("a", State.PERMISSION), sess("b", State.WORKING)]), NOW + 2)
    assert r.bells == 2
    r = c.on_snapshot(snap("srv", [sess("a", State.IDLE), sess("b", State.WORKING)]), NOW + 3)
    assert r.bells == 1
    r = c.on_snapshot(
        snap("srv", [sess("a", State.IDLE), sess("b", State.WORKING)], ts=NOW + 4), NOW + 4
    )
    assert (r.rebuild, r.bells, r.flash) == (False, 0, frozenset())


def test_removed_and_expired_hosts_rebuild() -> None:
    c = connected_controller()
    c.on_snapshot(snap("srv", [], ts=NOW), NOW)
    assert not c.on_removed("zzz").rebuild
    assert c.on_removed("srv").rebuild
    c.on_snapshot(snap("srv", [], ts=NOW), NOW)
    assert not c.expire(NOW + 3600).rebuild
    assert c.expire(NOW + 3601).rebuild
    assert c.screen(NOW + 3601, PARIS).message.startswith("MQTT : connecté")


def test_status_rebuild_only_on_change() -> None:
    c = Controller("h:1")
    assert c.on_status(ConnectionStatus(Phase.CONNECTED)).rebuild
    assert not c.on_status(ConnectionStatus(Phase.CONNECTED)).rebuild
    assert c.on_status(ConnectionStatus(Phase.DISCONNECTED, "x")).rebuild


def test_duration_and_clock_texts() -> None:
    assert duration_text(0, NOW) == ""
    assert duration_text(NOW - 42, NOW) == "0:42"
    assert duration_text(NOW + 10, NOW) == "0:00"
    assert clock_text(NOW, PARIS) == "10:40"


@pytest.mark.parametrize(
    ("count", "available", "expected"),
    [(3, 300, 3), (10, 300, 4), (10, 299, 3), (0, 300, 0), (5, 10, 0)],
)
def test_fit_cards(count: int, available: int, expected: int) -> None:
    assert fit_cards(count, available, card_height=70, more_height=20) == expected


def test_initial_geometry() -> None:
    assert initial_geometry("top-right", 3840, 1930) == "480x320+3340+20"
    assert initial_geometry("top-left", 3840, 1930) == "480x320+20+20"
    assert initial_geometry("bottom-left", 3840, 1930) == "480x320+20+1590"
    assert initial_geometry("bottom-right", 3840, 1930) == "480x320+3340+1590"


def test_screen_survives_hostile_snapshot() -> None:
    snap_ = parse_snapshot(
        '{"host":"srv","ts":1,"limits":{"h5":10,"h5_reset":10000000000000,'
        '"d7":5,"d7_reset":100000000000000000000},'
        '"sessions":[{"id":"a","project":"\\udce9","state":"idle"}]}'
    )
    assert snap_ is not None
    c = connected_controller()
    c.on_snapshot(snap_, NOW)
    s = c.screen(NOW, PARIS)
    assert s.cards[0].title == "�"
    assert s.quotas[0].reset_text == ""
