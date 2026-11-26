from pathlib import Path

import pytest

from claude_dash.config import ConfigError
from claude_dash.desktop_config import load_desktop_config

EXAMPLE = Path(__file__).resolve().parent.parent / "desktop.toml.example"


def write(tmp_path: Path, text: str, mode: int = 0o600) -> Path:
    p = tmp_path / "desktop.toml"
    p.write_text(text)
    p.chmod(mode)
    return p


def test_desktop_defaults(tmp_path: Path) -> None:
    cfg = load_desktop_config(write(tmp_path, '[mqtt]\nhost="h"\n'))
    assert (cfg.port, cfg.tls, cfg.transport, cfg.corner) == (8883, True, "tcp", "top-right")
    assert cfg.client_id == "claude-dash-desktop"
    assert cfg.topic == "claude-dash/+/state"


def test_desktop_websockets_and_corner(tmp_path: Path) -> None:
    cfg = load_desktop_config(
        write(
            tmp_path,
            '[mqtt]\nhost="h"\nport=443\ntransport="websockets"\n[window]\ncorner="bottom-left"\n',
        )
    )
    assert (cfg.port, cfg.transport, cfg.corner) == (443, "websockets", "bottom-left")


def test_desktop_topic_prefix(tmp_path: Path) -> None:
    cfg = load_desktop_config(write(tmp_path, '[mqtt]\nhost="h"\ntopic_prefix="cd"\n'))
    assert cfg.topic == "cd/+/state"


def test_desktop_ignores_agent_only_keys(tmp_path: Path) -> None:
    cfg = load_desktop_config(write(tmp_path, '[mqtt]\nhost="h"\nretain=false\n'))
    assert cfg.host == "h"


def test_desktop_rejects_bad_corner(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="corner"):
        load_desktop_config(write(tmp_path, '[mqtt]\nhost="h"\n[window]\ncorner="middle"\n'))


def test_desktop_requires_host(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="host"):
        load_desktop_config(write(tmp_path, "[mqtt]\nport=1883\n"))


def test_desktop_requires_chmod_600(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="600"):
        load_desktop_config(write(tmp_path, '[mqtt]\nhost="h"\n', mode=0o644))


def test_desktop_missing_file(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="absent.toml"):
        load_desktop_config(tmp_path / "absent.toml")


def test_desktop_rejects_placeholder(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="client_id"):
        load_desktop_config(
            write(tmp_path, '[mqtt]\nhost="h"\nclient_id="votre_device_id_poste"\n')
        )


def test_desktop_example_file_parses_once_placeholders_filled(tmp_path: Path) -> None:
    for name in ("iot-hub-ca.pem", "desktop.crt", "desktop.key"):
        (tmp_path / name).write_text("dummy\n")
    (tmp_path / "desktop.key").chmod(0o600)
    text = EXAMPLE.read_text().replace("~/.config/claude-dash", str(tmp_path))
    text = text.replace('"votre_device_id_poste"', '"8ddab598-0000-0000-0000-000000000000"')
    cfg = load_desktop_config(write(tmp_path, text))
    assert (cfg.transport, cfg.port) == ("websockets", 443)
    assert cfg.certfile == str(tmp_path / "desktop.crt")
