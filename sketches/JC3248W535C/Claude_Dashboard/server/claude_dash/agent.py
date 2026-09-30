"""Démon : agrège les sessions et publie le snapshot sur MQTT."""

from __future__ import annotations

import json
import logging
import signal
import sys
import time
from collections.abc import Callable
from pathlib import Path
from typing import Any, Protocol

import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion, MQTTErrorCode

from claude_dash import store
from claude_dash.config import Config, ConfigError, default_path, load_config
from claude_dash.procs import pid_alive as default_pid_alive
from claude_dash.snapshot import build_snapshot

log = logging.getLogger("claude-dash-agent")


class Ticker(Protocol):
    def tick(self, snapshot: dict[str, Any], now: float) -> bool: ...


class Publisher:
    """Publie si le contenu change (au plus toutes les `min_interval` s) ou en heartbeat."""

    def __init__(
        self, send: Callable[[str], None], min_interval: float = 2, heartbeat: float = 60
    ) -> None:
        self._send = send
        self._min_interval = min_interval
        self._heartbeat = heartbeat
        self._last_body: str | None = None
        self._last_sent = float("-inf")
        self._failing = False

    def tick(self, snapshot: dict[str, Any], now: float) -> bool:
        body = json.dumps({k: v for k, v in snapshot.items() if k != "ts"}, sort_keys=True)
        elapsed = now - self._last_sent
        changed = body != self._last_body
        if not ((changed and elapsed >= self._min_interval) or elapsed >= self._heartbeat):
            return False
        try:
            self._send(json.dumps(snapshot, separators=(",", ":")))
        except Exception as exc:
            # une seule alerte par panne : la boucle réessaie chaque seconde
            log.log(
                logging.DEBUG if self._failing else logging.WARNING, "publication échouée: %s", exc
            )
            self._failing = True
            return False
        if self._failing:
            log.info("publication rétablie")
            self._failing = False
        self._last_body = body
        self._last_sent = now
        return True


def run_once(
    base: Path,
    host: str,
    publisher: Ticker,
    now_epoch: int,
    now_mono: float,
    pid_alive: Callable[[int], bool] = default_pid_alive,
) -> None:
    """Une itération : lecture du store, purge des sessions mortes, publication.

    Ne lève jamais : la boucle du démon doit survivre à tout fichier corrompu.
    """
    try:
        snap, dead = build_snapshot(
            host, store.load_sessions(base), store.load_limits(base), now_epoch, pid_alive
        )
        for sid in dead:
            try:
                store.remove_session(base, sid)
                log.info("purge session morte %s", sid)
            except (OSError, ValueError) as exc:
                log.warning("purge impossible de %r: %s", sid, exc)
        publisher.tick(snap, now_mono)
    except Exception:
        log.exception("itération de l'agent en échec")


def _make_client(cfg: Config) -> mqtt.Client:
    client = mqtt.Client(CallbackAPIVersion.VERSION2, client_id=f"claude-dash-{cfg.hostname}")
    if cfg.tls:
        client.tls_set(ca_certs=cfg.ca_certs)
    if cfg.username:
        client.username_pw_set(cfg.username, cfg.password)
    client.reconnect_delay_set(min_delay=1, max_delay=60)
    client.on_connect = lambda c, u, f, rc, p: log.info("MQTT connecté (%s)", rc)
    client.on_disconnect = lambda c, u, f, rc, p: log.warning("MQTT déconnecté (%s)", rc)
    client.connect_async(cfg.host, cfg.port, keepalive=60)
    client.loop_start()
    return client


def run(cfg: Config) -> None:
    client = _make_client(cfg)
    base = store.default_base()

    def send(payload: str) -> None:
        if not client.is_connected():
            raise OSError("MQTT non connecté")
        info = client.publish(cfg.topic, payload, qos=1, retain=True)
        if info.rc != MQTTErrorCode.MQTT_ERR_SUCCESS:
            raise OSError(f"publish rc={info.rc}")

    publisher = Publisher(send)
    stop = False

    def _stop(*_: object) -> None:
        nonlocal stop
        stop = True

    signal.signal(signal.SIGTERM, _stop)
    signal.signal(signal.SIGINT, _stop)
    log.info("agent démarré, topic %s", cfg.topic)
    while not stop:
        run_once(base, cfg.hostname, publisher, int(time.time()), time.monotonic())
        time.sleep(1)
    client.loop_stop()
    client.disconnect()


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    try:
        cfg = load_config(default_path())
    except ConfigError as exc:
        log.error("%s", exc)
        sys.exit(2)
    run(cfg)
