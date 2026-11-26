"""MQTT subscriber of the desktop client: turns hub messages into events posted
from paho's network thread to the window's thread."""

from __future__ import annotations

import logging
import time
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion

from claude_dash.desktop_config import DesktopConfig
from claude_dash.model import HostSnapshot, parse_snapshot
from claude_dash.view import ConnectionStatus, Phase

log = logging.getLogger("claude-dash-desktop")

# Same as the firmware (HOST_MISMATCH_REPEAT_MS): one warning per host and reason.
WARN_REPEAT_S = 600.0
_last_warning: dict[tuple[str, str], float] = {}


@dataclass(frozen=True)
class SnapshotEvent:
    snapshot: HostSnapshot


@dataclass(frozen=True)
class RemovedEvent:
    host: str


@dataclass(frozen=True)
class StatusEvent:
    status: ConnectionStatus


Event = SnapshotEvent | RemovedEvent | StatusEvent
Post = Callable[[Event], None]


def _warn(host: str, reason: str) -> None:
    now = time.monotonic()
    key = (host, reason)
    last = _last_warning.get(key)
    if last is not None and now - last < WARN_REPEAT_S:
        return
    _last_warning[key] = now
    log.warning("message de %s ignoré : %s", host, reason)


def host_from_topic(topic: str, prefix: str) -> str | None:
    """'<prefix>/<host>/state' exactly -> host."""
    parts = topic.split("/")
    if len(parts) != 3 or parts[0] != prefix or parts[2] != "state" or not parts[1]:
        return None
    return parts[1]


def event_for_message(topic: str, payload: bytes, prefix: str) -> Event | None:
    host = host_from_topic(topic, prefix)
    if host is None:
        log.debug("topic inattendu ignoré : %s", topic)
        return None
    if not payload:
        return RemovedEvent(host)
    snap = parse_snapshot(payload)
    if snap is None:
        _warn(host, "snapshot invalide")
        return None
    # Hub filters bind each server to its own topic: a different host in the
    # payload is a misconfiguration, never trusted.
    if snap.host != host:
        _warn(host, f'host "{snap.host}" différent du topic')
        return None
    return SnapshotEvent(snap)


@dataclass(frozen=True)
class Callbacks:
    on_connect: Callable[..., None]
    on_connect_fail: Callable[..., None]
    on_disconnect: Callable[..., None]
    on_message: Callable[..., None]


def make_callbacks(cfg: DesktopConfig, post: Post) -> Callbacks:
    """paho VERSION2 callbacks; they run in paho's thread and only post events."""
    # paho follows a refused CONNACK with on_disconnect("Unspecified error"): keep
    # the refusal on screen, it names the real cause.
    refused = False

    def on_connect(client: Any, userdata: Any, flags: Any, reason_code: Any, props: Any) -> None:
        nonlocal refused
        refused = reason_code.is_failure
        if reason_code.is_failure:
            log.error("MQTT refusé (%s)", reason_code)
            post(StatusEvent(ConnectionStatus(Phase.REFUSED, str(reason_code))))
            return
        # Subscriptions are lost on reconnect: subscribing here covers every reconnect.
        client.subscribe(cfg.topic, 0)
        log.info("MQTT connecté, abonné à %s", cfg.topic)
        post(StatusEvent(ConnectionStatus(Phase.CONNECTED)))

    def on_connect_fail(client: Any, userdata: Any) -> None:
        log.warning("connexion MQTT impossible à %s:%s, nouvel essai", cfg.host, cfg.port)
        post(StatusEvent(ConnectionStatus(Phase.FAILED, "réseau, TLS ou certificat")))

    def on_disconnect(client: Any, userdata: Any, flags: Any, reason_code: Any, props: Any) -> None:
        log.warning("MQTT déconnecté (%s)", reason_code)
        if not refused:
            post(StatusEvent(ConnectionStatus(Phase.DISCONNECTED, str(reason_code))))

    def on_message(client: Any, userdata: Any, message: Any) -> None:
        try:
            event = event_for_message(message.topic, message.payload, cfg.topic_prefix)
        except Exception:  # noqa: BLE001 - never let a bad message kill paho's thread
            log.exception("message MQTT inexploitable")
            return
        if event is not None:
            post(event)

    return Callbacks(on_connect, on_connect_fail, on_disconnect, on_message)


def start_client(cfg: DesktopConfig, post: Post) -> mqtt.Client:
    post(StatusEvent(ConnectionStatus(Phase.CONNECTING)))
    client = mqtt.Client(
        CallbackAPIVersion.VERSION2, client_id=cfg.client_id, transport=cfg.transport
    )
    if cfg.tls:
        client.tls_set(ca_certs=cfg.ca_certs, certfile=cfg.certfile, keyfile=cfg.keyfile)
    if cfg.username:
        client.username_pw_set(cfg.username, cfg.password)
    client.reconnect_delay_set(min_delay=1, max_delay=60)
    callbacks = make_callbacks(cfg, post)
    client.on_connect = callbacks.on_connect
    client.on_connect_fail = callbacks.on_connect_fail
    client.on_disconnect = callbacks.on_disconnect
    client.on_message = callbacks.on_message
    client.connect_async(cfg.host, cfg.port, keepalive=60)
    client.loop_start()
    return client


def stop_client(client: mqtt.Client) -> None:
    client.disconnect()
    client.loop_stop()
