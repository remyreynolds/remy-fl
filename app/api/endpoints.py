"""Minimal API surface for generate + feedback."""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict, Optional

from app.db import StyleLibrary
from app.feedback import apply_feedback
from app.pipeline import AgentPipeline


def bootstrap_library(replace_seeds: bool = False) -> StyleLibrary:
    lib = StyleLibrary()
    seed_path = Path(__file__).resolve().parents[2] / "seeds" / "fingerprints.json"
    if lib.fingerprint_count() == 0 or replace_seeds:
        items = json.loads(seed_path.read_text())
        lib.seed_fingerprints(items, replace=replace_seeds)
    return lib


def generate_endpoint(body: Dict[str, Any]) -> Dict[str, Any]:
    lib = bootstrap_library()
    pipe = AgentPipeline(library=lib)
    request = body.get("prompt") or body.get("request", "")
    if body.get("element") and str(body["element"]).lower() not in request.lower():
        request += f", {str(body['element']).lower()}"
    if body.get("bars") and f"{body['bars']} bar" not in request.lower():
        request += f", {body['bars']} bars"
    res = pipe.generate(
        request,
        session_id=body.get("session_id", "default"),
        user_id=body.get("user_id", "default"),
    )
    out: Dict[str, Any] = {
        "ok": res.ok,
        "generation_id": res.generation_id,
        "warnings": res.warnings,
        "errors": res.errors,
        "clarifying_question": res.clarifying_question,
        "midi_paths": {k: str(v) for k, v in res.midi_paths.items()},
        "attempts": res.attempts,
    }
    if res.generation:
        out["json"] = res.finalized or res.generation.model_dump()
    return out


def feedback_endpoint(body: Dict[str, Any]) -> Dict[str, Any]:
    lib = bootstrap_library()
    return apply_feedback(
        lib,
        generation_id=int(body["generation_id"]),
        rating=int(body.get("rating", 0)),
        text=body.get("text", ""),
        original_midi=Path(body["original_midi"]) if body.get("original_midi") else None,
        edited_midi=Path(body["edited_midi"]) if body.get("edited_midi") else None,
        user_id=body.get("user_id", "default"),
    )
