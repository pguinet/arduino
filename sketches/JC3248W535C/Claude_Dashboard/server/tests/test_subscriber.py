import logging
from dataclasses import dataclass, field

import pytest

from claude_dash.desktop_config import DesktopConfig
from claude_dash.subscriber import (
    Event,
    RemovedEvent,
    SnapshotEvent,
    StatusEvent,
    event_for_message,
    host_from_topic,
    make_callbacks,
)
from claude_dash.view import ConnectionStatus, Phase


def cfg() -> DesktopConfig:
    return DesktopConfig(
        host="broker",
        port=443,
        username=None,
        password=None,
        ca_certs=None,
        tls=True,
        topic_prefix="claude-dash",
        client_id="dev",
        certfile=None,
        keyfile=None,
        transport="websockets",
        corner="top-right",
    )


@dataclass
class FakeReason:
    failure: bool
    text: str

    @property
    def is_failure(self) -> bool:
        return self.failure

    def __str__(self) -> str:
        return self.text


@dataclass
class FakeClient:
    subscribed: list[tuple[str, int]] = field(default_factory=list)

    def subscribe(self, topic: str, qos: int) -> None:
        self.subscribed.append((topic, qos))


@dataclass
class FakeMessage:
    topic: str
    payload: bytes


@pytest.mark.parametrize(
    ("topic", "expected"),
    [
        ("claude-dash/srv/state", "srv"),
        ("claude-dash/srv/other", None),
        ("claude-dash/a/b/state", None),
        ("other/srv/state", None),
        ("claude-dash//state", None),
    ],
)
def test_host_from_topic(topic: str, expected: str | None) -> None:
    assert host_from_topic(topic, "claude-dash") == expected


def test_empty_payload_removes_host() -> None:
    assert event_for_message("claude-dash/srv/state", b"", "claude-dash") == RemovedEvent("srv")


def test_valid_snapshot_event() -> None:
    e = event_for_message(
        "claude-dash/srv/state", b'{"host":"srv","ts":1,"sessions":[]}', "claude-dash"
    )
    assert isinstance(e, SnapshotEvent) and e.snapshot.host == "srv"


def test_message_on_unexpected_topic_dropped() -> None:
    assert event_for_message("claude-dash/srv/x", b'{"host":"srv"}', "claude-dash") is None
    assert event_for_message("claude-dash/srv/x", b"", "claude-dash") is None


@pytest.mark.parametrize("payload", [b"\xff", b"[]", b"null", b'{"host":"other"}'])
def test_bad_payload_or_host_mismatch_dropped(
    payload: bytes, caplog: pytest.LogCaptureFixture
) -> None:
    with caplog.at_level(logging.WARNING):
        assert (
            event_for_message(f"claude-dash/srv-{payload!r}/state", payload, "claude-dash") is None
        )
    assert caplog.records


def test_callbacks_post_status_and_subscribe() -> None:
    posted: list[Event] = []
    cb = make_callbacks(cfg(), posted.append)
    client = FakeClient()
    cb.on_connect(client, None, None, FakeReason(failure=False, text="Success"), None)
    assert client.subscribed == [("claude-dash/+/state", 0)]
    assert posted[-1] == StatusEvent(ConnectionStatus(Phase.CONNECTED))
    cb.on_disconnect(client, None, None, FakeReason(failure=True, text="Session taken over"), None)
    assert posted[-1] == StatusEvent(ConnectionStatus(Phase.DISCONNECTED, "Session taken over"))
    cb.on_connect(client, None, None, FakeReason(failure=True, text="Not authorized"), None)
    assert posted[-1] == StatusEvent(ConnectionStatus(Phase.REFUSED, "Not authorized"))
    assert len(client.subscribed) == 1
    cb.on_connect_fail(client, None)
    last = posted[-1]
    assert isinstance(last, StatusEvent) and last.status.phase is Phase.FAILED


def test_on_message_posts_events() -> None:
    posted: list[Event] = []
    cb = make_callbacks(cfg(), posted.append)
    cb.on_message(None, None, FakeMessage("claude-dash/srv/state", b""))
    assert posted == [RemovedEvent("srv")]


def test_on_message_never_raises_on_garbage() -> None:
    posted: list[Event] = []
    cb = make_callbacks(cfg(), posted.append)
    cb.on_message(None, None, FakeMessage("claude-dash/srv/state", b"\xff"))
    cb.on_message(None, None, object())  # not a message at all
    assert posted == []


def test_refusal_not_overwritten_by_the_disconnect_that_follows() -> None:
    # paho reports a refused CONNACK, then on_disconnect("Unspecified error").
    posted: list[Event] = []
    cb = make_callbacks(cfg(), posted.append)
    client = FakeClient()
    cb.on_connect(client, None, None, FakeReason(failure=True, text="Not authorized"), None)
    cb.on_disconnect(client, None, None, FakeReason(failure=True, text="Unspecified error"), None)
    assert posted == [StatusEvent(ConnectionStatus(Phase.REFUSED, "Not authorized"))]
    cb.on_connect(client, None, None, FakeReason(failure=False, text="Success"), None)
    cb.on_disconnect(client, None, None, FakeReason(failure=True, text="Keep alive timeout"), None)
    assert posted[-1] == StatusEvent(ConnectionStatus(Phase.DISCONNECTED, "Keep alive timeout"))
