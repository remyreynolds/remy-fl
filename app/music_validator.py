"""Programmatic music hard-gates (brain §5.6) — never trust the LLM alone."""
from __future__ import annotations

from collections import defaultdict
from typing import List, Tuple

from app.schema import Generation

# Pitch-class maps for common keys (root name -> pcs)
ROOT_PC = {
    "C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3,
    "E": 4, "F": 5, "F#": 6, "Gb": 6, "G": 7, "G#": 8, "Ab": 8,
    "A": 9, "A#": 10, "Bb": 10, "B": 11,
}

SCALES = {
    "major": [0, 2, 4, 5, 7, 9, 11],
    "minor": [0, 2, 3, 5, 7, 8, 10],  # natural minor
    "dorian": [0, 2, 3, 5, 7, 9, 10],
    "phrygian": [0, 1, 3, 5, 7, 8, 10],
    "mixolydian": [0, 2, 4, 5, 7, 9, 10],
    "harmonicminor": [0, 2, 3, 5, 7, 8, 11],
}


class MusicValidationError(ValueError):
    def __init__(self, errors: List[str]):
        self.errors = errors
        super().__init__("; ".join(errors))


def parse_key(key: str) -> Tuple[int, str]:
    parts = key.strip().replace("_", " ").split()
    if not parts:
        return 0, "minor"
    root = parts[0]
    # handle "Fmin" / "Amin"
    mode = "minor"
    if len(parts) >= 2:
        m = parts[1].lower()
        if "dor" in m:
            mode = "dorian"
        elif "phry" in m:
            mode = "phrygian"
        elif "mix" in m:
            mode = "mixolydian"
        elif "harm" in m:
            mode = "harmonicminor"
        elif "maj" in m:
            mode = "major"
        else:
            mode = "minor"
    else:
        low = root.lower()
        for suffix, m in (("min", "minor"), ("maj", "major")):
            if low.endswith(suffix) and len(low) > len(suffix):
                root = root[: -len(suffix)]
                mode = m
                break
    # normalize root
    r = root[0].upper() + root[1:] if len(root) > 1 else root.upper()
    if r not in ROOT_PC and len(r) >= 2:
        # try flat/sharp variants already in map
        pass
    if r not in ROOT_PC:
        # fallback: first letter
        r = root[0].upper()
        if len(root) > 1 and root[1] in "#b":
            r = root[0].upper() + root[1]
    pc = ROOT_PC.get(r, 0)
    return pc, mode


def scale_pcs(root_pc: int, mode: str) -> set:
    intervals = SCALES.get(mode, SCALES["minor"])
    return {(root_pc + i) % 12 for i in intervals}


def validate_music(gen: Generation, allow_warnings_only: bool = False) -> List[str]:
    """Return list of hard errors. Empty = pass."""
    errors: List[str] = []
    meta = gen.meta
    loop_ticks = meta.loop_bars * 4 * meta.ppq
    root_pc, mode = parse_key(meta.key)
    allowed = scale_pcs(root_pc, mode)

    for track in gen.tracks:
        seen = set()
        chromatics_per_bar: dict[int, int] = defaultdict(int)

        for n in track.notes:
            if n.pitch < 21 or n.pitch > 108:
                errors.append(f"{track.name}: pitch {n.pitch} out of 21–108")
            if n.velocity < 1 or n.velocity > 127:
                errors.append(f"{track.name}: velocity {n.velocity} out of 1–127")
            if n.length_ticks <= 0:
                errors.append(f"{track.name}: non-positive length")
            if n.start_tick < 0:
                errors.append(f"{track.name}: negative start_tick")
            end = n.start_tick + n.length_ticks
            if end > loop_ticks:
                errors.append(
                    f"{track.name}: note ends at {end} past loop end {loop_ticks}"
                )

            key = (n.pitch, n.start_tick)
            if key in seen:
                errors.append(
                    f"{track.name}: duplicate note pitch={n.pitch} start={n.start_tick}"
                )
            seen.add(key)

            if track.role == "bass" and not (28 <= n.pitch <= 40):
                # E1=28, E2=40 — allow occasional octave ghost to E3 (52) as soft error
                if n.pitch > 52 or n.pitch < 28:
                    errors.append(
                        f"{track.name}: bass pitch {n.pitch} outside E1–E2 (28–40)"
                    )

            # scale check
            if track.role != "perc":
                pc = n.pitch % 12
                if pc not in allowed:
                    bar = n.start_tick // (4 * meta.ppq)
                    chromatics_per_bar[bar] += 1
                    if chromatics_per_bar[bar] > 1:
                        errors.append(
                            f"{track.name}: >1 chromatic in bar {bar} (pitch {n.pitch})"
                        )

    if errors and not allow_warnings_only:
        raise MusicValidationError(errors)
    return errors


def swing_offset_ticks(straight_16th_index: int, swing_pct: float, ppq: int = 960) -> int:
    """
    Apply MPC-style 16th swing: even 16ths stay; odd 16ths delayed.
    swing_pct 50 = straight; 56 = typical deep house.
    At 960 PPQ a 16th = 240 ticks. Delay = (swing-50)/50 * half_16th.
    """
    sixteenth = ppq // 4  # 240 at 960
    if straight_16th_index % 2 == 0:
        return straight_16th_index * sixteenth
    # late odd 16ths
    delay_frac = (swing_pct - 50.0) / 50.0  # 0 at 50%, 0.12 at 56%
    delay = int(round(delay_frac * (sixteenth / 2)))
    return straight_16th_index * sixteenth + delay
