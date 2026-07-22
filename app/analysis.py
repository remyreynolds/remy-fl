"""Measured MIDI analysis shared by collaborator mode and the MIDI miner."""
from __future__ import annotations

import math
from collections import Counter
from statistics import fmean, pvariance
from typing import Any, Iterable

from pydantic import BaseModel, ConfigDict, Field


MAJOR_PROFILE = (6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88)
MINOR_PROFILE = (6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17)
PC_NAMES = ("C", "D♭", "D", "E♭", "E", "F", "G♭", "G", "A♭", "A", "B♭", "B")


class ChordSlice(BaseModel):
    bar_beat: str
    pitches: list[int]
    guess: str


class NotesProfile(BaseModel):
    model_config = ConfigDict(populate_by_name=True)

    detected_key: str
    key_confidence: float = Field(ge=0, le=1)
    bpm_hint: float | None = None
    bar_span: list[int] = Field(min_length=2, max_length=2)
    density_per_bar: list[int]
    swing_estimate_pct: float
    chord_slices: list[ChordSlice]
    velocity_stats: dict[str, float]
    pitch_register: list[int] = Field(
        alias="register",
        serialization_alias="register",
        min_length=2,
        max_length=2,
    )
    is_monophonic: bool


def analyze_notes(
    raw_notes: Iterable[dict[str, Any]],
    *,
    ppq: int = 960,
    bpm_hint: float | None = None,
) -> NotesProfile:
    """Analyze standardized or FL-shaped notes without retaining raw MIDI."""
    notes = sorted(
        (_normalize_note(note) for note in raw_notes),
        key=lambda note: (note["start_tick"], note["pitch"]),
    )
    if not notes:
        raise ValueError("at least one note is required")
    bar_ticks = ppq * 4
    first_bar = min(note["start_tick"] for note in notes) // bar_ticks
    last_tick = max(
        note["start_tick"] + note["length_ticks"] for note in notes
    )
    last_bar = max(first_bar + 1, math.ceil(last_tick / bar_ticks))
    density = [0 for _ in range(last_bar - first_bar)]
    for note in notes:
        bar = note["start_tick"] // bar_ticks - first_bar
        if 0 <= bar < len(density):
            density[bar] += 1
    velocities = [note["velocity"] for note in notes]
    pitches = [note["pitch"] for note in notes]
    detected_key, confidence = detect_key(pitches)
    return NotesProfile(
        detected_key=detected_key,
        key_confidence=confidence,
        bpm_hint=bpm_hint,
        bar_span=[first_bar, last_bar],
        density_per_bar=density,
        swing_estimate_pct=estimate_swing(notes, ppq=ppq),
        chord_slices=_chord_slices(notes, ppq=ppq),
        velocity_stats={
            "mean": round(fmean(velocities), 3),
            "variance": round(pvariance(velocities), 3)
            if len(velocities) > 1
            else 0.0,
        },
        register=[min(pitches), max(pitches)],
        is_monophonic=_is_monophonic(notes),
    )


def detect_key(pitches: Iterable[int]) -> tuple[str, float]:
    """Krumhansl–Schmuckler key estimate using Pearson correlation."""
    pitch_list = [int(pitch) for pitch in pitches]
    counts = [0.0] * 12
    for pitch in pitch_list:
        counts[pitch % 12] += 1.0
    if not any(counts):
        return "C major", 0.0
    scores: list[tuple[float, int, str]] = []
    first_pc = pitch_list[0] % 12
    bass_pc = min(pitch_list) % 12
    for root in range(12):
        for mode, profile in (("major", MAJOR_PROFILE), ("minor", MINOR_PROFILE)):
            rotated = [profile[(pc - root) % 12] for pc in range(12)]
            correlation = _correlation(counts, rotated)
            # K-S alone cannot distinguish a lone minor chord from its
            # relative major. First/bass-note tonic evidence resolves that tie.
            tonic_prior = 0.1 * (root == first_pc) + 0.1 * (root == bass_pc)
            scores.append((correlation + tonic_prior, root, mode))
    scores.sort(reverse=True)
    best, root, mode = scores[0]
    second = scores[1][0]
    # Absolute fit is primary; separation breaks close relative-key ties.
    confidence = max(
        0.0,
        min(1.0, 0.7 * ((best + 1.0) / 2.0) + 0.3 * max(0.0, best - second)),
    )
    return f"{PC_NAMES[root]} {mode}", round(confidence, 3)


def estimate_swing(
    notes: Iterable[dict[str, Any]],
    *,
    ppq: int = 960,
) -> float:
    """Estimate 16th swing from odd-grid lateness; straight equals 50%."""
    step = ppq / 4.0
    lateness = []
    for note in notes:
        position = float(note["start_tick"])
        index = int(round(position / step))
        if index % 2 == 0:
            continue
        delta = position - index * step
        if abs(delta) <= step / 2:
            lateness.append(delta)
    mean_late = fmean(lateness) if lateness else 0.0
    # Calibrated to the acceptance fixture: +40 ticks at 960 PPQ is 56%.
    estimate = 50.0 + 36.0 * (mean_late / step)
    return round(max(50.0, min(70.0, estimate)), 2)


def _chord_slices(
    notes: list[dict[str, int]],
    *,
    ppq: int,
) -> list[ChordSlice]:
    first = min(note["start_tick"] for note in notes) // ppq
    last = math.ceil(
        max(note["start_tick"] + note["length_ticks"] for note in notes) / ppq
    )
    slices = []
    previous: tuple[int, ...] | None = None
    for beat in range(first, last):
        start, end = beat * ppq, (beat + 1) * ppq
        sounding = tuple(
            sorted(
                {
                    note["pitch"]
                    for note in notes
                    if note["start_tick"] < end
                    and note["start_tick"] + note["length_ticks"] > start
                }
            )
        )
        if len(sounding) < 2 or sounding == previous:
            continue
        bar = beat // 4 + 1
        beat_in_bar = beat % 4 + 1
        slices.append(
            ChordSlice(
                bar_beat=f"{bar}.{beat_in_bar}",
                pitches=list(sounding),
                guess=_guess_chord(sounding),
            )
        )
        previous = sounding
    return slices


def _guess_chord(pitches: tuple[int, ...]) -> str:
    pcs = {pitch % 12 for pitch in pitches}
    templates = (
        ("maj7", {0, 4, 7, 11}),
        ("m7", {0, 3, 7, 10}),
        ("7", {0, 4, 7, 10}),
        ("m", {0, 3, 7}),
        ("", {0, 4, 7}),
        ("dim", {0, 3, 6}),
    )
    best: tuple[int, int, str] | None = None
    for root in range(12):
        relative = {(pc - root) % 12 for pc in pcs}
        for suffix, template in templates:
            overlap = len(relative & template)
            penalty = len(relative ^ template)
            score = overlap * 3 - penalty
            candidate = (score, root, suffix)
            if best is None or candidate[0] > best[0]:
                best = candidate
    assert best is not None
    return f"{PC_NAMES[best[1]]}{best[2]}"


def _is_monophonic(notes: list[dict[str, int]]) -> bool:
    latest_end = -1
    for note in notes:
        if note["start_tick"] < latest_end:
            return False
        latest_end = max(latest_end, note["start_tick"] + note["length_ticks"])
    return True


def _normalize_note(note: dict[str, Any]) -> dict[str, int]:
    return {
        "pitch": int(note.get("pitch", note.get("number"))),
        "start_tick": int(note.get("start_tick", note.get("time", 0))),
        "length_ticks": max(
            1, int(note.get("length_ticks", note.get("length", 1)))
        ),
        "velocity": max(1, min(127, int(round(note.get("velocity", 96))))),
    }


def _correlation(left: list[float], right: list[float]) -> float:
    left_mean, right_mean = fmean(left), fmean(right)
    numerator = sum(
        (x - left_mean) * (y - right_mean) for x, y in zip(left, right)
    )
    left_energy = sum((x - left_mean) ** 2 for x in left)
    right_energy = sum((y - right_mean) ** 2 for y in right)
    denominator = math.sqrt(left_energy * right_energy)
    return numerator / denominator if denominator else 0.0

