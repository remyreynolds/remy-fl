"""Relay configuration stored at ~/.midiagent/config.toml."""
from __future__ import annotations

import os
import json
import tomllib
from dataclasses import dataclass
from pathlib import Path


DEFAULT_HOME = Path.home() / ".midiagent"


@dataclass(frozen=True)
class RelayConfig:
    backend_url: str = "http://127.0.0.1:8000"
    api_key: str = ""
    host: str = "127.0.0.1"
    port: int = 8765
    backend_timeout_seconds: float = 90.0
    bridge_dir: Path = Path.home() / "Documents" / "MIDIAgent" / "bridge"
    log_dir: Path = DEFAULT_HOME / "logs"


def default_config_path() -> Path:
    return DEFAULT_HOME / "config.toml"


def write_default_config(path: Path | None = None) -> Path:
    target = path or default_config_path()
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        return target
    target.write_text(
        "\n".join(
            [
                'backend_url = "http://127.0.0.1:8000"',
                'api_key = ""',
                'host = "127.0.0.1"',
                "port = 8765",
                "backend_timeout_seconds = 90.0",
                "bridge_dir = "
                + json.dumps(
                    str(Path.home() / "Documents" / "MIDIAgent" / "bridge")
                ),
                "",
            ]
        ),
        encoding="utf-8",
    )
    return target


def load_config(path: Path | None = None) -> RelayConfig:
    source = write_default_config(path)
    with source.open("rb") as handle:
        raw = tomllib.load(handle)

    backend_url = os.environ.get(
        "MIDIAGENT_BACKEND_URL", raw.get("backend_url", RelayConfig.backend_url)
    ).rstrip("/")
    api_key = os.environ.get("MIDIAGENT_API_KEY", raw.get("api_key", ""))
    bridge_value = raw.get(
        "bridge_dir", str(Path.home() / "Documents" / "MIDIAgent" / "bridge")
    )
    return RelayConfig(
        backend_url=backend_url,
        api_key=api_key,
        host=str(raw.get("host", "127.0.0.1")),
        port=int(raw.get("port", 8765)),
        backend_timeout_seconds=float(raw.get("backend_timeout_seconds", 90.0)),
        bridge_dir=Path(bridge_value).expanduser(),
        log_dir=DEFAULT_HOME / "logs",
    )

