"""CLI: python3 -m app.cli 'deep house chords in F minor'"""
from __future__ import annotations

import json
import argparse
import sys
from pathlib import Path

from app.api.endpoints import bootstrap_library, generate_endpoint
from app.learning import Consolidator


def main(argv=None):
    parser = argparse.ArgumentParser(prog="midi-brain")
    parser.add_argument("prompt", nargs="*")
    parser.add_argument("--reset-taste", metavar="USER_ID")
    parser.add_argument("--consolidate", action="store_true")
    parser.add_argument("--serve", action="store_true")
    args = parser.parse_args(argv)
    library = bootstrap_library()
    if args.reset_taste:
        library.reset_taste(args.reset_taste)
        print(f"Reset taste and heuristics for {args.reset_taste}")
        return 0
    if args.consolidate:
        result = Consolidator(library).run(
            Path(__file__).resolve().parents[1] / "brain" / "3-memory" / "brain_report.md"
        )
        print(json.dumps(result, indent=2))
        return 0
    if args.serve:
        import uvicorn

        uvicorn.run("app.api.server:app", host="127.0.0.1", port=8000)
        return 0
    req = " ".join(args.prompt) or "deep house chords in F minor, 8 bars"
    out = generate_endpoint({"request": req, "session_id": "cli"})
    print(json.dumps(out, indent=2))
    return 0 if out.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
