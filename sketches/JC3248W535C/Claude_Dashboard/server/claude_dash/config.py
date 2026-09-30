"""Chargement de la configuration de l'agent (TOML, permissions 600)."""

from __future__ import annotations

import os
import re
import socket
import stat
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Any

_TOPIC_FORBIDDEN = set("/+#\0")
_HOSTNAME = re.compile(r"[A-Za-z0-9._-]{1,32}")


class ConfigError(Exception):
    pass


@dataclass(frozen=True)
class Config:
    host: str
    port: int
    username: str | None
    password: str | None
    ca_certs: str | None
    tls: bool
    topic_prefix: str
    hostname: str

    @property
    def topic(self) -> str:
        return f"{self.topic_prefix}/{self.hostname}/state"


def default_path() -> Path:
    xdg = os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))
    return Path(xdg) / "claude-dash" / "config.toml"


def _opt_str(section: dict[str, Any], key: str) -> str | None:
    value = section.get(key)
    if value is not None and not isinstance(value, str):
        raise ConfigError(f"{key} doit être une chaîne")
    return value


def _topic_prefix(value: str) -> str:
    """Niveau de topic MQTT : non vide, sans séparateur ni joker, hors topics système `$`."""
    if not value or value.startswith("$") or _TOPIC_FORBIDDEN & set(value):
        raise ConfigError(f"topic_prefix invalide pour un topic MQTT: {value!r}")
    return value


def _hostname(agent: dict[str, Any]) -> str:
    """Nom publié (client id + topic) : explicite, sinon nom court de la machine."""
    explicit = _opt_str(agent, "hostname")
    if explicit is not None:
        if not _HOSTNAME.fullmatch(explicit):
            raise ConfigError(f"[agent] hostname invalide (A-Z a-z 0-9 . _ -, 1..32): {explicit!r}")
        return explicit
    short = socket.gethostname().split(".")[0]
    if not _HOSTNAME.fullmatch(short):
        raise ConfigError(
            f"nom de machine {short!r} inutilisable dans un topic : définir [agent] hostname"
        )
    return short


def load_config(path: Path) -> Config:
    try:
        mode = stat.S_IMODE(path.stat().st_mode)
        raw = tomllib.loads(path.read_text())
    except (OSError, UnicodeDecodeError, tomllib.TOMLDecodeError) as exc:
        raise ConfigError(f"{path}: {exc}") from exc
    if mode & 0o077:
        raise ConfigError(f"{path} doit être en chmod 600 (actuel {mode:o})")
    mqtt = raw.get("mqtt", {})
    agent = raw.get("agent", {})
    if not isinstance(mqtt, dict) or not isinstance(agent, dict):
        raise ConfigError(f"{path}: [mqtt] et [agent] doivent être des tables")
    try:
        host = _opt_str(mqtt, "host")
        if not host:
            raise ConfigError("[mqtt] host manquant")
        port = mqtt.get("port", 8883)
        if not isinstance(port, int) or isinstance(port, bool) or not 0 < port < 65536:
            raise ConfigError(f"port invalide: {port!r}")
        tls = mqtt.get("tls", True)
        if not isinstance(tls, bool):
            raise ConfigError("tls doit valoir true ou false")
        prefix = _opt_str(mqtt, "topic_prefix") or "claude-dash"
        return Config(
            host=host,
            port=port,
            username=_opt_str(mqtt, "username"),
            password=_opt_str(mqtt, "password"),
            ca_certs=_opt_str(mqtt, "ca_certs"),
            tls=tls,
            topic_prefix=_topic_prefix(prefix),
            hostname=_hostname(agent),
        )
    except ConfigError as exc:
        raise ConfigError(f"{path}: {exc}") from None
