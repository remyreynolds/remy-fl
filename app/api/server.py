"""FastAPI application for the cognitive brain."""
from __future__ import annotations

from io import BytesIO
from pathlib import Path
from typing import Literal

import mido
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, Field

from app.feedback import apply_feedback
from app.learning import Consolidator
from app.memory import CognitiveMemory
from app.pipeline import AgentPipeline
from app.schema import InputNote


class GenerateBody(BaseModel):
    prompt: str | None = None
    request: str | None = None
    element: Literal["Chords", "Bass", "Melody", "Arp", "Perc", "Stack"] | None = None
    bars: int | None = Field(default=None, ge=1, le=16)
    session_id: str = "default"
    user_id: str = "default"
    mode: Literal[
        "Generate", "Fix groove", "Reharmonize", "Continue", "Bass under"
    ] = "Generate"
    input_notes: list[InputNote] = Field(default_factory=list)
    selection_only: bool = False
    roll_ppq: int | None = Field(default=None, gt=0)
    sound_type: Literal["pluck", "pad", "keys", "stab", "sub", "lead"] | None = None


class FeedbackBody(BaseModel):
    generation_id: int | None = None
    session_id: str | None = None
    user_id: str = "default"
    rating: int = Field(ge=-1, le=1)
    text: str = ""
    original_midi: str | None = None
    edited_midi: str | None = None


def create_app(memory: CognitiveMemory | None = None) -> FastAPI:
    store = memory or CognitiveMemory()
    pipeline = AgentPipeline(library=store)
    app = FastAPI(title="House MIDI Cognitive Brain", version="2.0.0")
    app.state.memory = store
    app.state.pipeline = pipeline

    @app.get("/health")
    def health():
        return {"status": "ok", "brain": "cognitive-v2"}

    @app.post("/generate")
    def generate(body: GenerateBody):
        prompt = (body.prompt or body.request or "").strip()
        if not prompt:
            raise HTTPException(status_code=422, detail="prompt is required")
        suffix = []
        if body.element and body.element.lower() not in prompt.lower():
            suffix.append(body.element.lower())
        if body.bars and f"{body.bars} bar" not in prompt.lower():
            suffix.append(f"{body.bars} bars")
        request = ", ".join([prompt, *suffix])
        result = pipeline.generate(
            request,
            session_id=body.session_id,
            user_id=body.user_id,
            mode=body.mode,
            input_notes=[note.model_dump() for note in body.input_notes],
            selection_only=body.selection_only,
            roll_ppq=body.roll_ppq,
            sound_type=body.sound_type,
        )
        if result.clarifying_question:
            return {
                "ok": True,
                "clarifying_question": result.clarifying_question,
                "trace_id": result.trace_id,
            }
        if not result.ok or not result.finalized:
            raise HTTPException(
                status_code=502,
                detail="; ".join(result.errors) or "generation failed",
            )
        return {
            **result.finalized,
            "generation_id": result.generation_id,
            "midi_paths": {
                key: str(value) for key, value in result.midi_paths.items()
            },
        }

    @app.post("/feedback")
    def feedback(body: FeedbackBody):
        generation_id = body.generation_id
        if generation_id is None and body.session_id:
            with store.connect() as db:
                row = db.execute(
                    """
                    SELECT id FROM generations WHERE session_id=?
                    ORDER BY id DESC LIMIT 1
                    """,
                    (body.session_id,),
                ).fetchone()
            generation_id = int(row["id"]) if row else None
        if generation_id is None:
            raise HTTPException(
                status_code=404,
                detail="no generation found for feedback",
            )
        return apply_feedback(
            store,
            generation_id=generation_id,
            rating=body.rating,
            text=body.text,
            original_midi=Path(body.original_midi)
            if body.original_midi
            else None,
            edited_midi=Path(body.edited_midi) if body.edited_midi else None,
            user_id=body.user_id,
        )

    @app.get("/debug/trace/{trace_id}")
    def debug_trace(trace_id: str):
        trace = store.get_trace(trace_id)
        if not trace:
            raise HTTPException(status_code=404, detail="trace not found")
        return trace

    @app.get("/companion/history")
    def companion_history(
        session_id: str | None = None,
        q: str = "",
        include_past: bool = False,
    ):
        return store.history(
            session_id=None if include_past else session_id,
            query=q,
        )

    @app.get("/companion/brain")
    def companion_brain(user_id: str = "default"):
        taste = store.get_taste(user_id)
        beliefs = []
        preferred = taste.get("preferred_keys") or {}
        confidence = taste.get("confidence") or {}
        if preferred:
            keys = sorted(preferred, key=preferred.get, reverse=True)[:2]
            beliefs.append(
                {
                    "id": "keys",
                    "label": f"prefers {' / '.join(keys)}",
                    "confidence": confidence.get("preferred_keys", 0.5),
                }
            )
        if taste.get("swing_bias"):
            beliefs.append(
                {
                    "id": "swing",
                    "label": f"swing {float(taste['swing_bias']):+g}",
                    "confidence": confidence.get("swing_bias", 0.5),
                }
            )
        if taste.get("density_bias"):
            beliefs.append(
                {
                    "id": "density",
                    "label": "bass: sparse"
                    if taste["density_bias"] < 0
                    else "prefers dense parts",
                    "confidence": confidence.get("density_bias", 0.5),
                }
            )
        if taste.get("brightness"):
            beliefs.append(
                {
                    "id": "brightness",
                    "label": "leans dark"
                    if taste["brightness"] < 0
                    else "leans bright",
                    "confidence": confidence.get("brightness", 0.5),
                }
            )
        with store.connect() as db:
            heuristics = [
                {
                    "id": row["id"],
                    "rule": row["rule"],
                    "trigger": row["trigger_condition"].replace(
                        " AND ", " · "
                    ),
                    "evidence": row["evidence"],
                    "active": bool(row["active"]),
                }
                for row in db.execute(
                    """
                    SELECT * FROM heuristics WHERE user_id=?
                    ORDER BY evidence DESC, id DESC LIMIT 30
                    """,
                    (user_id,),
                )
            ]
        report_path = Path(__file__).resolve().parents[2] / "brain_report.md"
        report = (
            report_path.read_text(encoding="utf-8")
            if report_path.exists()
            else "# Weekly brain report\n\nNo consolidation report yet."
        )
        return {"taste": beliefs, "heuristics": heuristics, "report": report}

    @app.delete("/companion/brain/taste/{belief_id}")
    def delete_taste_belief(belief_id: str, user_id: str = "default"):
        store.delete_taste_belief(belief_id, user_id)
        return {"ok": True}

    @app.patch("/companion/brain/heuristics/{heuristic_id}")
    def toggle_companion_heuristic(heuristic_id: int, payload: dict):
        store.set_heuristic_active(heuristic_id, bool(payload.get("active")))
        return {"ok": True}

    @app.get("/companion/references")
    def companion_references():
        return store.list_fingerprints()

    @app.post("/companion/references/analyze")
    def analyze_companion_reference(payload: dict):
        reference = str(payload.get("reference", "")).strip()
        if not reference:
            raise HTTPException(status_code=422, detail="reference is required")
        low = reference.lower()
        subgenre = next(
            (
                name
                for needle, name in (
                    ("tech", "tech_house"),
                    ("garage", "garage"),
                    ("afro", "afro"),
                    ("piano", "piano_house"),
                    ("french", "french"),
                    ("progress", "prog_melodic"),
                    ("classic", "classic"),
                )
                if needle in low
            ),
            "deep_house",
        )
        card = store.get_genre_card(subgenre)
        return {
            "fingerprint": {
                "track": reference,
                "subgenre": subgenre,
                "bpm": card.get("default_bpm", 124),
                "key": card.get("default_key", "F minor"),
                "progression": [],
                "groove": card.get("rhythm", "house pocket"),
                "confidence": 0.62,
            }
        }

    @app.post("/companion/references/analyze-midi")
    def analyze_companion_midi(payload: dict):
        try:
            content = bytes.fromhex(str(payload.get("content_hex", "")))
            midi = mido.MidiFile(file=BytesIO(content))
        except Exception as exc:
            raise HTTPException(status_code=422, detail="invalid MIDI file") from exc
        pitches = []
        tempo = 500000
        for track in midi.tracks:
            for message in track:
                if message.type == "set_tempo":
                    tempo = message.tempo
                elif message.type == "note_on" and message.velocity > 0:
                    pitches.append(message.note)
        role = "bass" if pitches and sum(pitches) / len(pitches) < 48 else "chords"
        return {
            "fingerprint": {
                "track": payload.get("filename") or "Dropped MIDI",
                "subgenre": "deep_house",
                "bpm": round(mido.tempo2bpm(tempo), 1),
                "key": "unknown",
                "progression": [],
                "groove": f"{role} MIDI with {len(pitches)} note onsets",
                "confidence": 0.58,
            }
        }

    @app.post("/companion/references")
    def save_companion_reference(payload: dict):
        groove = str(payload.get("groove", ""))
        item = {
            **payload,
            "mode": payload.get("mode", "minor"),
            "signature_moves": [groove] if groove else [],
            "harmonic_rhythm": payload.get("harmonic_rhythm", "2 bars"),
            "bass_archetype": payload.get("bass_archetype", "offbeat"),
            "swing_pct": payload.get("swing_pct", 54),
            "density": payload.get(
                "density", {"chords": "med", "melody": "low", "perc": "med"}
            ),
        }
        store.seed_fingerprints([item])
        return {"ok": True}

    @app.delete("/companion/references/{fingerprint_id}")
    def delete_companion_reference(fingerprint_id: int):
        store.delete_fingerprint(fingerprint_id)
        return {"ok": True}

    @app.post("/admin/reset-taste/{user_id}")
    def reset_taste(user_id: str):
        store.reset_taste(user_id)
        return {"ok": True, "user_id": user_id}

    @app.post("/session/{session_id}/close")
    def close_session(session_id: str):
        store.close_session(session_id)
        return {"ok": True, "session_id": session_id}

    @app.post("/admin/consolidate")
    def consolidate():
        report = Path(__file__).resolve().parents[2] / "brain_report.md"
        return Consolidator(store).run(report)

    return app


app = create_app()

