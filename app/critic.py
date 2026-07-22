"""Stage 5: deterministic hard checks followed by optional musical scoring."""
from __future__ import annotations

import json
from collections import Counter, defaultdict
from typing import Any

from app.music_validator import parse_key, scale_pcs
from app.schema import GenerationPlan, NoteJSON, Track, Verdict


class CodeCritic:
    def evaluate(
        self,
        draft: NoteJSON,
        plan: GenerationPlan,
        siblings: list[Track] | None = None,
        input_notes: list[dict[str, Any]] | None = None,
    ) -> list[str]:
        errors: list[str] = []
        siblings = siblings or []
        input_notes = input_notes or []
        root = plan.key.split()[0]
        root_pc, mode = parse_key(f"{root} {plan.mode}")
        allowed = scale_pcs(root_pc, mode)
        loop_end = plan.bars * 4 * 960

        for track in draft.tracks:
            seen: set[tuple[int, int]] = set()
            velocities: set[int] = set()
            register = plan.register_map.get(track.role, [21, 108])
            for note in track.notes:
                if (
                    plan.collab_type != "fix_groove"
                    and track.role != "perc"
                    and note.pitch % 12 not in allowed
                ):
                    errors.append(
                        f"scale:{track.role} pitch {note.pitch} is outside {plan.key}/{plan.mode}"
                    )
                if (
                    plan.collab_type != "fix_groove"
                    and not register[0] <= note.pitch <= register[1]
                ):
                    errors.append(
                        f"register:{track.role} pitch {note.pitch} outside {register}"
                    )
                if note.start_tick < 0 or note.start_tick + note.length_ticks > loop_end:
                    errors.append(
                        f"loop_wrap:{track.role} note {note.pitch}@{note.start_tick}"
                    )
                duplicate = (note.pitch, note.start_tick)
                if duplicate in seen:
                    errors.append(
                        f"duplicate:{track.role} pitch {note.pitch}@{note.start_tick}"
                    )
                seen.add(duplicate)
                velocities.add(note.velocity)
                if (
                    track.role == "bass"
                    and "sub_hold" not in plan.bass_archetype
                    and _near_grid(note.start_tick, 960, 24)
                ):
                    errors.append(
                        f"kick_avoidance:bass onset {note.start_tick} hits kick downbeat"
                    )
            if len(track.notes) > 1 and len(velocities) <= 1:
                errors.append(f"velocity_variance:{track.role} is flat")
            if plan.collab_type not in {"fix_groove", "reharmonize"}:
                adherence = _rhythm_adherence(track, plan)
                if adherence < 0.8:
                    errors.append(
                        f"rhythm_grid:{track.role} adherence {adherence:.0%} < 80%"
                    )
            errors.extend(_sound_type_errors(track, plan))
        if plan.collab_type == "fix_groove":
            expected = Counter(int(note["pitch"]) for note in input_notes)
            actual = Counter(
                note.pitch for track in draft.tracks for note in track.notes
            )
            if actual != expected:
                errors.append("collab_fix:pitch multiset changed")
        elif plan.collab_type == "reharmonize":
            expected = Counter(int(note["start_tick"]) for note in input_notes)
            actual = Counter(
                note.start_tick for track in draft.tracks for note in track.notes
            )
            if actual != expected:
                errors.append("collab_reharmonize:onset multiset changed")
        elif plan.collab_type == "continue" and input_notes:
            boundary = int(plan.notes_profile.get("bar_span", [0, 0])[1]) * 3840
            if any(
                note.start_tick < boundary
                for track in draft.tracks
                for note in track.notes
            ):
                errors.append(
                    f"collab_continue:output overlaps input before {boundary}"
                )
        return list(dict.fromkeys(errors))


class CriticStage:
    def __init__(self, llm: Any | None = None):
        self.llm = llm
        self.code = CodeCritic()

    def review(
        self,
        draft: NoteJSON,
        plan: GenerationPlan,
        *,
        attempt: int,
        siblings: list[Track] | None = None,
        input_notes: list[dict[str, Any]] | None = None,
    ) -> Verdict:
        errors = self.code.evaluate(
            draft, plan, siblings, input_notes=input_notes
        )
        if errors:
            return Verdict(
                passed=False,
                code_passed=False,
                code_errors=errors,
                revision_note=_code_revision_note(errors),
                attempt=attempt,
            )

        scores = self._musical_scores(draft, plan)
        low_axes = {axis: score for axis, score in scores.items() if score <= 2}
        if low_axes:
            note = self._musical_revision_note(draft, plan, low_axes)
            return Verdict(
                passed=False,
                code_passed=True,
                scores=scores,
                revision_note=note,
                attempt=attempt,
            )
        return Verdict(
            passed=True,
            code_passed=True,
            scores=scores,
            attempt=attempt,
        )

    def _musical_scores(
        self, draft: NoteJSON, plan: GenerationPlan
    ) -> dict[str, int]:
        fallback = {
            "groove_pocket": 4,
            "harmonic_color": 4,
            "repetition_fatigue": 4,
            "genre_authenticity": 4,
        }
        if not self.llm or not getattr(self.llm, "has_key", lambda: False)():
            return fallback
        result = self.llm.call_json(
            stage="musical_critic",
            system=(
                "Score groove_pocket, harmonic_color, repetition_fatigue, and "
                "genre_authenticity from 1 to 5. Return {scores:{...},"
                "revision_note:string|null}. JSON only."
            ),
            user=json.dumps(
                {"plan": plan.model_dump(), "draft": draft.model_dump()}
            ),
            tier="mid",
        )
        if not result.ok:
            return fallback
        try:
            data = json.loads(result.text)
            values = data.get("scores", data)
            return {
                key: max(1, min(5, int(values[key])))
                for key in fallback
            }
        except Exception:
            return fallback

    def _musical_revision_note(
        self,
        draft: NoteJSON,
        plan: GenerationPlan,
        low_axes: dict[str, int],
    ) -> str:
        if self.llm and getattr(self.llm, "has_key", lambda: False)():
            result = self.llm.call_json(
                stage="musical_critic_revision",
                system=(
                    "Return one concrete revision_note naming bars/onsets and "
                    "specific note operations. JSON only."
                ),
                user=json.dumps(
                    {
                        "low_axes": low_axes,
                        "plan": plan.model_dump(),
                        "draft": draft.model_dump(),
                    }
                ),
                tier="mid",
            )
            if result.ok:
                try:
                    return str(json.loads(result.text)["revision_note"])
                except Exception:
                    pass
        axes = ", ".join(low_axes)
        return (
            f"Improve {axes}: preserve the plan, move the weakest repeated "
            "onset in bar 3 to the '&' of beat 2 and vary its velocity."
        )


def _rhythm_adherence(track: Track, plan: GenerationPlan) -> float:
    if not track.notes:
        return 0.0
    expected = {
        "chords": [240, 1200],
        "bass": [240, 720, 1200, 1680],
        "melody": [0, 960, 1920, 2880],
        "arp": [index * 240 for index in range(16)],
        "perc": [0, 480, 960, 1440, 1920, 2400, 2880, 3360],
    }.get(track.role, [])
    grid_text = plan.rhythm_grid.get(track.role, "")
    parsed = _parse_grid(grid_text)
    if parsed:
        expected = parsed
    density = plan.density_budget.get(track.role, "med")
    if track.role == "bass" and density == "low":
        expected = expected[:2]
    if track.role == "chords" and density == "low":
        expected = expected[:1]
    if track.role == "melody":
        expected = [0, 960, 1920] if density == "low" else [0, 960, 1920, 2880]
    if track.role == "arp":
        expected = expected[: {"low": 8, "med": 12, "high": 16}[density]]
    if track.role == "perc":
        expected = expected[:4] if density == "low" else expected
    planned = 0
    hit = 0
    onsets = {note.start_tick for note in track.notes}
    for bar in range(plan.bars):
        for offset in expected:
            planned += 1
            target = bar * 3840 + offset
            if any(abs(onset - target) <= 30 for onset in onsets):
                hit += 1
    return hit / planned if planned else 1.0


def _near_grid(value: int, grid: int, tolerance: int) -> bool:
    remainder = value % grid
    return remainder <= tolerance or grid - remainder <= tolerance


def _parse_grid(value: str) -> list[int]:
    low = value.lower()
    if "16th" in low:
        step = 240
    elif "8th" in low:
        step = 480
    else:
        return []
    pattern = "".join(character for character in low.split("per")[0] if character in ".x")
    if not pattern:
        return []
    return [index * step for index, character in enumerate(pattern[:16]) if character == "x"]


def _code_revision_note(errors: list[str]) -> str:
    return "Fix code-critic violations exactly: " + "; ".join(errors[:8])


def _sound_type_errors(
    track: Track,
    plan: GenerationPlan,
) -> list[str]:
    constraints = plan.sound_type_constraints
    if not plan.sound_type or not constraints:
        return []
    errors = []
    register = constraints.get("register", [21, 108])
    for note in track.notes:
        if not register[0] <= note.pitch <= register[1]:
            errors.append(
                f"sound_type:{plan.sound_type} pitch {note.pitch} outside {register}"
            )
    cap = int(constraints.get("polyphony_cap", 16))
    polyphony = _max_polyphony(track)
    if polyphony > cap:
        errors.append(
            f"sound_type:{plan.sound_type} polyphony {polyphony} exceeds {cap}"
        )
    if not plan.explicit_long_notes:
        step = _sound_grid_step(track.role, plan)
        fixed = constraints.get("fixed_ticks")
        for note in track.notes:
            if fixed and abs(note.length_ticks - int(fixed)) > max(
                10, int(fixed * 0.1)
            ):
                errors.append(
                    f"sound_type:{plan.sound_type} gate {note.length_ticks} "
                    f"must be {fixed} ticks"
                )
                continue
            if fixed:
                continue
            gate = note.length_ticks / step * 100
            minimum = float(constraints.get("gate_min_pct", 0)) - 10
            maximum = float(constraints.get("gate_max_pct", 100)) + 10
            overlap = int(constraints.get("overlap_ticks", 0))
            maximum += overlap / step * 100
            if not minimum <= gate <= maximum:
                errors.append(
                    f"sound_type:{plan.sound_type} gate {gate:.0f}% "
                    f"outside {minimum:.0f}-{maximum:.0f}%"
                )
    return list(dict.fromkeys(errors))


def _max_polyphony(track: Track) -> int:
    onsets: Counter[int] = Counter()
    for note in track.notes:
        onsets[note.start_tick] += 1
    return max(onsets.values(), default=0)


def _sound_grid_step(role: str, plan: GenerationPlan) -> int:
    text = plan.rhythm_grid.get(role, "").lower()
    if "16th" in text:
        return 240
    if "8th" in text:
        return 480
    return 960

