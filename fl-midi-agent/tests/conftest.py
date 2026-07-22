from __future__ import annotations

import logging
from pathlib import Path

import pytest

from relay.config import RelayConfig
from relay.models import NoteContract


@pytest.fixture
def contract_dict() -> dict:
    return {
        "meta": {
            "bpm": 124,
            "key": "F minor",
            "ppq": 960,
            "loop_bars": 8,
            "swing_pct": 56,
        },
        "tracks": [
            {
                "name": "Chords",
                "role": "chords",
                "notes": [
                    {
                        "pitch": 53,
                        "start_tick": 0,
                        "length_ticks": 420,
                        "velocity": 92,
                    }
                ],
            }
        ],
    }


@pytest.fixture
def contract(contract_dict) -> NoteContract:
    return NoteContract.model_validate(contract_dict)


@pytest.fixture
def relay_config(tmp_path: Path) -> RelayConfig:
    return RelayConfig(
        backend_url="http://brain.test",
        api_key="",
        host="127.0.0.1",
        port=8765,
        backend_timeout_seconds=2,
        bridge_dir=tmp_path / "bridge",
        log_dir=tmp_path / "logs",
    )


@pytest.fixture
def logger() -> logging.Logger:
    value = logging.getLogger("midiagent.test")
    value.handlers.clear()
    value.addHandler(logging.NullHandler())
    return value

