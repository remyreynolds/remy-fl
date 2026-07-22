from __future__ import annotations

from pathlib import Path

import pytest

from app.context import ContextAssembler
from app.critic import CodeCritic
from app.generator import GeneratorStage
from app.memory import CognitiveMemory
from app.planner import PlannerStage
from app.schema import GenerationPlan, Intent


class NoLlm:
    @staticmethod
    def has_key() -> bool:
        return False


ROLE_BY_SOUND = {
    "pluck": "chords",
    "pad": "chords",
    "keys": "chords",
    "stab": "chords",
    "sub": "bass",
    "lead": "melody",
}


def _plan(
    sound_type: str,
    constraints: dict,
    *,
    explicit_long_notes: bool = False,
) -> GenerationPlan:
    role = ROLE_BY_SOUND[sound_type]
    registers = {
        "bass": [28, 40],
        "chords": [48, 72],
        "melody": [60, 84],
        "arp": [55, 84],
        "perc": [35, 81],
    }
    registers[role] = list(constraints["register"])
    return GenerationPlan(
        key="F minor",
        mode="aeolian",
        bpm=124,
        bars=2,
        swing_pct=56,
        progression=["Fm9", "D♭maj7"],
        harmonic_rhythm=1,
        bass_archetype="rolling_16ths",
        rhythm_grid={
            "chords": ".x...x.......... per 16ths",
            "bass": ".x.x.x.x........ per 16ths",
            "melody": "motif with one-beat breath",
            "arp": "16ths",
            "perc": "16ths",
        },
        density_budget={
            "chords": "med",
            "bass": "med",
            "melody": "med",
            "arp": "med",
            "perc": "med",
        },
        register_map=registers,
        energy_curve="steady",
        influence_citation="test",
        self_check="test",
        sound_type=sound_type,
        sound_type_constraints=constraints,
        explicit_long_notes=explicit_long_notes,
    )


@pytest.mark.parametrize("sound_type", ROLE_BY_SOUND)
def test_each_sound_type_generation_passes_code_critic(
    tmp_path: Path,
    sound_type: str,
):
    memory = CognitiveMemory(tmp_path / f"{sound_type}.db")
    constraints = memory.get_sound_type(sound_type)
    plan = _plan(sound_type, constraints)
    role = ROLE_BY_SOUND[sound_type]
    draft = GeneratorStage(NoLlm()).generate_element(role, plan)
    errors = CodeCritic().evaluate(draft, plan)
    assert errors == []


def test_sub_with_melody_fails_cleanly(tmp_path: Path):
    memory = CognitiveMemory(tmp_path / "brain.db")
    intent = Intent(
        element="melody",
        sound_type="sub",
        constraints={"key": "F minor", "bpm": 124, "bars": 2},
        confidence=1,
    )
    context = ContextAssembler(memory).assemble(
        intent,
        session_id="sub-melody",
        request_text="sub melody",
    )
    with pytest.raises(ValueError, match="incompatible"):
        PlannerStage(NoLlm()).plan(intent, context)


def test_explicit_long_notes_override_pluck_gate(tmp_path: Path):
    memory = CognitiveMemory(tmp_path / "brain.db")
    constraints = memory.get_sound_type("pluck")
    plan = _plan("pluck", constraints, explicit_long_notes=True)
    draft = GeneratorStage(NoLlm()).generate_element("chords", plan)
    assert all(note.length_ticks >= 240 for note in draft.tracks[0].notes)
    assert not any(
        error.startswith("sound_type:")
        for error in CodeCritic().evaluate(draft, plan)
    )

