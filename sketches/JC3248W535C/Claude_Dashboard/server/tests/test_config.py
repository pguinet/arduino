from pathlib import Path

import pytest

from claude_dash import config
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
        'host="h"\ntopic_prefix="$SYS"',
        "host=12",
    ],
)
def test_invalid_values(tmp_path: Path, body: str) -> None:
    with pytest.raises(ConfigError):
        load_config(write(tmp_path, f"[mqtt]\n{body}\n"))


def test_hostname_with_wildcard_rejected(tmp_path: Path) -> None:
    with pytest.raises(ConfigError):
        load_config(write(tmp_path, '[mqtt]\nhost="h"\n[agent]\nhostname="a+b"\n'))


@pytest.mark.parametrize("name", ["a b", "x" * 33, "hôte", "a/b", ""])
def test_explicit_hostname_validated(tmp_path: Path, name: str) -> None:
    with pytest.raises(ConfigError):
        load_config(write(tmp_path, f'[mqtt]\nhost="h"\n[agent]\nhostname="{name}"\n'))


def test_explicit_hostname_may_contain_dots(tmp_path: Path) -> None:
    cfg = load_config(write(tmp_path, '[mqtt]\nhost="h"\n[agent]\nhostname="srv.dev_1-a"\n'))
    assert cfg.hostname == "srv.dev_1-a"


def test_default_hostname_is_short_name(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(config.socket, "gethostname", lambda: "srv-01.example.org")
    assert load_config(write(tmp_path, '[mqtt]\nhost="h"\n')).hostname == "srv-01"


def test_invalid_default_hostname_asks_for_explicit(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(config.socket, "gethostname", lambda: "x" * 40)
    with pytest.raises(ConfigError, match=r"\[agent\] hostname"):
        load_config(write(tmp_path, '[mqtt]\nhost="h"\n'))


# --- options Scaleway IoT Hub : client id, mTLS, retain, heartbeat ----------


def test_new_options_defaults(tmp_path: Path) -> None:
    cfg = load_config(write(tmp_path, '[mqtt]\nhost="h"\n[agent]\nhostname="srv"\n'))
    assert cfg.client_id == "claude-dash-srv"
    assert cfg.certfile is None and cfg.keyfile is None
    assert cfg.retain is True
    assert cfg.heartbeat == 60


def test_explicit_client_id(tmp_path: Path) -> None:
    uuid = "0b7e3c5e-4a9f-4c7d-9a51-2f7f8e0d6b1a"
    cfg = load_config(write(tmp_path, f'[mqtt]\nhost="h"\nclient_id="{uuid}"\n'))
    assert cfg.client_id == uuid


@pytest.mark.parametrize("value", ['""', '"a b"', '"a\\tb"', f'"{"x" * 65}"', "12"])
def test_invalid_client_id(tmp_path: Path, value: str) -> None:
    with pytest.raises(ConfigError, match="client_id"):
        load_config(write(tmp_path, f'[mqtt]\nhost="h"\nclient_id={value}\n'))


def test_client_id_max_length_accepted(tmp_path: Path) -> None:
    cfg = load_config(write(tmp_path, f'[mqtt]\nhost="h"\nclient_id="{"x" * 64}"\n'))
    assert cfg.client_id == "x" * 64


def cert_pair(tmp_path: Path, key_mode: int = 0o600) -> tuple[Path, Path]:
    cert = tmp_path / "device.pem"
    key = tmp_path / "device.key"
    cert.write_text("cert")
    key.write_text("key")
    cert.chmod(0o644)
    key.chmod(key_mode)
    return cert, key


def test_client_certificate(tmp_path: Path) -> None:
    cert, key = cert_pair(tmp_path)
    cfg = load_config(write(tmp_path, f'[mqtt]\nhost="h"\ncertfile="{cert}"\nkeyfile="{key}"\n'))
    assert (cfg.certfile, cfg.keyfile) == (str(cert), str(key))


def test_paths_expand_home(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("HOME", str(tmp_path))
    cert_pair(tmp_path)
    cfg = load_config(
        write(
            tmp_path,
            '[mqtt]\nhost="h"\nca_certs="~/ca.pem"\n'
            'certfile="~/device.pem"\nkeyfile="~/device.key"\n',
        )
    )
    assert cfg.ca_certs == str(tmp_path / "ca.pem")
    assert cfg.certfile == str(tmp_path / "device.pem")
    assert cfg.keyfile == str(tmp_path / "device.key")


@pytest.mark.parametrize("key", ["certfile", "keyfile"])
def test_certfile_and_keyfile_go_together(tmp_path: Path, key: str) -> None:
    cert, keyfile = cert_pair(tmp_path)
    path = cert if key == "certfile" else keyfile
    with pytest.raises(ConfigError, match="certfile et keyfile"):
        load_config(write(tmp_path, f'[mqtt]\nhost="h"\n{key}="{path}"\n'))


@pytest.mark.parametrize("missing", ["certfile", "keyfile"])
def test_client_certificate_files_must_exist(tmp_path: Path, missing: str) -> None:
    cert, key = cert_pair(tmp_path)
    (cert if missing == "certfile" else key).unlink()
    with pytest.raises(ConfigError, match=missing):
        load_config(write(tmp_path, f'[mqtt]\nhost="h"\ncertfile="{cert}"\nkeyfile="{key}"\n'))


@pytest.mark.parametrize("mode", [0o640, 0o604, 0o644])
def test_keyfile_must_be_private(tmp_path: Path, mode: int) -> None:
    cert, key = cert_pair(tmp_path, key_mode=mode)
    with pytest.raises(ConfigError, match="chmod 600"):
        load_config(write(tmp_path, f'[mqtt]\nhost="h"\ncertfile="{cert}"\nkeyfile="{key}"\n'))


def test_keyfile_readonly_owner_accepted(tmp_path: Path) -> None:
    cert, key = cert_pair(tmp_path, key_mode=0o400)
    cfg = load_config(write(tmp_path, f'[mqtt]\nhost="h"\ncertfile="{cert}"\nkeyfile="{key}"\n'))
    assert cfg.keyfile == str(key)


def test_client_certificate_requires_tls(tmp_path: Path) -> None:
    cert, key = cert_pair(tmp_path)
    with pytest.raises(ConfigError, match="tls"):
        load_config(
            write(
                tmp_path,
                f'[mqtt]\nhost="h"\ntls=false\ncertfile="{cert}"\nkeyfile="{key}"\n',
            )
        )


def test_retain_can_be_disabled(tmp_path: Path) -> None:
    assert load_config(write(tmp_path, '[mqtt]\nhost="h"\nretain=false\n')).retain is False


def test_retain_must_be_bool(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="retain"):
        load_config(write(tmp_path, '[mqtt]\nhost="h"\nretain="no"\n'))


@pytest.mark.parametrize("value", [5, 20, 170])
def test_heartbeat_accepted(tmp_path: Path, value: int) -> None:
    cfg = load_config(write(tmp_path, f'[mqtt]\nhost="h"\n[agent]\nheartbeat={value}\n'))
    assert cfg.heartbeat == value


@pytest.mark.parametrize("value", ["4", "171", "0", "-1", "true", "20.5", '"20"'])
def test_heartbeat_rejected(tmp_path: Path, value: str) -> None:
    with pytest.raises(ConfigError, match="heartbeat"):
        load_config(write(tmp_path, f'[mqtt]\nhost="h"\n[agent]\nheartbeat={value}\n'))


@pytest.mark.parametrize(
    "header",
    [
        "-----BEGIN ENCRYPTED PRIVATE KEY-----",
        "-----BEGIN RSA PRIVATE KEY-----\nProc-Type: 4,ENCRYPTED",
    ],
)
def test_encrypted_keyfile_rejected(tmp_path: Path, header: str) -> None:
    cert, key = cert_pair(tmp_path)
    key.write_text(f"{header}\nabc\n")
    with pytest.raises(ConfigError, match="chiffrée"):
        load_config(write(tmp_path, f'[mqtt]\nhost="h"\ncertfile="{cert}"\nkeyfile="{key}"\n'))


@pytest.mark.parametrize(
    ("section", "key"),
    [
        ("mqtt", "host"),
        ("mqtt", "client_id"),
        ("mqtt", "username"),
        ("agent", "hostname"),
    ],
)
def test_example_placeholder_rejected(tmp_path: Path, section: str, key: str) -> None:
    tables: dict[str, dict[str, str]] = {"mqtt": {"host": '"h"', "tls": "false"}, "agent": {}}
    tables[section][key] = '"votre_valeur"'
    body = "".join(
        f"[{name}]\n" + "".join(f"{k}={v}\n" for k, v in fields.items())
        for name, fields in tables.items()
    )
    with pytest.raises(ConfigError, match=f"valeur d'exemple non renseignée : {key}"):
        load_config(write(tmp_path, body))


def test_placeholder_only_as_prefix(tmp_path: Path) -> None:
    cfg = load_config(write(tmp_path, '[mqtt]\nhost="h"\n[agent]\nhostname="pas_votre_nom"\n'))
    assert cfg.hostname == "pas_votre_nom"
