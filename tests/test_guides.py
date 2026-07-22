"""Guides retrieval for music theory knowledge."""
from __future__ import annotations

from app.guides import load_all_guides, retrieve_guides, sync_guides_to_plugin_knowledge
from app.context import ContextAssembler
from app.memory import CognitiveMemory
from app.schema import Intent


def test_guides_load_and_retrieve(tmp_path):
    docs = load_all_guides()
    assert len(docs) >= 8  # curated + full PDF extracts
    ids = {d["id"] for d in docs}
    assert "edm-chord-progressions" in ids
    assert "full-music-theory-tldr" in ids
    assert "full-how-to-make-electronic-music" in ids
    assert any(d.get("source") == "full_pdf" for d in docs)

    chord_guides = retrieve_guides(element="chords", query="minor progression Fm", limit=3)
    assert chord_guides
    assert any("chord" in g["id"] or "theory" in g["id"] for g in chord_guides)

    melody_guides = retrieve_guides(element="melody", query="write a topline", limit=3)
    assert any("melody" in g["id"] or "5-steps" in g["id"] for g in melody_guides)


def test_context_includes_theory_guides(tmp_path):
    memory = CognitiveMemory(tmp_path / "style.db")
    memory.seed_defaults()
    pack = ContextAssembler(memory).assemble(
        Intent(action="generate", element="chords", subgenre="deep_house"),
        session_id="guides-test",
        request_text="deep house chords in F minor",
        reuse_cache=False,
    )
    assert pack.theory_guides
    dumped = pack.model_dump()
    assert "theory_guides" in dumped
    assert any(g.get("excerpt") for g in pack.theory_guides)


def test_sync_guides_to_plugin_knowledge(tmp_path, monkeypatch):
    monkeypatch.setenv("HOME", str(tmp_path))
    dest = sync_guides_to_plugin_knowledge()
    assert dest.exists()
    assert (dest / "edm-chord-progressions.md").exists()
    assert (dest / "theory-for-midi.md").exists()
    assert (dest / "full-music-theory-tldr.md").exists()
    assert (dest / "pdfs" / "Music-Theory-TLDR.pdf").exists()
    assert (dest / "pdfs" / "How-to-Make-Electronic-Music.pdf").exists()
