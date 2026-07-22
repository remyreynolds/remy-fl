"""Install the bundled script into FL Studio's piano-roll scripts folder."""
from __future__ import annotations

import platform
import shutil
from pathlib import Path

import flscript


SCRIPT_FILES = ("MIDI Agent.pyscript", "midi_agent_core.py")


def default_fl_script_dir(
    system: str | None = None,
    home: Path | None = None,
) -> Path:
    os_name = system or platform.system()
    user_home = home or Path.home()
    if os_name == "Windows":
        return (
            user_home
            / "Documents"
            / "Image-Line"
            / "FL Studio"
            / "Settings"
            / "Piano roll scripts"
            / "MIDI Agent"
        )
    if os_name == "Darwin":
        return (
            user_home
            / "Documents"
            / "Image-Line"
            / "FL Studio"
            / "Settings"
            / "Piano roll scripts"
            / "MIDI Agent"
        )
    raise RuntimeError(
        "Automatic FL script installation supports Windows and macOS only."
    )


def install_script(
    destination: Path | None = None,
    relay_port: int = 8765,
) -> Path:
    source_dir = Path(flscript.__file__).resolve().parent
    target = destination or default_fl_script_dir()
    target.mkdir(parents=True, exist_ok=True)
    for name in SCRIPT_FILES:
        source = source_dir / name
        if not source.exists():
            raise FileNotFoundError(f"Bundled FL script file is missing: {source}")
        shutil.copy2(source, target / name)
    helper = target / "midi_agent_core.py"
    helper.write_text(
        helper.read_text(encoding="utf-8").replace(
            "RELAY_PORT = 8765",
            f"RELAY_PORT = {int(relay_port)}",
            1,
        ),
        encoding="utf-8",
    )
    return target

