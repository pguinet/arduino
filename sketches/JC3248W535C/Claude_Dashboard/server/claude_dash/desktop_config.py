"""Configuration of the desktop client (TOML, chmod 600), separate from the agent's:
it connects with its own subscribe-only device."""

from __future__ import annotations

import os
import stat
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Literal

from claude_dash.config import (
    ConfigError,
    Transport,
    _client_cert,
    _client_id,
    _opt_bool,
    _opt_path,
    _opt_str,
    _reject_placeholders,
    _topic_prefix,
    _transport,
)

Corner = Literal["top-left", "top-right", "bottom-left", "bottom-right"]
CORNERS: tuple[Corner, ...] = ("top-left", "top-right", "bottom-left", "bottom-right")


@dataclass(frozen=True)
class DesktopConfig:
    host: str
    port: int
    username: str | None
    password: str | None
    ca_certs: str | None
    tls: bool
    topic_prefix: str
    client_id: str
    certfile: str | None
    keyfile: str | None
    transport: Transport
    corner: Corner

    @property
    def topic(self) -> str:
        return f"{self.topic_prefix}/+/state"


def default_path() -> Path:
    xdg = os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))
    return Path(xdg) / "claude-dash" / "desktop.toml"


def _corner(window: dict[str, Any]) -> Corner:
    value = window.get("corner", "top-right")
    for corner in CORNERS:
        if value == corner:
            return corner
    raise ConfigError(f"[window] corner invalide ({', '.join(CORNERS)}): {value!r}")


def _port(mqtt: dict[str, Any]) -> int:
    port = mqtt.get("port", 8883)
    if not isinstance(port, int) or isinstance(port, bool) or not 0 < port < 65536:
        raise ConfigError(f"port invalide: {port!r}")
    return port


def load_desktop_config(path: Path) -> DesktopConfig:
    try:
        mode = stat.S_IMODE(path.stat().st_mode)
        raw = tomllib.loads(path.read_text())
    except (OSError, UnicodeDecodeError, tomllib.TOMLDecodeError) as exc:
        raise ConfigError(f"{path}: {exc}") from exc
    if mode & 0o077:
        raise ConfigError(f"{path} doit être en chmod 600 (actuel {mode:o})")
    mqtt = raw.get("mqtt", {})
    window = raw.get("window", {})
    if not isinstance(mqtt, dict) or not isinstance(window, dict):
        raise ConfigError(f"{path}: [mqtt] et [window] doivent être des tables")
    try:
        _reject_placeholders(mqtt, window)
        host = _opt_str(mqtt, "host")
        if not host:
            raise ConfigError("[mqtt] host manquant")
        tls = _opt_bool(mqtt, "tls", True)
        certfile, keyfile = _client_cert(mqtt, tls)
        return DesktopConfig(
            host=host,
            port=_port(mqtt),
            username=_opt_str(mqtt, "username"),
            password=_opt_str(mqtt, "password"),
            ca_certs=_opt_path(mqtt, "ca_certs"),
            tls=tls,
            topic_prefix=_topic_prefix(_opt_str(mqtt, "topic_prefix") or "claude-dash"),
            client_id=_client_id(mqtt, "desktop"),
            certfile=certfile,
            keyfile=keyfile,
            transport=_transport(mqtt),
            corner=_corner(window),
        )
    except ConfigError as exc:
        raise ConfigError(f"{path}: {exc}") from None
