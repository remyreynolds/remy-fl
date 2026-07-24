#!/usr/bin/env python3
"""Ingest house_music_midi_generator_brain.pdf → heading-chunked JSON (Appendix E)."""
from __future__ import annotations

import hashlib
import json
import re
import sys
from pathlib import Path

from pypdf import PdfReader

ROOT = Path(__file__).resolve().parents[1]
PDF = ROOT / "brain/1-knowledge/guides/pdfs/house_music_midi_generator_brain.pdf"
OUT = ROOT / "brain/1-knowledge/house_brain_corpus.json"

ARCHETYPES = {
    "PROSPA": "rave_house_lift",
    "CHRIS STUSSY": "minimal_deep_tech_pocket",
    "KETTAMA": "high_impact_loop_pressure",
    "AVICII": "melody_first_progressive_house",
    "DAVID GUETTA": "pop_edm_clarity",
}

# Order matters: first match wins for more specific headings.
LAYER_RULES = [
    ("harmony", ["harmon", "chord", "voic", "progression", "borrow", "roman", "parameter card"]),
    ("melody", ["melody", "motif", "contour", "hook", "lead", "phrase", "catchiness"]),
    ("bass", ["bass engine", "bass-note", "bass ", "sub ", "collision rules", "sidechain"]),
    ("drums", ["drum", "groove", "kick", "hat", "swing", "percussion", "clap", "one-bar drum"]),
    ("arrangement", ["arrang", "section", "state sequence", "generation recipe", "failure mode",
                     "pipeline", "form", "energy", "variation budget", "pseudocode"]),
    ("validation", ["validat", "reject", "similarity", "candidate score", "constraint",
                    "originality", "weighted"]),
    ("library", ["appendix", "progression library", "drum pattern", "prompt template",
                 "chunking", "glossary", "json schema", "comparison matrix", "fast preset"]),
    ("theory", ["midi is", "velocity", "schema", "ppq", "evidence", "scope", "contents", "part i"]),
]


def infer_layer(title: str, text: str) -> str:
    blob = (title + "\n" + text[:900]).lower()
    for layer, words in LAYER_RULES:
        if any(w in blob for w in words):
            return layer
    return "theory"


def infer_archetype(title: str, path: list[str]) -> str | None:
    joined = " ".join(path + [title]).upper()
    for name, slug in ARCHETYPES.items():
        if name in joined:
            return slug
    return None


def infer_part(path: list[str], title: str, page: int) -> str:
    blob = " ".join(path + [title]).upper()
    if "APPENDIX" in blob or page >= 38:
        return "IV"
    if "PART III" in blob or 31 <= page <= 37:
        return "III"
    if "PART II" in blob or any(a in blob for a in ARCHETYPES) or 15 <= page <= 30:
        return "II"
    return "I"


def evidence_label(text: str) -> str:
    t = text.lower()
    if "listening-based" in t or "listening based" in t:
        return "listening_based"
    if "engineering" in t or "implementation" in t or "pipeline" in t:
        return "engineering_convention"
    if "derived" in t or "inferred" in t:
        return "derived"
    return "document"


def walk_outline(nodes, reader, path=None, out=None):
    if path is None:
        path = []
    if out is None:
        out = []
    idx = 0
    nodes = list(nodes)
    while idx < len(nodes):
        node = nodes[idx]
        if isinstance(node, list):
            idx += 1
            continue
        title = str(getattr(node, "title", node)).strip()
        try:
            page = reader.get_destination_page_number(node) + 1
        except Exception:
            page = 0
        out.append({"title": title, "page": page, "path": list(path)})
        if idx + 1 < len(nodes) and isinstance(nodes[idx + 1], list):
            walk_outline(nodes[idx + 1], reader, path + [title], out)
            idx += 2
        else:
            idx += 1
    return out


def main() -> int:
    if not PDF.exists():
        print(f"missing PDF: {PDF}", file=sys.stderr)
        return 1

    reader = PdfReader(str(PDF))
    pages = {i + 1: (p.extract_text() or "") for i, p in enumerate(reader.pages)}
    heads = walk_outline(reader.outline, reader)
    heads = sorted(heads, key=lambda h: (h["page"], h["title"]))
    uniq, seen = [], set()
    for h in heads:
        key = (h["title"], h["page"])
        if key in seen:
            continue
        seen.add(key)
        uniq.append(h)
    heads = uniq

    chunks = []
    for i, h in enumerate(heads):
        start = max(1, h["page"])
        end = heads[i + 1]["page"] if i + 1 < len(heads) else len(pages) + 1
        body = "\n".join(pages.get(p, "") for p in range(start, min(end + 1, len(pages) + 1))).strip()
        if not body:
            continue
        archetype = infer_archetype(h["title"], h["path"])
        part = infer_part(h["path"], h["title"], start)
        layer = infer_layer(h["title"], body)
        has_table = bool(re.search(r"\b(ID|LANE|STEP|RULE|FEATURE|STATE|ARCHETYPE|FUNCTIONS)\b", body[:900]))
        chunk_id = hashlib.sha1(f"{h['title']}|{start}".encode()).hexdigest()[:12]
        chunks.append({
            "id": chunk_id,
            "title": h["title"],
            "heading_path": h["path"] + [h["title"]],
            "text": body,
            "page_start": start,
            "page_end": max(start, end - 1) if end > start else start,
            "part": part,
            "archetype": archetype,  # null = shared theory
            "layer": layer,
            "musical_topic": h["title"],
            "evidence_label": evidence_label(body),
            "has_table": has_table,
            "source": "house_music_midi_generator_brain.pdf",
        })

    corpus = {
        "version": "1.0",
        "source_pdf": "house_music_midi_generator_brain.pdf",
        "ingested_pages": len(pages),
        "chunk_count": len(chunks),
        "chunking": "heading_outline_appendix_e",
        "archetypes": ARCHETYPES,
        "fast_preset_selection": [
            {"match": ["piano", "organ stab", "rave", "breakbeat", "prospa"],
             "archetype": "rave_house_lift", "label": "Prospa"},
            {"match": ["minimal", "deep tech", "stussy", "swing", "rolling bass", "restrained", "minimal groove"],
             "archetype": "minimal_deep_tech_pocket", "label": "Chris Stussy"},
            {"match": ["kettama", "hard", "sample-led", "pressure", "impact", "fast club"],
             "archetype": "high_impact_loop_pressure", "label": "KETTAMA"},
            {"match": ["avicii", "memorable melody", "progressive", "emotional", "festival melody", "big festival"],
             "archetype": "melody_first_progressive_house", "label": "Avicii"},
            {"match": ["guetta", "pop edm", "vocal", "build/drop", "topline", "festival drop"],
             "archetype": "pop_edm_clarity", "label": "David Guetta"},
        ],
        "hybridization_policy": {
            "primary_required": True,
            "max_borrowed_dimensions": 2,
            "never_average": True,
            "locked_primary_dimensions": ["tempo", "swing", "bass_density", "arrangement"],
        },
        "hard_rules": {
            "pipeline_order": [
                "global_setup", "form", "harmony", "kick_backbeat", "bass",
                "chord_rhythm", "melody", "hats_percussion", "section_mutation",
                "humanization", "export"
            ],
            "candidate_score_weights": {
                "harmony_fit": 1.6, "groove_fit": 1.4, "motif_coherence": 1.2,
                "voice_leading": 1.0, "phrase_shape": 0.9, "archetype_fit": 0.8,
                "playable_range": 0.6, "novelty": 0.5,
                "collision_penalty": -1.8, "reference_similarity": -2.2,
                "density_violation": -1.0
            },
            "rejection": {
                "melody_interval_similarity_max": 0.86,
                "melody_rhythm_similarity_max": 0.82,
                "drum_mask_similarity_max": 0.90,
                "bass_rhythm_similarity_max": 0.84
            },
            "midi_note_range": [0, 127],
            "bass_monophony_hard": True,
            "optional_drum_budget_hard": True,
            "default_optional_drum_hits_per_bar": 7,
        },
        "prompt_override": (
            "Generate using the parameter cards, progression libraries (Appendix A), "
            "drum pattern libraries (Appendix B), velocity bands, swing ranges, and "
            "generation recipes from the provided HOUSE MUSIC MIDI GENERATOR BRAIN document. "
            "Where the document specifies a value or range, it overrides your general knowledge."
        ),
        "chunks": chunks,
    }

    OUT.write_text(json.dumps(corpus, indent=2, ensure_ascii=False))
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes, {len(chunks)} chunks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
