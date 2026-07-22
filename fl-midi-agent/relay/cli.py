"""midiagent-relay command."""
from __future__ import annotations

import argparse
from pathlib import Path

import uvicorn

from .app import create_app
from .config import load_config
from .installer import install_script


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="midiagent-relay",
        description="Run the local FL Studio MIDI Agent relay.",
    )
    parser.add_argument(
        "--install-script",
        action="store_true",
        help="install the FL piano-roll script and exit",
    )
    parser.add_argument(
        "--script-destination",
        type=Path,
        help=argparse.SUPPRESS,
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.install_script:
        config = load_config()
        destination = install_script(
            args.script_destination,
            relay_port=config.port,
        )
        print(f"Installed MIDI Agent piano-roll script to: {destination}")
        return 0

    config = load_config()
    uvicorn.run(
        create_app(config),
        host=config.host,
        port=config.port,
        log_level="info",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

