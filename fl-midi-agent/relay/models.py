"""Relay request and note-contract models."""
from __future__ import annotations

from typing import Literal

from pydantic import BaseModel, Field, field_validator


Element = Literal["Chords", "Bass", "Melody", "Arp", "Perc"]
Role = Literal["chords", "bass", "melody", "arp", "perc"]
Mode = Literal[
    "Generate", "Fix groove", "Reharmonize", "Continue", "Bass under"
]
SoundType = Literal["pluck", "pad", "keys", "stab", "sub", "lead"]


class InputNote(BaseModel):
    pitch: int = Field(ge=0, le=127)
    start_tick: int = Field(ge=0)
    length_ticks: int = Field(gt=0)
    velocity: int = Field(ge=1, le=127)
    selected: bool = False


class GenerateRequest(BaseModel):
    prompt: str = Field(min_length=1)
    element: Element = "Chords"
    bars: int = Field(default=8, ge=1, le=16)
    session_id: str | None = None
    mode: Mode = "Generate"
    input_notes: list[InputNote] = Field(default_factory=list)
    selection_only: bool = False
    roll_ppq: int | None = Field(default=None, gt=0)
    sound_type: SoundType | None = None


class FeedbackRequest(BaseModel):
    session_id: str | None = None
    rating: int = Field(ge=-1, le=1)
    text: str = ""


class Note(BaseModel):
    pitch: int = Field(ge=0, le=127)
    start_tick: int = Field(ge=0)
    length_ticks: int = Field(gt=0)
    velocity: int = Field(ge=1, le=127)


class Track(BaseModel):
    name: str
    role: Role
    notes: list[Note]


class Meta(BaseModel):
    bpm: float = Field(gt=0)
    key: str
    ppq: int = Field(gt=0)
    loop_bars: int = Field(ge=1, le=16)
    swing_pct: float

    @field_validator("ppq")
    @classmethod
    def backend_ppq_is_960(cls, value: int) -> int:
        if value != 960:
            raise ValueError("backend contract requires ppq=960")
        return value


class NoteContract(BaseModel):
    meta: Meta
    tracks: list[Track] = Field(min_length=1)

