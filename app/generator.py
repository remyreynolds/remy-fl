"""Stage 4: execute a fixed plan into 960-PPQ notes."""
from __future__ import annotations

import json
import random
from typing import Any

from app.music_validator import parse_key, scale_pcs
from app.schema import (
    GenerationPlan,
    Meta,
    Note,
    NoteJSON,
    Track,
)


CHANNELS = {"chords": 0, "bass": 1, "melody": 2, "arp": 3, "perc": 9}
NAMES = {role: role.title() for role in CHANNELS}


class GeneratorStage:
    def __init__(self, llm: Any | None = None):
        self.llm = llm

    def generate_element(
        self,
        element: str,
        plan: GenerationPlan,
        *,
        siblings: list[Track] | None = None,
        revision_note: str | None = None,
        input_notes: list[dict[str, Any]] | None = None,
    ) -> NoteJSON:
        siblings = siblings or []
        input_notes = input_notes or []
        generated = None
        if self.llm and getattr(self.llm, "has_key", lambda: False)():
            generated = self._llm_generate(
                element, plan, siblings, revision_note, input_notes
            )
        if generated is None:
            generated = (
                self._offline_collaborate(
                    element, plan, input_notes, revision_note
                )
                if plan.collab_type in {"fix_groove", "reharmonize"}
                else self._offline_generate(element, plan, revision_note)
            )
        if plan.collab_type:
            generated = self._enforce_collaboration(
                generated, element, plan, input_notes
            )
        else:
            generated = self._lock_and_humanize(generated, element, plan)
        return self._apply_sound_type(generated, element, plan)

    def _llm_generate(
        self,
        element: str,
        plan: GenerationPlan,
        siblings: list[Track],
        revision_note: str | None,
        input_notes: list[dict[str, Any]],
    ) -> NoteJSON | None:
        result = self.llm.call_json(
            stage="generator",
            system=(
                "Execute the supplied plan exactly. Return NoteJSON at 960 PPQ "
                "for only the requested element. Humanize swing, velocity waves, "
                "and tiny timing jitter. You may not contradict the plan; use "
                "plan_concern if needed. JSON only."
            ),
            user=json.dumps(
                {
                    "element": element,
                    "plan": plan.model_dump(),
                    "sibling_tracks": [
                        sibling.model_dump() for sibling in siblings
                    ],
                    "revision_note": revision_note,
                    "collaboration": {
                        "type": plan.collab_type,
                        "instructions": plan.collab_instructions,
                        "notes_profile": plan.notes_profile,
                        "input_notes": input_notes,
                    }
                }
            ),
            tier="strong",
        )
        if not result.ok:
            return None
        try:
            data = json.loads(result.text)
            if "meta" not in data:
                data = {
                    "meta": _meta(plan).model_dump(),
                    "tracks": [data],
                }
            return NoteJSON.model_validate(data)
        except Exception:
            return None

    def _offline_collaborate(
        self,
        element: str,
        plan: GenerationPlan,
        input_notes: list[dict[str, Any]],
        revision_note: str | None,
    ) -> NoteJSON:
        if not input_notes:
            return self._offline_generate(element, plan, revision_note)
        ordered = sorted(
            input_notes,
            key=lambda note: (int(note["start_tick"]), int(note["pitch"])),
        )
        notes = []
        if plan.collab_type == "fix_groove":
            estimated = float(plan.notes_profile.get("swing_estimate_pct", 50))
            target_swing = max(plan.swing_pct, estimated if estimated >= 54 else 0)
            velocities = sorted(int(note["velocity"]) for note in ordered)
            top_quartile = velocities[max(0, int(len(velocities) * 0.75) - 1)]
            for index, source in enumerate(ordered):
                grid_index = int(round(int(source["start_tick"]) / 240))
                grid = grid_index * 240
                delay = (
                    int(round((target_swing - 50) / 6 * 40))
                    if grid_index % 2
                    else 0
                )
                accent = int(source["velocity"]) >= top_quartile
                velocity = int(source["velocity"]) + ((index % 4) - 1) * 3
                if accent:
                    velocity = max(velocity, top_quartile)
                notes.append(
                    Note(
                        pitch=int(source["pitch"]),
                        start_tick=max(0, grid + delay),
                        length_ticks=max(1, int(int(source["length_ticks"]) * 0.9)),
                        velocity=max(1, min(127, velocity)),
                    )
                )
        else:
            root_pc, mode = parse_key(f"{plan.key.split()[0]} {plan.mode}")
            allowed = scale_pcs(root_pc, mode)
            for index, source in enumerate(ordered):
                original = int(source["pitch"])
                candidates = [
                    pitch
                    for pitch in range(max(21, original - 3), min(108, original + 3) + 1)
                    if pitch % 12 in allowed
                ]
                shifted = min(
                    candidates,
                    key=lambda pitch: (
                        abs(pitch - (original + 2)),
                        pitch == original,
                    ),
                ) if candidates else original
                notes.append(
                    Note(
                        pitch=shifted,
                        start_tick=int(source["start_tick"]),
                        length_ticks=int(source["length_ticks"]),
                        velocity=max(
                            1,
                            min(127, int(source["velocity"]) + (index % 3) - 1),
                        ),
                    )
                )
        return NoteJSON(
            meta=_meta(plan),
            tracks=[
                Track(
                    name=NAMES[element],
                    channel=CHANNELS[element],
                    role=element,
                    notes=notes,
                )
            ],
            plan_concern=(
                f"offline collaboration after revision: {revision_note}"
                if revision_note
                else None
            ),
        )

    def _enforce_collaboration(
        self,
        generated: NoteJSON,
        element: str,
        plan: GenerationPlan,
        input_notes: list[dict[str, Any]],
    ) -> NoteJSON:
        tracks = [track for track in generated.tracks if track.role == element]
        if not tracks:
            return generated
        track = tracks[0]
        ordered_input = sorted(
            input_notes,
            key=lambda note: (int(note["start_tick"]), int(note["pitch"])),
        )
        output = list(track.notes)
        if plan.collab_type == "fix_groove" and ordered_input:
            output = [
                Note(
                    pitch=int(source["pitch"]),
                    start_tick=(
                        output[index].start_tick
                        if index < len(output)
                        else int(source["start_tick"])
                    ),
                    length_ticks=(
                        output[index].length_ticks
                        if index < len(output)
                        else int(source["length_ticks"])
                    ),
                    velocity=(
                        output[index].velocity
                        if index < len(output)
                        else int(source["velocity"])
                    ),
                )
                for index, source in enumerate(ordered_input)
            ]
        elif plan.collab_type == "reharmonize" and ordered_input:
            output = [
                Note(
                    pitch=(
                        output[index].pitch
                        if index < len(output)
                        else int(source["pitch"])
                    ),
                    start_tick=int(source["start_tick"]),
                    length_ticks=int(source["length_ticks"]),
                    velocity=(
                        output[index].velocity
                        if index < len(output)
                        else int(source["velocity"])
                    ),
                )
                for index, source in enumerate(ordered_input)
            ]
        elif plan.collab_type == "continue" and ordered_input:
            boundary = int(plan.notes_profile.get("bar_span", [0, 0])[1]) * 3840
            output = [note for note in output if note.start_tick >= boundary]
        return NoteJSON(
            meta=_meta(plan),
            tracks=[
                Track(
                    name=NAMES[element],
                    channel=CHANNELS[element],
                    role=element,
                    notes=output,
                )
            ],
            plan_concern=generated.plan_concern,
        )

    def _offline_generate(
        self,
        element: str,
        plan: GenerationPlan,
        revision_note: str | None,
    ) -> NoteJSON:
        root_pc, parsed_mode = parse_key(
            f"{plan.key.split()[0]} {plan.mode}"
        )
        pcs = sorted(scale_pcs(root_pc, parsed_mode))
        density = plan.density_budget.get(element, "med")
        notes: list[Note] = []
        if element == "bass":
            root = _pitch_for_pc(root_pc, 1, plan.register_map["bass"])
            per_bar = {"low": 2, "med": 4, "high": 6}[density]
            positions = [240, 720, 1200, 1680, 2640, 3120][:per_bar]
            for bar in range(plan.bars):
                for index, offset in enumerate(positions):
                    notes.append(
                        Note(
                            pitch=(
                                root
                                if index % 4
                                else _nearest_scale_in_bounds(
                                    root + 3,
                                    pcs,
                                    plan.register_map["bass"],
                                )
                            ),
                            start_tick=bar * 3840 + offset,
                            length_ticks=150 if "rolling" in plan.bass_archetype else 300,
                            velocity=82 + ((bar + index) % 5) * 4,
                        )
                    )
        elif element == "chords":
            events = {"low": 1, "med": 2, "high": 4}[density]
            positions = [240, 1200, 2160, 3120][:events]
            register = plan.register_map["chords"]
            for bar in range(plan.bars):
                degree = bar % len(pcs)
                chord = [
                    _pitch_for_pc(pcs[(degree + step) % len(pcs)], 3, register)
                    for step in (0, 2, 4, 6)
                ]
                for event, offset in enumerate(positions):
                    for voice, pitch in enumerate(sorted(set(chord))):
                        notes.append(
                            Note(
                                pitch=pitch,
                                start_tick=bar * 3840 + offset,
                                length_ticks=360 if density != "high" else 240,
                                velocity=76 + voice * 4 + (event % 2) * 5,
                            )
                        )
        elif element in {"melody", "arp"}:
            register = plan.register_map[element]
            steps = (
                {"low": 3, "med": 5, "high": 7}[density]
                if element == "melody"
                else {"low": 8, "med": 12, "high": 16}[density]
            )
            melody_positions = [0, 960, 1920, 2880, 3360, 1440, 2400]
            for bar in range(plan.bars):
                for index in range(steps):
                    pitch = _pitch_for_pc(
                        pcs[(index + bar * 2) % len(pcs)], 4, register
                    )
                    notes.append(
                        Note(
                            pitch=pitch,
                            start_tick=bar * 3840
                            + (
                                melody_positions[index]
                                if element == "melody"
                                else index * 240
                            ),
                            length_ticks=600 if element == "melody" else 150,
                            velocity=72 + ((index * 3 + bar) % 6) * 5,
                        )
                    )
        else:
            hits = [
                (36, 0, 112),
                (36, 960, 108),
                (36, 1920, 114),
                (36, 2880, 109),
                (42, 480, 72),
                (42, 1440, 79),
                (42, 2400, 74),
                (46, 3360, 82),
            ]
            if density == "high":
                hits.extend([(38, 960, 96), (38, 2880, 100), (39, 2640, 78)])
            elif density == "low":
                hits = hits[:4]
            for bar in range(plan.bars):
                for pitch, offset, velocity in hits:
                    notes.append(
                        Note(
                            pitch=pitch,
                            start_tick=bar * 3840 + offset,
                            length_ticks=120,
                            velocity=velocity + (bar % 2),
                        )
                    )
        concern = (
            f"offline execution used after revision: {revision_note}"
            if revision_note
            else None
        )
        return NoteJSON(
            meta=_meta(plan),
            tracks=[
                Track(
                    name=NAMES[element],
                    channel=CHANNELS[element],
                    role=element,
                    notes=notes,
                )
            ],
            plan_concern=concern,
        )

    def _lock_and_humanize(
        self, generated: NoteJSON, element: str, plan: GenerationPlan
    ) -> NoteJSON:
        locked_tracks = []
        loop_end = plan.bars * 4 * 960
        for track in generated.tracks:
            if track.role != element:
                continue
            randomizer = random.Random(
                f"{plan.key}:{plan.bpm}:{element}:{len(track.notes)}"
            )
            notes = []
            for index, note in enumerate(track.notes):
                base = max(0, min(loop_end - 1, note.start_tick))
                sixteenth = int(round(base / 240))
                swing_delay = (
                    int(round((plan.swing_pct - 50) / 50 * 120))
                    if sixteenth % 2
                    else 0
                )
                jitter = 0 if element == "perc" and note.pitch == 36 else randomizer.randint(-3, 3)
                start = max(0, min(loop_end - 1, base + swing_delay + jitter))
                length = max(1, min(note.length_ticks, loop_end - start))
                velocity_wave = ((index % 4) - 1) * 2
                velocity = max(
                    1,
                    min(
                        127,
                        note.velocity
                        + velocity_wave
                        + randomizer.randint(-2, 2),
                    ),
                )
                notes.append(
                    Note(
                        pitch=note.pitch,
                        start_tick=start,
                        length_ticks=length,
                        velocity=velocity,
                    )
                )
            locked_tracks.append(
                Track(
                    name=NAMES[element],
                    channel=CHANNELS[element],
                    role=element,
                    notes=notes,
                )
            )
        return NoteJSON(
            meta=_meta(plan),
            tracks=locked_tracks,
            plan_concern=generated.plan_concern,
        )

    def _apply_sound_type(
        self,
        generated: NoteJSON,
        element: str,
        plan: GenerationPlan,
    ) -> NoteJSON:
        constraints = plan.sound_type_constraints
        if not plan.sound_type or not constraints:
            return generated
        grid_step = _sound_grid_step(element, plan)
        fixed = constraints.get("fixed_ticks")
        if plan.explicit_long_notes:
            target_length = max(grid_step, int(grid_step * 1.25))
        elif fixed:
            target_length = int(fixed)
        else:
            minimum = float(constraints.get("gate_min_pct", 40))
            maximum = float(constraints.get("gate_max_pct", minimum))
            target_length = int(round(grid_step * ((minimum + maximum) / 2) / 100))
            target_length += int(constraints.get("overlap_ticks", 0))
        register = constraints.get("register", [21, 108])
        cap = max(1, int(constraints.get("polyphony_cap", 16)))
        tracks = []
        for track in generated.tracks:
            if track.role != element:
                continue
            grouped: dict[int, list[Note]] = {}
            for note in track.notes:
                pitch = max(register[0], min(register[1], note.pitch))
                converted = Note(
                    pitch=pitch,
                    start_tick=note.start_tick,
                    length_ticks=max(
                        1,
                        min(
                            target_length,
                            plan.bars * 3840 - note.start_tick,
                        ),
                    ),
                    velocity=note.velocity,
                )
                grouped.setdefault(converted.start_tick, []).append(converted)
            notes = []
            for start in sorted(grouped):
                voices = sorted(
                    grouped[start],
                    key=lambda note: note.pitch,
                    reverse=plan.sound_type == "lead",
                )
                notes.extend(voices[:cap])
            tracks.append(
                Track(
                    name=NAMES[element],
                    channel=CHANNELS[element],
                    role=element,
                    notes=notes,
                )
            )
        return NoteJSON(
            meta=_meta(plan),
            tracks=tracks,
            plan_concern=generated.plan_concern,
        )


def _meta(plan: GenerationPlan) -> Meta:
    return Meta(
        bpm=plan.bpm,
        key=plan.key,
        ppq=960,
        loop_bars=plan.bars,
        swing_pct=plan.swing_pct,
    )


def _pitch_for_pc(pc: int, octave: int, bounds: list[int]) -> int:
    pitch = (octave + 1) * 12 + pc
    while pitch < bounds[0]:
        pitch += 12
    while pitch > bounds[1]:
        pitch -= 12
    return max(bounds[0], min(bounds[1], pitch))


def _nearest_scale_in_bounds(
    pitch: int, pcs: list[int], bounds: list[int]
) -> int:
    candidates = [
        candidate
        for candidate in range(bounds[0], bounds[1] + 1)
        if candidate % 12 in pcs
    ]
    return min(candidates, key=lambda candidate: abs(candidate - pitch))


def _sound_grid_step(element: str, plan: GenerationPlan) -> int:
    text = plan.rhythm_grid.get(element, "").lower()
    if "16th" in text:
        return 240
    if "8th" in text:
        return 480
    return 960

