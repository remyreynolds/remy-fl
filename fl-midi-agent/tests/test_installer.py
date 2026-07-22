from pathlib import Path
from types import SimpleNamespace

from relay.cli import main
from relay.installer import SCRIPT_FILES, default_fl_script_dir


def test_macos_install_path(tmp_path: Path):
    expected = (
        tmp_path
        / "Documents"
        / "Image-Line"
        / "FL Studio"
        / "Settings"
        / "Piano roll scripts"
        / "MIDI Agent"
    )
    assert default_fl_script_dir("Darwin", tmp_path) == expected


def test_install_script_command(tmp_path: Path, monkeypatch):
    destination = tmp_path / "FL Scripts" / "MIDI Agent"
    monkeypatch.setattr(
        "relay.cli.load_config",
        lambda: SimpleNamespace(port=18765),
    )
    result = main(
        [
            "--install-script",
            "--script-destination",
            str(destination),
        ]
    )
    assert result == 0
    assert {path.name for path in destination.iterdir()} == set(SCRIPT_FILES)
    helper = (destination / "midi_agent_core.py").read_text()
    assert "RELAY_PORT = 18765" in helper

