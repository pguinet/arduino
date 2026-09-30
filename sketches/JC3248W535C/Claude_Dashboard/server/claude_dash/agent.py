"""Démon : agrège les sessions et publie le snapshot sur MQTT."""

from __future__ import annotations

import json
import logging
import signal
import ssl
import sys
import threading
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


class Outage:
    """Journalise une panne persistante une seule fois, puis son rétablissement.

    La boucle réessaie chaque seconde : sans ce verrou, une panne remplit le journal.
    """

    def __init__(self, recovered_msg: str) -> None:
        self._recovered_msg = recovered_msg
        self.active = False

    def failed(
        self, msg: str, *args: object, level: int = logging.WARNING, exc_info: bool = False
    ) -> None:
        log.log(logging.DEBUG if self.active else level, msg, *args, exc_info=exc_info)
        self.active = True

    def recovered(self) -> None:
        if self.active:
            log.info("%s", self._recovered_msg)
            self.active = False


class Publisher:
    """Publie si le contenu change (au plus toutes les `min_interval` s) ou en heartbeat.

    Avant la première publication réussie, les échecs (connexion MQTT pas encore établie)
    restent en DEBUG pendant `startup_grace` s, puis sont signalés normalement.
    """

    def __init__(
        self,
        send: Callable[[str], None],
        min_interval: float = 2,
        heartbeat: float = 60,
        startup_grace: float = 30,
    ) -> None:
        self._send = send
        self._startup_grace = startup_grace
        self._started: float | None = None
        self._min_interval = min_interval
        self._heartbeat = heartbeat
        self._last_body: str | None = None
        self._last_sent = float("-inf")
        self._forced = False
        self._outage = Outage("publication rétablie")

    def force(self) -> None:
        """Le prochain tick publie quels que soient contenu et throttling (après connexion)."""
        self._forced = True

    def tick(self, snapshot: dict[str, Any], now: float) -> bool:
        if self._started is None:
            self._started = now
        body = json.dumps({k: v for k, v in snapshot.items() if k != "ts"}, sort_keys=True)
        elapsed = now - self._last_sent
        changed = body != self._last_body
        due = (changed and elapsed >= self._min_interval) or elapsed >= self._heartbeat
        if not (due or self._forced):
            return False
        try:
            self._send(json.dumps(snapshot, separators=(",", ":")))
        except Exception as exc:
            starting = self._last_body is None and now - self._started < self._startup_grace
            if starting:
                log.debug("publication échouée: %s", exc)
            else:
                self._outage.failed("publication échouée: %s", exc)
            return False
        self._outage.recovered()
        self._forced = False
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
    outage: Outage | None = None,
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
        (outage or Outage("")).failed(
            "itération de l'agent en échec", level=logging.ERROR, exc_info=True
        )
    else:
        if outage:
            outage.recovered()


def on_connect_callback(connected: threading.Event) -> Callable[..., None]:
    """Callback on_connect (API VERSION2) : signale la connexion à la boucle principale."""

    def on_connect(client: Any, userdata: Any, flags: Any, reason_code: Any, props: Any) -> None:
        if reason_code.is_failure:
            log.error("MQTT refusé (%s)", reason_code)
            return
        log.info("MQTT connecté (%s)", reason_code)
        connected.set()

    return on_connect


def _make_client(cfg: Config, connected: threading.Event) -> mqtt.Client:
    client = mqtt.Client(CallbackAPIVersion.VERSION2, client_id=f"claude-dash-{cfg.hostname}")
    if cfg.tls:
        client.tls_set(ca_certs=cfg.ca_certs)
    if cfg.username:
        client.username_pw_set(cfg.username, cfg.password)
    client.reconnect_delay_set(min_delay=1, max_delay=60)
    client.on_connect = on_connect_callback(connected)
    client.on_connect_fail = lambda c, u: log.debug("connexion MQTT impossible, nouvel essai")
    client.on_disconnect = lambda c, u, f, rc, p: log.warning("MQTT déconnecté (%s)", rc)
    client.connect_async(cfg.host, cfg.port, keepalive=60)
    client.loop_start()
    return client


def run(cfg: Config, client: mqtt.Client, connected: threading.Event) -> None:
    base = store.default_base()

    def send(payload: str) -> None:
        if not client.is_connected():
            raise OSError("MQTT non connecté")
        info = client.publish(cfg.topic, payload, qos=1, retain=True)
        if info.rc != MQTTErrorCode.MQTT_ERR_SUCCESS:
            raise OSError(f"publish rc={info.rc}")

    publisher = Publisher(send)
    outage = Outage("agent rétabli")
    stop = False

    def _stop(*_: object) -> None:
        nonlocal stop
        stop = True

    signal.signal(signal.SIGTERM, _stop)
    signal.signal(signal.SIGINT, _stop)
    log.info("agent démarré, topic %s", cfg.topic)
    while not stop:
        if connected.is_set():
            connected.clear()
            publisher.force()  # republier tout de suite après (re)connexion
        run_once(base, cfg.hostname, publisher, int(time.time()), time.monotonic(), outage=outage)
        time.sleep(1)
    client.disconnect()
    client.loop_stop()


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    try:
        cfg = load_config(default_path())
    except ConfigError as exc:
        log.error("%s", exc)
        sys.exit(2)
    connected = threading.Event()
    try:
        client = _make_client(cfg, connected)
    except (OSError, ssl.SSLError, ValueError) as exc:
        log.error(
            "client MQTT impossible à initialiser (%s:%s, tls=%s, ca_certs=%s): %s",
            cfg.host,
            cfg.port,
            cfg.tls,
            cfg.ca_certs,
            exc,
        )
        sys.exit(2)
    run(cfg, client, connected)
