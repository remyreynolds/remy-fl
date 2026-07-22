"""Pydantic schema for brain §6 JSON output."""
from __future__ import annotations

import json
import re
from typing import Any, Dict, List, Literal, Optional

from pydantic import BaseModel, Field, field_validator, model_validator

Role = Literal["chords", "bass", "melody", "arp", "perc"]
SoundType = Literal["pluck", "pad", "keys", "stab", "sub", "lead"]
CollabType = Literal["fix_groove", "reharmonize", "continue", "bass_under"]


class Note(BaseModel):
    pitch: int = Field(..., ge=21, le=108)
    start_tick: int = Field(..., ge=0)
    length_ticks: int = Field(..., gt=0)
    velocity: int = Field(..., ge=1, le=127)


class InputNote(BaseModel):
    pitch: int = Field(..., ge=0, le=127)
    start_tick: int = Field(..., ge=0)
    length_ticks: int = Field(..., gt=0)
    velocity: int = Field(..., ge=1, le=127)
    selected: bool = False


class Track(BaseModel):
    name: str
    channel: int = Field(..., ge=0, le=15)
    role: Role
    notes: List[Note]

    @model_validator(mode="after")
    def perc_channel(self) -> "Track":
        if self.role == "perc" and self.channel not in (9,):
            # Allow channel 9 only for perc (0-indexed MIDI channel 10)
            object.__setattr__(self, "channel", 9)
        return self


class Meta(BaseModel):
    bpm: float = Field(..., gt=40, lt=220)
    key: str
    ppq: int = Field(960, gt=0)
    loop_bars: int = Field(..., ge=1, le=16)
    swing_pct: float = Field(..., ge=50, le=70)


class Generation(BaseModel):
    meta: Meta
    tracks: List[Track] = Field(..., min_length=1)


class Intent(BaseModel):
    action: Literal[
        "generate",
        "revise",
        "analyze_reference",
        "feedback",
        "question",
        "collaborate",
    ] = "generate"
    element: Literal["chords", "bass", "melody", "arp", "perc", "stack"] = "chords"
    subgenre: Optional[
        Literal[
            "deep_house",
            "tech_house",
            "classic",
            "french",
            "prog_melodic",
            "afro",
            "garage",
            "piano_house",
        ]
    ] = None
    references: List[str] = Field(default_factory=list)
    constraints: Dict[str, Any] = Field(
        default_factory=lambda: {"key": None, "bpm": None, "bars": None}
    )
    mood_words: List[str] = Field(default_factory=list)
    mood_parameters: Dict[str, Any] = Field(default_factory=dict)
    revision_target: Optional[str] = None
    collab_type: Optional[CollabType] = None
    input_notes: List[InputNote] = Field(default_factory=list)
    sound_type: Optional[SoundType] = None
    request_text: str = ""
    confidence: float = Field(default=0.8, ge=0, le=1)
    clarifying_question: Optional[str] = None


class ContextPack(BaseModel):
    session: Dict[str, Any]
    taste_profile: Dict[str, Any]
    fingerprints: List[Dict[str, Any]] = Field(default_factory=list)
    episodes: List[Dict[str, Any]] = Field(default_factory=list)
    genre_card: Dict[str, Any] = Field(default_factory=dict)
    heuristics: List[Dict[str, Any]] = Field(default_factory=list, max_length=5)
    notes_profile: Dict[str, Any] = Field(default_factory=dict)
    sound_type_constraints: Dict[str, Any] = Field(default_factory=dict)
    token_estimate: int = 0
    cache_hit: bool = False


class GenerationPlan(BaseModel):
    key: str
    mode: str
    bpm: float
    bars: int = Field(ge=1, le=16)
    swing_pct: float = Field(ge=50, le=70)
    progression: List[str]
    harmonic_rhythm: int = Field(ge=1)
    bass_archetype: str
    rhythm_grid: Dict[str, str]
    density_budget: Dict[str, Literal["low", "med", "high"]]
    register_map: Dict[str, List[int]]
    energy_curve: str
    influence_citation: str
    self_check: str
    revision_diff: Dict[str, Any] = Field(default_factory=dict)
    collab_type: Optional[CollabType] = None
    collab_instructions: str = ""
    notes_profile: Dict[str, Any] = Field(default_factory=dict)
    sound_type: Optional[SoundType] = None
    sound_type_constraints: Dict[str, Any] = Field(default_factory=dict)
    explicit_long_notes: bool = False


class NoteJSON(BaseModel):
    meta: Meta
    tracks: List[Track] = Field(..., min_length=1)
    plan_concern: Optional[str] = None

    def as_generation(self) -> Generation:
        return Generation(meta=self.meta, tracks=self.tracks)


class Verdict(BaseModel):
    passed: bool
    code_passed: bool
    code_errors: List[str] = Field(default_factory=list)
    scores: Dict[str, int] = Field(default_factory=dict)
    revision_note: Optional[str] = None
    warnings: List[str] = Field(default_factory=list)
    attempt: int = 1


class TasteDelta(BaseModel):
    updates: Dict[str, Any] = Field(default_factory=dict)
    candidate_heuristics: List[Dict[str, Any]] = Field(default_factory=list)


FENCE_RE = re.compile(r"^```(?:json)?\s*|\s*```$", re.IGNORECASE | re.MULTILINE)


def strip_fences(text: str) -> str:
    t = text.strip()
    if "```" in t:
        t = FENCE_RE.sub("", t).strip()
        # also extract outermost object if prose remains
    start = t.find("{")
    end = t.rfind("}")
    if start >= 0 and end > start:
        t = t[start : end + 1]
    return t.strip()


class SchemaValidationError(ValueError):
    pass


def parse_and_validate_schema(raw: str | dict[str, Any]) -> Generation:
    if isinstance(raw, str):
        cleaned = strip_fences(raw)
        try:
            data = json.loads(cleaned)
        except json.JSONDecodeError as e:
            raise SchemaValidationError(f"JSON parse failed: {e}") from e
    else:
        data = raw

    if not isinstance(data, dict):
        raise SchemaValidationError("Root must be a JSON object")
    if "meta" not in data:
        raise SchemaValidationError("missing meta")
    if "tracks" not in data:
        raise SchemaValidationError("missing tracks")

    try:
        return Generation.model_validate(data)
    except Exception as e:
        raise SchemaValidationError(str(e)) from e
