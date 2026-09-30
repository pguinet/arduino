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
_CLIENT_ID = re.compile(r"\S{1,64}")
# L'écran juge un serveur injoignable après 180 s sans message : marge de 10 s.
HEARTBEAT_RANGE = (5, 170)


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
    client_id: str
    certfile: str | None
    keyfile: str | None
    retain: bool
    heartbeat: int

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


def _opt_bool(section: dict[str, Any], key: str, default: bool) -> bool:
    value = section.get(key, default)
    if not isinstance(value, bool):
        raise ConfigError(f"{key} doit valoir true ou false")
    return value


def _opt_path(section: dict[str, Any], key: str) -> str | None:
    value = _opt_str(section, key)
    return None if value is None else os.path.expanduser(value)


def _client_id(mqtt: dict[str, Any], hostname: str) -> str:
    """Client id MQTT : explicite (ex. Device ID Scaleway), sinon claude-dash-<hostname>."""
    value = mqtt.get("client_id")
    if value is None:
        return f"claude-dash-{hostname}"
    if not isinstance(value, str) or not _CLIENT_ID.fullmatch(value):
        raise ConfigError(f"client_id invalide (non vide, sans espace, 64 max): {value!r}")
    return value


def _client_cert(mqtt: dict[str, Any], tls: bool) -> tuple[str | None, str | None]:
    """Certificat client (mTLS) : certfile et keyfile ensemble, clé privée en chmod 600."""
    certfile, keyfile = _opt_path(mqtt, "certfile"), _opt_path(mqtt, "keyfile")
    if certfile is None and keyfile is None:
        return None, None
    if certfile is None or keyfile is None:
        raise ConfigError("certfile et keyfile doivent être renseignés ensemble")
    if not tls:
        raise ConfigError("certfile/keyfile exigent tls = true")
    for key, value in (("certfile", certfile), ("keyfile", keyfile)):
        if not Path(value).is_file():
            raise ConfigError(f"{key} introuvable: {value}")
    mode = stat.S_IMODE(Path(keyfile).stat().st_mode)
    if mode & 0o077:
        raise ConfigError(f"keyfile {keyfile} doit être en chmod 600 (actuel {mode:o})")
    try:
        pem = Path(keyfile).read_text(errors="replace")
    except OSError as exc:
        raise ConfigError(f"keyfile illisible: {exc}") from None
    # Sans tty (systemd), une clé chiffrée ferait échouer tls_set : refus explicite.
    if "ENCRYPTED" in pem:
        raise ConfigError(
            f"keyfile {keyfile} est chiffrée : la déchiffrer "
            f"(openssl pkey -in {keyfile} -out clair.key, puis remplacer, chmod 600)"
        )
    return certfile, keyfile


def _heartbeat(agent: dict[str, Any]) -> int:
    value = agent.get("heartbeat", 60)
    low, high = HEARTBEAT_RANGE
    if not isinstance(value, int) or isinstance(value, bool) or not low <= value <= high:
        raise ConfigError(f"[agent] heartbeat invalide (entier {low}..{high} s): {value!r}")
    return value


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
        tls = _opt_bool(mqtt, "tls", True)
        prefix = _opt_str(mqtt, "topic_prefix") or "claude-dash"
        hostname = _hostname(agent)
        certfile, keyfile = _client_cert(mqtt, tls)
        return Config(
            host=host,
            port=port,
            username=_opt_str(mqtt, "username"),
            password=_opt_str(mqtt, "password"),
            ca_certs=_opt_path(mqtt, "ca_certs"),
            tls=tls,
            topic_prefix=_topic_prefix(prefix),
            hostname=hostname,
            client_id=_client_id(mqtt, hostname),
            certfile=certfile,
            keyfile=keyfile,
            retain=_opt_bool(mqtt, "retain", True),
            heartbeat=_heartbeat(agent),
        )
    except ConfigError as exc:
        raise ConfigError(f"{path}: {exc}") from None
