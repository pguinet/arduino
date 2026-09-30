from pathlib import Path

import pytest

from claude_dash.config import ConfigError, load_config


def write(tmp_path: Path, text: str, mode: int = 0o600) -> Path:
    p = tmp_path / "config.toml"
    p.write_text(text)
    p.chmod(mode)
    return p


def test_load_defaults(tmp_path: Path) -> None:
    cfg = load_config(write(tmp_path, '[mqtt]\nhost="h"\nusername="u"\npassword="p"\n'))
    assert cfg.port == 8883
    assert cfg.topic_prefix == "claude-dash"
    assert cfg.ca_certs is None
    assert cfg.tls is True
    assert cfg.hostname  # socket.gethostname()


def test_full_config_and_topic(tmp_path: Path) -> None:
    cfg = load_config(
        write(
            tmp_path,
            '[mqtt]\nhost="h"\nport=1883\nca_certs="/ca.pem"\ntopic_prefix="cd"\n'
            '[agent]\nhostname="srv-dev"\n',
        )
    )
    assert (cfg.host, cfg.port, cfg.ca_certs) == ("h", 1883, "/ca.pem")
    assert cfg.username is None and cfg.password is None
    assert cfg.topic == "cd/srv-dev/state"


def test_tls_can_be_disabled(tmp_path: Path) -> None:
    cfg = load_config(write(tmp_path, '[mqtt]\nhost="h"\ntls=false\n'))
    assert cfg.tls is False


def test_missing_host(tmp_path: Path) -> None:
    with pytest.raises(ConfigError):
        load_config(write(tmp_path, "[mqtt]\n"))


def test_rejects_world_readable(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="600"):
        load_config(write(tmp_path, '[mqtt]\nhost="h"\n', mode=0o644))


def test_rejects_group_readable(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="600"):
        load_config(write(tmp_path, '[mqtt]\nhost="h"\n', mode=0o640))


def test_missing_file(tmp_path: Path) -> None:
    with pytest.raises(ConfigError):
        load_config(tmp_path / "absent.toml")


def test_invalid_toml(tmp_path: Path) -> None:
    with pytest.raises(ConfigError):
        load_config(write(tmp_path, "[mqtt\n"))


@pytest.mark.parametrize(
    "body",
    [
        'host="h"\nport="abc"',
        'host="h"\nport=70000',
        'host="h"\nport=true',
        'host="h"\ntls="yes"',
        'host="h"\nusername=42',
        'host="h"\ntopic_prefix="a/#"',
        "host=12",
    ],
)
def test_invalid_values(tmp_path: Path, body: str) -> None:
    with pytest.raises(ConfigError):
        load_config(write(tmp_path, f"[mqtt]\n{body}\n"))


def test_hostname_with_wildcard_rejected(tmp_path: Path) -> None:
    with pytest.raises(ConfigError):
        load_config(write(tmp_path, '[mqtt]\nhost="h"\n[agent]\nhostname="a+b"\n'))
