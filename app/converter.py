"""JSON → MIDI via mido. Combined + per-element files for FL Studio."""
from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Tuple

import mido

from app.schema import Generation, Track


def _tempo_us(bpm: float) -> int:
    return int(round(60_000_000 / bpm))


def _track_messages(track: Track, ppq: int) -> List[mido.Message]:
    """Build absolute-time note events then convert to delta messages."""
    events: List[Tuple[int, mido.Message]] = []
    ch = track.channel + 1  # mido uses 1–16
    if track.role == "perc":
        ch = 10

    for n in track.notes:
        events.append(
            (
                n.start_tick,
                mido.Message("note_on", note=n.pitch, velocity=n.velocity, channel=ch - 1),
            )
        )
        events.append(
            (
                n.start_tick + n.length_ticks,
                mido.Message("note_off", note=n.pitch, velocity=0, channel=ch - 1),
            )
        )

    events.sort(key=lambda x: (x[0], 0 if x[1].type == "note_off" else 1))
    msgs: List[mido.Message] = []
    last = 0
    for abs_tick, msg in events:
        delta = max(0, abs_tick - last)
        msg.time = delta
        msgs.append(msg)
        last = abs_tick
    return msgs


def generation_to_midi_file(gen: Generation, path: Path) -> Path:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)

    mid = mido.MidiFile(ticks_per_beat=gen.meta.ppq)

    # tempo / meta track
    meta_track = mido.MidiTrack()
    mid.tracks.append(meta_track)
    meta_track.append(mido.MetaMessage("set_tempo", tempo=_tempo_us(gen.meta.bpm), time=0))
    meta_track.append(mido.MetaMessage("track_name", name="AI MIDI Gen", time=0))
    meta_track.append(
        mido.MetaMessage(
            "text",
            text=f"{gen.meta.key} | swing {gen.meta.swing_pct}% | {gen.meta.loop_bars} bars",
            time=0,
        )
    )

    for track in gen.tracks:
        mt = mido.MidiTrack()
        mid.tracks.append(mt)
        mt.append(mido.MetaMessage("track_name", name=track.name, time=0))
        for msg in _track_messages(track, gen.meta.ppq):
            mt.append(msg)

    mid.save(path)
    return path


def write_combined_and_stems(gen: Generation, out_dir: Path, basename: str = "generation") -> Dict[str, Path]:
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    paths: Dict[str, Path] = {}
    combined = out_dir / f"{basename}.mid"
    paths["combined"] = generation_to_midi_file(gen, combined)

    for track in gen.tracks:
        stem = Generation(meta=gen.meta, tracks=[track])
        safe = track.role.replace(" ", "_")
        p = out_dir / f"{basename}_{safe}.mid"
        paths[track.role] = generation_to_midi_file(stem, p)
    return paths


def parse_midi_note_events(path: Path) -> List[Tuple[int, int, int, int]]:
    """Return list of (abs_start, pitch, length, velocity) from a .mid file."""
    mid = mido.MidiFile(path)
    notes_on: Dict[Tuple[int, int], Tuple[int, int]] = {}
    out: List[Tuple[int, int, int, int]] = []
    abs_t = 0
    for msg in mido.merge_tracks(mid.tracks):
        abs_t += msg.time
        if msg.type == "note_on" and msg.velocity > 0:
            notes_on[(msg.channel, msg.note)] = (abs_t, msg.velocity)
        elif msg.type == "note_off" or (msg.type == "note_on" and msg.velocity == 0):
            key = (msg.channel, msg.note)
            if key in notes_on:
                start, vel = notes_on.pop(key)
                out.append((start, msg.note, abs_t - start, vel))
    out.sort()
    return out
