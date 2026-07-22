"""Optional live-provider acceptance tests."""
from __future__ import annotations

import os

import pytest

from app.memory import CognitiveMemory
from app.pipeline import AgentPipeline


LIVE_AVAILABLE = bool(
    os.environ.get("ANTHROPIC_API_KEY") or os.environ.get("OPENAI_API_KEY")
)


@pytest.mark.live
@pytest.mark.skipif(not LIVE_AVAILABLE, reason="No live LLM API key configured")
def test_live_eight_subgenres_within_two_revisions(tmp_path):
    requests = [
        "deep house chords in F minor, 4 bars",
        "tech house bass in A minor, 4 bars",
        "classic house chords in C major, 4 bars",
        "French house bass in D minor, 4 bars",
        "progressive melodic arp in E minor, 4 bars",
        "afro house percussion, 4 bars",
        "garage chords in A minor, 4 bars",
        "piano house melody in C major, 4 bars",
    ]
    memory = CognitiveMemory(tmp_path / "live.db")
    pipeline = AgentPipeline(library=memory, out_dir=tmp_path / "midi")
    for index, request in enumerate(requests):
        result = pipeline.generate(request, session_id=f"live-{index}")
        assert result.ok and result.generation
        with memory.connect() as db:
            revisions = db.execute(
                "SELECT revision_count FROM generations WHERE id=?",
                (result.generation_id,),
            ).fetchone()["revision_count"]
        assert revisions <= 2

