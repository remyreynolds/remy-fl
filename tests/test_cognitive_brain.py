from __future__ import annotations

import json
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from app.api.server import create_app
from app.context import ContextAssembler
from app.critic import CodeCritic
from app.feedback import apply_feedback
from app.finalizer import FinalizerStage
from app.generator import GeneratorStage
from app.learning import Consolidator
from app.memory import CognitiveMemory
from app.perception import PerceptionStage
from app.pipeline import AgentPipeline
from app.planner import PlannerStage, resolve_constraints
from app.schema import (
    ContextPack,
    GenerationPlan,
    Intent,
    NoteJSON,
)
from app.llm_client import LlmResult


@pytest.fixture
def memory(tmp_path: Path) -> CognitiveMemory:
    return CognitiveMemory(tmp_path / "brain.db")


@pytest.fixture
def plan() -> GenerationPlan:
    return GenerationPlan(
        key="F minor",
        mode="aeolian",
        bpm=124,
        bars=2,
        swing_pct=56,
        progression=["Fm9", "Dbmaj7"],
        harmonic_rhythm=2,
        bass_archetype="rolling_16ths",
        rhythm_grid={"chords": "..x...x.", "bass": "x.xx"},
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
        energy_curve="lift then breath",
        influence_citation="test",
        self_check="fits",
    )


def test_memory_schema_and_seed_counts(memory: CognitiveMemory):
    # Derive expected counts from the seed files so the test stays valid as
    # the knowledge base grows (hardcoded counts went stale before).
    from app.memory import _read_seed

    assert memory.fingerprint_count() == len(_read_seed("fingerprints.json"))
    assert len(memory.moods()) >= 20
    with memory.connect() as db:
        assert db.execute("SELECT COUNT(*) FROM genre_cards").fetchone()[0] == len(
            _read_seed("genre_cards.json")
        )
        tables = {
            row[0]
            for row in db.execute(
                "SELECT name FROM sqlite_master WHERE type='table'"
            )
        }
    assert {
        "sessions",
        "generations",
        "feedback",
        "critic_verdicts",
        "taste_profiles",
        "heuristics",
        "traces",
    } <= tables


def test_perception_maps_moods_and_clarifies_once(memory: CognitiveMemory):
    stage = PerceptionStage(memory)
    intent = stage.perceive(
        "dark bouncy tech house bass in F minor at 126bpm, 8 bars"
    )
    assert intent.element == "bass"
    assert intent.subgenre == "tech_house"
    assert intent.mood_parameters["mode"] == "phrygian"
    assert intent.mood_parameters["swing_delta"] == 4
    assert intent.clarifying_question is None

    vague = stage.perceive("make me something")
    assert vague.clarifying_question
    assert vague.clarifying_question.count("?") == 1


def test_context_priority_and_cache(memory: CognitiveMemory):
    session = memory.get_session("ctx")
    session.key = "F minor"
    session.elements = {
        "chords": {"bars": ["bar1: Fm9 stab &1 &3"], "note_count": 8}
    }
    memory.save_session(session)
    intent = Intent(
        element="chords",
        subgenre="deep_house",
        constraints={"key": None, "bpm": 122, "bars": 8},
        confidence=0.9,
    )
    assembler = ContextAssembler(memory, token_budget=900)
    first = assembler.assemble(intent, session_id="ctx", request_text="deep")
    second = assembler.assemble(intent, session_id="ctx", request_text="deep")
    assert first.session["key"] == "F minor"
    assert first.taste_profile
    assert len(first.fingerprints) <= 3
    assert second.cache_hit is True


def test_context_never_truncates_session_or_taste(memory: CognitiveMemory):
    session = memory.get_session("large")
    session.summary = "x" * 5000
    memory.save_session(session)
    memory.update_taste({"banned_moves": ["y" * 5000]}, "default")
    intent = Intent(element="chords", subgenre="deep_house", confidence=1)
    pack = ContextAssembler(memory, token_budget=100).assemble(
        intent, session_id="large"
    )
    assert pack.session["session_id"] == "large"
    assert pack.taste_profile["banned_moves"]
    assert pack.fingerprints == []


def test_planner_conflict_resolution_matrix():
    intent = Intent(
        element="chords",
        subgenre="deep_house",
        constraints={"key": "D minor", "bpm": 130, "bars": 4},
        confidence=1,
    )
    context = ContextPack(
        session={
            "key": "F minor",
            "bpm": 124,
            "loop_bars": 8,
            "elements": {},
            "last_plan": {},
        },
        taste_profile={
            "preferred_keys": {"A minor": 8},
            "swing_bias": 0,
            "density_bias": 0,
        },
        fingerprints=[
            {
                "key": "G minor",
                "bpm": 126,
                "mode": "dorian",
                "swing_pct": 56,
            }
        ],
        genre_card={
            "default_key": "C minor",
            "default_bpm": 122,
            "default_mode": "aeolian",
        },
    )
    resolved = resolve_constraints(intent, context)
    assert resolved["key"] == "F minor"
    assert resolved["bpm"] == 124
    assert resolved["bars"] == 8

    no_session = context.model_copy(
        update={
            "session": {
                "key": None,
                "bpm": None,
                "loop_bars": None,
                "elements": {},
                "last_plan": {},
            }
        }
    )
    resolved = resolve_constraints(
        intent.model_copy(
            update={"constraints": {"key": None, "bpm": None, "bars": None}}
        ),
        no_session,
    )
    assert resolved["key"] == "G minor"
    assert resolved["bpm"] == 126


def test_code_critic_catches_every_violation_class(plan: GenerationPlan):
    draft = NoteJSON(
        meta={
            "bpm": 124,
            "key": "F minor",
            "ppq": 960,
            "loop_bars": 2,
            "swing_pct": 56,
        },
        tracks=[
            {
                "name": "Bass",
                "channel": 1,
                "role": "bass",
                "notes": [
                    {
                        "pitch": 54,
                        "start_tick": 0,
                        "length_ticks": 100,
                        "velocity": 90,
                    },
                    {
                        "pitch": 54,
                        "start_tick": 0,
                        "length_ticks": 100,
                        "velocity": 90,
                    },
                    {
                        "pitch": 60,
                        "start_tick": 7600,
                        "length_ticks": 200,
                        "velocity": 90,
                    },
                ],
            }
        ],
    )
    errors = CodeCritic().evaluate(draft, plan)
    prefixes = {error.split(":", 1)[0] for error in errors}
    assert {
        "scale",
        "register",
        "kick_avoidance",
        "loop_wrap",
        "duplicate",
        "velocity_variance",
        "rhythm_grid",
    } <= prefixes


def test_generator_executes_plan_and_humanizes(plan: GenerationPlan):
    draft = GeneratorStage().generate_element("bass", plan)
    assert draft.meta.ppq == 960
    assert draft.meta.key == plan.key
    track = draft.tracks[0]
    assert all(
        plan.register_map["bass"][0]
        <= note.pitch
        <= plan.register_map["bass"][1]
        for note in track.notes
    )
    assert len({note.velocity for note in track.notes}) > 1


def test_finalizer_merges_and_offers_variations(plan: GenerationPlan):
    intent = Intent(
        element="chords", subgenre="deep_house", confidence=1
    )
    draft = GeneratorStage().generate_element("chords", plan)
    finalized = FinalizerStage().finalize([draft], plan, intent)
    assert finalized["meta"]["ppq"] == 960
    assert finalized["influence_citation"] == "test"
    assert len(finalized["variation_offers"]) == 2


class AlwaysWrongGeneratorLlm:
    def has_key(self):
        return True

    def call_json(self, *, stage, system, user, tier):
        if stage == "generator":
            return LlmResult(
                ok=True,
                text=json.dumps(
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
                                "notes": [
                                    {
                                        "pitch": 54,
                                        "start_tick": 240,
                                        "length_ticks": 200,
                                        "velocity": 90,
                                    }
                                ],
                            }
                        ],
                    }
                ),
                model="fake",
            )
        return LlmResult(ok=False, error="fixture fallback", model="fake")


class CountingFailLlm:
    def __init__(self):
        self.calls = 0

    def has_key(self):
        return True

    def call_json(self, *, stage, system, user, tier):
        self.calls += 1
        return LlmResult(ok=False, error="use deterministic fallback", model="fake")


def test_revision_loop_terminates_after_two(memory: CognitiveMemory, tmp_path: Path):
    pipeline = AgentPipeline(
        llm=AlwaysWrongGeneratorLlm(),
        library=memory,
        out_dir=tmp_path,
    )
    result = pipeline.generate(
        "deep house chords in F minor, 2 bars",
        session_id="revision",
        max_retries=2,
    )
    assert result.ok
    assert "revision limit" in " ".join(result.warnings).lower()
    with memory.connect() as db:
        row = db.execute(
            "SELECT revision_count FROM generations WHERE id=?",
            (result.generation_id,),
        ).fetchone()
    assert row["revision_count"] == 2


def test_stack_never_exceeds_seven_llm_calls(
    memory: CognitiveMemory, tmp_path: Path
):
    llm = CountingFailLlm()
    pipeline = AgentPipeline(llm=llm, library=memory, out_dir=tmp_path)
    result = pipeline.generate(
        "deep house full stack in F minor, 2 bars",
        session_id="budget",
    )
    assert result.ok
    assert llm.calls <= 7
    assert {track.role for track in result.generation.tracks} == {
        "chords",
        "bass",
        "melody",
        "perc",
    }


def test_session_lock_and_feedback_reduces_density(
    memory: CognitiveMemory, tmp_path: Path
):
    pipeline = AgentPipeline(library=memory, out_dir=tmp_path)
    first = pipeline.generate(
        "tech house rolling bass in F minor at 126bpm, 4 bars",
        session_id="learn",
    )
    assert first.ok and first.generation
    first_bass = next(
        track for track in first.generation.tracks if track.role == "bass"
    )
    apply_feedback(
        memory,
        generation_id=first.generation_id,
        rating=-1,
        text="less busy",
        user_id="default",
    )
    second = pipeline.generate("make it less busy", session_id="learn")
    assert second.ok and second.generation
    second_bass = next(
        track for track in second.generation.tracks if track.role == "bass"
    )
    assert second.generation.meta.key == first.generation.meta.key
    assert second.generation.meta.bpm == first.generation.meta.bpm
    assert len(second_bass.notes) < len(first_bass.notes)


def test_offline_eight_subgenres_pass_code_critic(
    memory: CognitiveMemory, tmp_path: Path
):
    requests = [
        "deep house chords in F minor, 4 bars",
        "tech house rolling bass in A minor, 4 bars",
        "classic house chords in C major, 4 bars",
        "French house bass in D minor, 4 bars",
        "progressive melodic arp in E minor, 4 bars",
        "afro house percussion in D minor, 4 bars",
        "garage chords in A minor, 4 bars",
        "piano house melody in C major, 4 bars",
    ]
    pipeline = AgentPipeline(library=memory, out_dir=tmp_path)
    for index, request in enumerate(requests):
        result = pipeline.generate(request, session_id=f"genre-{index}")
        assert result.ok and result.generation
        with memory.connect() as db:
            row = db.execute(
                """
                SELECT validation_ok, revision_count FROM generations
                WHERE id=?
                """,
                (result.generation_id,),
            ).fetchone()
        assert row["validation_ok"] == 1
        assert row["revision_count"] <= 2


def test_consolidation_promotes_exact_threshold(
    memory: CognitiveMemory, tmp_path: Path
):
    session = memory.get_session("closed")
    session.summary = "simulated closed project"
    memory.save_session(session)
    memory.close_session("closed")
    memory.log_generation(
        request="closed session generation",
        output_json="{}",
        validation_ok=True,
        session_id="closed",
    )
    memory.add_heuristic("element=bass", "few pitches", evidence=3)
    memory.add_heuristic("element=chords", "short stabs", evidence=2)
    result = Consolidator(memory).run(tmp_path / "brain_report.md")
    assert result["promoted"] == 1
    assert result["compressed_sessions"] == 1
    assert (tmp_path / "brain_report.md").exists()
    with memory.connect() as db:
        active = db.execute(
            "SELECT COUNT(*) FROM heuristics WHERE active=1"
        ).fetchone()[0]
    assert active == 1


def test_fastapi_trace_endpoint(memory: CognitiveMemory):
    app = create_app(memory)
    with TestClient(app) as client:
        generated = client.post(
            "/generate",
            json={
                "prompt": "deep house chords in F minor, 2 bars",
                "session_id": "api",
            },
        )
        assert generated.status_code == 200
        trace_id = generated.json()["trace_id"]
        traced = client.get(f"/debug/trace/{trace_id}")
        feedback = client.post(
            "/feedback",
            json={
                "session_id": "api",
                "rating": 1,
                "text": "keep this",
            },
        )
    assert traced.status_code == 200
    assert feedback.status_code == 200
    stages = [item["stage"] for item in traced.json()["stages"]]
    assert stages[:3] == ["perception", "context", "planner"]


def test_companion_history_brain_and_reference_api(memory: CognitiveMemory):
    generation = {
        "meta": {
            "bpm": 124,
            "key": "F minor",
            "ppq": 960,
            "loop_bars": 8,
            "swing_pct": 56,
        },
        "tracks": [
            {
                "name": "Bass",
                "role": "bass",
                "notes": [
                    {
                        "pitch": 29,
                        "start_tick": 0,
                        "length_ticks": 240,
                        "velocity": 92,
                    }
                ],
            }
        ],
    }
    memory.log_generation(
        request="dark rolling tech house bass",
        output_json=json.dumps(generation),
        validation_ok=True,
        session_id="companion",
        element="bass",
        plan={
            "key": "F minor",
            "progression": ["Fm9"],
            "bass_archetype": "rolling_16ths",
            "swing_pct": 56,
        },
    )
    client = TestClient(create_app(memory))
    history = client.get(
        "/companion/history",
        params={"session_id": "companion", "q": "rolling"},
    )
    brain = client.get("/companion/brain")
    analyzed = client.post(
        "/companion/references/analyze",
        json={"reference": "Example Artist – Tech House Track"},
    )

    assert history.status_code == 200
    assert history.json()[0]["generation"]["tracks"][0]["role"] == "bass"
    assert "rolling_16ths" in history.json()[0]["plan_summary"]
    assert brain.status_code == 200
    assert {"taste", "heuristics", "report"} <= brain.json().keys()
    assert analyzed.status_code == 200
    assert analyzed.json()["fingerprint"]["subgenre"] == "tech_house"

