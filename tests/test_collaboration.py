from __future__ import annotations

from pathlib import Path
from statistics import pvariance

from app.analysis import analyze_notes
from app.context import ContextAssembler
from app.critic import CodeCritic
from app.memory import CognitiveMemory
from app.perception import PerceptionStage
from app.pipeline import AgentPipeline
from app.schema import GenerationPlan, NoteJSON


class NoLlm:
    @staticmethod
    def has_key() -> bool:
        return False


def _input_chord() -> list[dict]:
    return [
        {
            "pitch": pitch,
            "start_tick": start,
            "length_ticks": 720,
            "velocity": 96,
            "selected": True,
        }
        for start in (0, 240, 480, 720)
        for pitch in (53, 56, 60, 63)
    ]


def _plan(collab_type: str) -> GenerationPlan:
    return GenerationPlan(
        key="F minor",
        mode="aeolian",
        bpm=124,
        bars=2,
        swing_pct=56,
        progression=["Fm7", "D♭maj7"],
        harmonic_rhythm=1,
        bass_archetype="rolling_16ths",
        rhythm_grid={
            "chords": "xxxx per 16ths",
            "bass": ".x.x.x.x per 16ths",
        },
        density_budget={
            "chords": "med",
            "bass": "med",
            "melody": "low",
            "arp": "med",
            "perc": "med",
        },
        register_map={
            "bass": [28, 40],
            "chords": [48, 72],
            "melody": [60, 84],
            "arp": [55, 84],
            "perc": [35, 81],
        },
        energy_curve="steady",
        influence_citation="test",
        self_check="test",
        collab_type=collab_type,
        notes_profile={"bar_span": [0, 1], "swing_estimate_pct": 50},
    )


def test_perception_collaborator_skips_clarification(tmp_path: Path):
    memory = CognitiveMemory(tmp_path / "brain.db")
    intent = PerceptionStage(memory).perceive(
        "fix my groove",
        mode="Fix groove",
        input_notes=_input_chord(),
    )
    assert intent.action == "collaborate"
    assert intent.collab_type == "fix_groove"
    assert intent.clarifying_question is None
    context = ContextAssembler(memory).assemble(
        intent,
        session_id="collab",
        request_text="fix my groove",
    )
    assert context.notes_profile["detected_key"] == "F minor"


def test_fix_groove_critic_rejects_changed_pitch_multiset():
    input_notes = _input_chord()[:4]
    output = [
        {**note, "pitch": note["pitch"] + (1 if index == 0 else 0)}
        for index, note in enumerate(input_notes)
    ]
    draft = NoteJSON.model_validate(
        {
            "meta": {
                "bpm": 124,
                "key": "F minor",
                "ppq": 960,
                "loop_bars": 2,
                "swing_pct": 56,
            },
            "tracks": [
                {
                    "name": "Chords",
                    "channel": 0,
                    "role": "chords",
                    "notes": output,
                }
            ],
        }
    )
    errors = CodeCritic().evaluate(
        draft, _plan("fix_groove"), input_notes=input_notes
    )
    assert "collab_fix:pitch multiset changed" in errors


def test_reharmonize_critic_rejects_changed_onset_multiset():
    input_notes = _input_chord()[:4]
    output = [
        {
            **note,
            "pitch": note["pitch"] + 2,
            "start_tick": note["start_tick"] + (10 if index == 0 else 0),
        }
        for index, note in enumerate(input_notes)
    ]
    draft = NoteJSON.model_validate(
        {
            "meta": {
                "bpm": 124,
                "key": "F minor",
                "ppq": 960,
                "loop_bars": 2,
                "swing_pct": 56,
            },
            "tracks": [
                {
                    "name": "Chords",
                    "channel": 0,
                    "role": "chords",
                    "notes": output,
                }
            ],
        }
    )
    errors = CodeCritic().evaluate(
        draft, _plan("reharmonize"), input_notes=input_notes
    )
    assert "collab_reharmonize:onset multiset changed" in errors


def test_robotic_chords_fix_adds_velocity_and_swing(
    tmp_path: Path,
):
    memory = CognitiveMemory(tmp_path / "brain.db")
    input_notes = _input_chord()
    result = AgentPipeline(
        llm=NoLlm(),
        library=memory,
        out_dir=tmp_path / "midi",
    ).generate(
        "fix my groove, deep house chords, 1 bar",
        session_id="robotic",
        mode="Fix groove",
        input_notes=input_notes,
    )
    assert result.ok, result.errors
    assert result.generation is not None
    output = result.generation.tracks[0].notes
    assert pvariance(note.velocity for note in output) > 0
    profile = analyze_notes([note.model_dump() for note in output])
    assert 54 <= profile.swing_estimate_pct <= 58
    assert sorted(note.pitch for note in output) == sorted(
        note["pitch"] for note in input_notes
    )

