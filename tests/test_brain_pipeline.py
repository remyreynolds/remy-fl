"""Unit + integration tests for the Groovewright brain pipeline."""
from __future__ import annotations

import json
from pathlib import Path

import pytest

from app.converter import generation_to_midi_file, parse_midi_note_events, write_combined_and_stems
from app.db import StyleLibrary
from app.music_validator import swing_offset_ticks, validate_music, MusicValidationError
from app.pipeline import AgentPipeline
from app.prompt_loader import PromptLoadError, load_brain_prompt
from app.schema import SchemaValidationError, parse_and_validate_schema, strip_fences

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = {
    "meta": {"bpm": 124, "key": "F minor", "ppq": 960, "loop_bars": 4, "swing_pct": 56},
    "tracks": [
        {
            "name": "Chords",
            "channel": 0,
            "role": "chords",
            "notes": [
                {"pitch": 53, "start_tick": 240, "length_ticks": 300, "velocity": 92},
                {"pitch": 56, "start_tick": 240, "length_ticks": 300, "velocity": 88},
                {"pitch": 60, "start_tick": 240, "length_ticks": 300, "velocity": 90},
                {"pitch": 53, "start_tick": 1200, "length_ticks": 300, "velocity": 96},
                {"pitch": 56, "start_tick": 1200, "length_ticks": 300, "velocity": 84},
                {"pitch": 60, "start_tick": 1200, "length_ticks": 300, "velocity": 91},
                {"pitch": 53, "start_tick": 4080, "length_ticks": 300, "velocity": 90},
                {"pitch": 56, "start_tick": 4080, "length_ticks": 300, "velocity": 86},
                {"pitch": 60, "start_tick": 4080, "length_ticks": 300, "velocity": 88},
                {"pitch": 53, "start_tick": 5040, "length_ticks": 300, "velocity": 94},
                {"pitch": 56, "start_tick": 5040, "length_ticks": 300, "velocity": 80},
                {"pitch": 60, "start_tick": 5040, "length_ticks": 300, "velocity": 87},
            ],
        }
    ],
}


def test_prompt_loader_reads_file():
    text = load_brain_prompt()
    assert "Groovewright" in text
    assert "OUTPUT FORMAT" in text


def test_prompt_loader_fails_loud(tmp_path):
    with pytest.raises(PromptLoadError):
        load_brain_prompt(tmp_path / "missing.md")


def test_strip_fences():
    raw = "```json\n{\"a\":1}\n```"
    assert '"a"' in strip_fences(raw)


def test_schema_ok():
    gen = parse_and_validate_schema(FIXTURE)
    assert gen.meta.bpm == 124
    assert len(gen.tracks[0].notes) == 12


def test_schema_rejects_missing_meta():
    with pytest.raises(SchemaValidationError):
        parse_and_validate_schema({"tracks": []})


def test_schema_rejects_bad_pitch():
    bad = json.loads(json.dumps(FIXTURE))
    bad["tracks"][0]["notes"][0]["pitch"] = 200
    with pytest.raises(SchemaValidationError):
        parse_and_validate_schema(bad)


def test_schema_rejects_negative_length():
    bad = json.loads(json.dumps(FIXTURE))
    bad["tracks"][0]["notes"][0]["length_ticks"] = -1
    with pytest.raises(SchemaValidationError):
        parse_and_validate_schema(bad)


def test_music_validator_bass_register():
    data = {
        "meta": {"bpm": 126, "key": "A minor", "ppq": 960, "loop_bars": 4, "swing_pct": 52},
        "tracks": [
            {
                "name": "Bass",
                "channel": 1,
                "role": "bass",
                "notes": [{"pitch": 60, "start_tick": 240, "length_ticks": 100, "velocity": 100}],
            }
        ],
    }
    gen = parse_and_validate_schema(data)
    with pytest.raises(MusicValidationError):
        validate_music(gen)


def test_music_validator_past_loop_end():
    data = json.loads(json.dumps(FIXTURE))
    data["tracks"][0]["notes"].append(
        {"pitch": 53, "start_tick": 15000, "length_ticks": 500, "velocity": 90}
    )
    gen = parse_and_validate_schema(data)
    with pytest.raises(MusicValidationError):
        validate_music(gen)


def test_swing_math_56pct():
    # 16th = 240 ticks at 960 PPQ; odd 16ths delayed by 0.12 * 120 = 14.4 → 14
    assert swing_offset_ticks(0, 56, 960) == 0
    assert swing_offset_ticks(1, 56, 960) == 240 + 14
    assert swing_offset_ticks(2, 56, 960) == 480


def test_converter_roundtrip(tmp_path):
    gen = parse_and_validate_schema(FIXTURE)
    path = generation_to_midi_file(gen, tmp_path / "t.mid")
    events = parse_midi_note_events(path)
    original = sorted(
        (n.start_tick, n.pitch, n.length_ticks, n.velocity) for n in gen.tracks[0].notes
    )
    # round-trip lengths/starts should match
    assert len(events) == len(original)
    for (s1, p1, l1, v1), (s2, p2, l2, v2) in zip(original, sorted(events)):
        assert (s1, p1, l1, v1) == (s2, p2, l2, v2)


def test_stems_written(tmp_path):
    gen = parse_and_validate_schema(FIXTURE)
    paths = write_combined_and_stems(gen, tmp_path, "demo")
    assert paths["combined"].exists()
    assert paths["chords"].exists()


def test_seed_fingerprints(tmp_path):
    db = tmp_path / "t.db"
    lib = StyleLibrary(db)
    seeds = json.loads((ROOT / "brain" / "1-knowledge" / "fingerprints.json").read_text())
    n = lib.seed_fingerprints(seeds, replace=True)
    assert n >= 15
    hits = lib.retrieve_fingerprints(subgenre="tech house", limit=3)
    assert hits
    assert "tech" in hits[0]["subgenre"]


def test_ambiguity_asks_one_question():
    pipe = AgentPipeline(out_dir=ROOT / "samples" / "_test")
    res = pipe.generate("make me something groovy", session_id="ambig")
    assert res.clarifying_question
    assert res.ok


def test_offline_generation_and_session_lock(tmp_path):
    pipe = AgentPipeline(out_dir=tmp_path)
    # no API key → offline fallback
    r1 = pipe.generate("deep house chords in F minor, 8 bars", session_id="lock")
    assert r1.ok and r1.generation
    assert r1.midi_paths["combined"].exists()
    key1 = r1.generation.meta.key
    bpm1 = r1.generation.meta.bpm

    r2 = pipe.generate("add a bassline", session_id="lock")
    assert r2.ok and r2.generation
    assert r2.generation.meta.key == key1
    assert r2.generation.meta.bpm == bpm1
    bass = next(t for t in r2.generation.tracks if t.role == "bass")
    # roots match F (pc 5) often — F1=29 → 29%12=5
    pcs = [n.pitch % 12 for n in bass.notes]
    assert pcs.count(5) / len(pcs) >= 0.7


def test_musical_smoke_chords_and_velocity():
    gen = parse_and_validate_schema(FIXTURE)
    validate_music(gen)
    # group by start
    by_start = {}
    for n in gen.tracks[0].notes:
        by_start.setdefault(n.start_tick, []).append(n.pitch)
    for pitches in by_start.values():
        assert len(set(pitches)) >= 2
    vels = [n.velocity for n in gen.tracks[0].notes]
    assert max(vels) - min(vels) > 0


def test_retry_path_with_broken_then_ok(monkeypatch, tmp_path):
    pipe = AgentPipeline(out_dir=tmp_path)
    calls = {"n": 0}

    class FakeLlm:
        def generate_json(self, *a, **k):
            from app.llm_client import LlmResult

            calls["n"] += 1
            if calls["n"] == 1:
                return LlmResult(ok=True, text="not json at all", latency_ms=1)
            return LlmResult(ok=True, text=json.dumps(FIXTURE), latency_ms=1)

    pipe.llm = FakeLlm()
    res = pipe.generate("tech house chords in A minor, 4 bars", session_id="retry")
    assert res.ok
    assert res.attempts >= 2
    assert res.midi_paths["combined"].exists()


SUBGENRE_REQUESTS = [
    "deep house chords in F minor, 8 bars",
    "tech house rolling bass 126bpm in A minor, 4 bars",
    "chicago house piano stabs in C major, 4 bars",
    "french filter house chords in D minor, 8 bars",
    "progressive house chords in E minor, 8 bars",
    "afro house chords in D minor, 8 bars",
    "uk garage house chords in A minor, 4 bars",
    "piano house uplifting chords in C major, 8 bars",
]


@pytest.mark.integration
def test_live_or_offline_eight_subgenres(tmp_path):
    pipe = AgentPipeline(out_dir=tmp_path / "subs")
    lib = StyleLibrary(tmp_path / "s.db")
    seeds = json.loads((ROOT / "brain" / "1-knowledge" / "fingerprints.json").read_text())
    lib.seed_fingerprints(seeds, replace=True)
    pipe.library = lib

    passes = 0
    for i, req in enumerate(SUBGENRE_REQUESTS):
        res = pipe.generate(req, session_id=f"sub{i}")
        if res.ok and res.generation and res.midi_paths.get("combined"):
            validate_music(res.generation)
            passes += 1
    assert passes == 8, f"pass rate {passes}/8"
