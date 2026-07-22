"""Feedback + MIDI diff → taste profile updates."""
from __future__ import annotations

from typing import Any, Dict, List, Tuple

from app.converter import parse_midi_note_events
from app.db import StyleLibrary
from app.learning import TasteUpdater
from pathlib import Path


def midi_diff(
    original_events: List[Tuple[int, int, int, int]],
    edited_events: List[Tuple[int, int, int, int]],
) -> Dict[str, Any]:
    """Compare (start, pitch, length, vel) lists."""
    o = set(original_events)
    e = set(edited_events)
    added = list(e - o)
    removed = list(o - e)
    # velocity deltas for shared pitch+start
    o_map = {(s, p): (l, v) for s, p, l, v in original_events}
    e_map = {(s, p): (l, v) for s, p, l, v in edited_events}
    vel_deltas = []
    length_ratios = []
    for k in set(o_map) & set(e_map):
        if o_map[k][1] != e_map[k][1]:
            vel_deltas.append({"pitch": k[1], "start": k[0], "from": o_map[k][1], "to": e_map[k][1]})
        if o_map[k][0] > 0 and o_map[k][0] != e_map[k][0]:
            length_ratios.append(e_map[k][0] / o_map[k][0])
    return {
        "added": len(added),
        "removed": len(removed),
        "velocity_changes": vel_deltas,
        "added_notes": added[:50],
        "removed_notes": removed[:50],
        "mean_length_ratio": (
            sum(length_ratios) / len(length_ratios) if length_ratios else None
        ),
    }


def apply_feedback(
    library: StyleLibrary,
    *,
    generation_id: int,
    rating: int,
    text: str = "",
    original_midi: Path | None = None,
    edited_midi: Path | None = None,
    user_id: str = "default",
) -> Dict[str, Any]:
    diff: Dict[str, Any] = {}
    if original_midi and edited_midi and Path(original_midi).exists() and Path(edited_midi).exists():
        diff = midi_diff(parse_midi_note_events(original_midi), parse_midi_note_events(edited_midi))

    with library.connect() as db:
        row = db.execute(
            """
            SELECT output_json, session_id, element, intent_json
            FROM generations WHERE id=?
            """,
            (generation_id,),
        ).fetchone()
    output = {}
    session_id = None
    element = "chords"
    subgenre = None
    if row:
        import json

        output = json.loads(row["output_json"] or "{}")
        session_id = row["session_id"]
        element = row["element"] or "chords"
        intent = json.loads(row["intent_json"] or "{}")
        subgenre = intent.get("subgenre")

    library.add_feedback(
        generation_id,
        rating,
        text,
        diff,
        user_id=user_id,
        session_id=session_id,
    )
    library.record_heuristic_outcome(
        user_id=user_id,
        subgenre=subgenre,
        element=element,
        kept=rating > 0,
    )
    delta = TasteUpdater(library).update(
        output=output,
        feedback_text=text,
        rating=rating,
        diff=diff,
        user_id=user_id,
        subgenre=subgenre,
        element=element,
    )
    return {
        "taste": library.get_taste(user_id),
        "taste_delta": delta.model_dump(),
        "diff": diff,
    }
