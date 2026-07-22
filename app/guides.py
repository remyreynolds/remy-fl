"""Load and retrieve music theory guides from brain/1-knowledge/guides.

Shared corpus for:
- Python brain generator / companion (ContextAssembler → Planner → Generator)
- Plugin chatbot / MIDI generation (KnowledgeBase synced from this folder)

Chatbot may ALSO use Claude's trained knowledge on top of these docs.
"""
from __future__ import annotations

import re
import shutil
from pathlib import Path
from typing import Iterable

REPO_ROOT = Path(__file__).resolve().parents[1]
GUIDES_DIR = REPO_ROOT / "brain" / "1-knowledge" / "guides"
PDFS_DIR = GUIDES_DIR / "pdfs"
FULL_DIR = GUIDES_DIR / "full"

# Element / keyword → preferred guide stems (without .md)
_GUIDE_PRIORITY = {
    "chords": [
        "edm-chord-progressions",
        "full-edm-chord-progressions",
        "theory-for-midi",
        "full-music-theory-tldr",
        "electronic-midi-workflow",
    ],
    "bass": [
        "theory-for-midi",
        "full-music-theory-tldr",
        "electronic-midi-workflow",
        "edm-chord-progressions",
    ],
    "melody": [
        "melody-5-steps",
        "full-5-steps-melody",
        "theory-for-midi",
        "full-music-theory-tldr",
        "electronic-midi-workflow",
    ],
    "arp": [
        "melody-5-steps",
        "full-5-steps-melody",
        "theory-for-midi",
        "electronic-midi-workflow",
    ],
    "perc": ["electronic-midi-workflow", "theory-for-midi", "full-how-to-make-electronic-music"],
    "stack": [
        "electronic-midi-workflow",
        "edm-chord-progressions",
        "full-edm-chord-progressions",
        "theory-for-midi",
        "full-music-theory-tldr",
        "melody-5-steps",
        "full-how-to-make-electronic-music",
    ],
}


def guides_dir() -> Path:
    return GUIDES_DIR


def list_guides() -> list[Path]:
    """All agent-readable markdown guides (curated + full PDF extracts)."""
    if not GUIDES_DIR.exists():
        return []
    paths: list[Path] = []
    for path in GUIDES_DIR.rglob("*.md"):
        name = path.name
        if name.startswith("00-") or name.startswith("."):
            continue
        paths.append(path)
    return sorted(paths, key=lambda p: (0 if p.parent == GUIDES_DIR else 1, p.name))


def load_all_guides() -> list[dict[str, str]]:
    docs = []
    for path in list_guides():
        text = path.read_text(encoding="utf-8").strip()
        if not text:
            continue
        docs.append(
            {
                "id": path.stem,
                "title": _title(text, path.stem),
                "body": text,
                "path": str(path.relative_to(GUIDES_DIR)),
                "source": "full_pdf" if path.parent == FULL_DIR else "curated",
            }
        )
    return docs


def retrieve_guides(
    *,
    element: str | None = None,
    query: str = "",
    limit: int = 4,
    max_chars: int = 9000,
) -> list[dict[str, str]]:
    """Pick the most relevant guide excerpts for this generation/chat turn.

    Same ranking is used by the Python brain and (via synced files) the plugin
    KnowledgeBase so generator + chatbot share one corpus.
    """
    docs = load_all_guides()
    if not docs:
        return []
    preferred = _GUIDE_PRIORITY.get((element or "stack").lower(), _GUIDE_PRIORITY["stack"])
    keywords = _keywords(query)
    ranked: list[tuple[int, dict[str, str]]] = []
    for doc in docs:
        score = 0
        if doc["id"] in preferred:
            score += 30 - preferred.index(doc["id"])
        # Prefer curated cheat sheets slightly for token efficiency, but keep full PDFs.
        if doc.get("source") == "curated":
            score += 2
        body_l = doc["body"].lower()
        title_l = doc["title"].lower()
        for kw in keywords:
            if kw in title_l:
                score += 6
            score += min(8, body_l.count(kw))
        ranked.append((score, doc))
    ranked.sort(key=lambda item: item[0], reverse=True)

    selected: list[dict[str, str]] = []
    used = 0
    budget_tokens = max_chars // 4
    for score, doc in ranked:
        if score <= 0 and selected:
            continue
        per_doc = max(900, max_chars // max(1, limit))
        excerpt = _excerpt(doc["body"], keywords, budget=per_doc)
        payload = {
            "id": doc["id"],
            "title": doc["title"],
            "excerpt": excerpt,
            "source": doc.get("source", "curated"),
            "path": doc.get("path", ""),
        }
        size = len(excerpt) // 4
        if used + size > budget_tokens and selected:
            break
        selected.append(payload)
        used += size
        if len(selected) >= limit:
            break
    return selected


def sync_guides_to_plugin_knowledge() -> Path:
    """Copy shared guide corpus into the plugin chatbot knowledge folder.

    Includes curated .md, full PDF text extracts, and original PDFs so both
    surfaces read the same brain knowledge.
    """
    dest = (
        Path.home()
        / "Library"
        / "Application Support"
        / "AIMidiGen"
        / "knowledge"
    )
    dest.mkdir(parents=True, exist_ok=True)
    pdf_dest = dest / "pdfs"
    pdf_dest.mkdir(parents=True, exist_ok=True)

    for path in list_guides():
        # Flatten into knowledge root with stable names so KnowledgeBase finds them.
        target_name = path.name
        (dest / target_name).write_text(path.read_text(encoding="utf-8"), encoding="utf-8")

    index = GUIDES_DIR / "00-index.md"
    if index.exists():
        (dest / "00-guides-index.md").write_text(
            index.read_text(encoding="utf-8"), encoding="utf-8"
        )

    if PDFS_DIR.exists():
        for pdf in PDFS_DIR.glob("*.pdf"):
            shutil.copy2(pdf, pdf_dest / pdf.name)

    return dest


def _title(text: str, fallback: str) -> str:
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("#"):
            return line.lstrip("#").strip()
    return fallback.replace("-", " ")


def _keywords(query: str) -> list[str]:
    tokens = re.findall(r"[a-z0-9]+", (query or "").lower())
    stop = {
        "the",
        "and",
        "for",
        "with",
        "that",
        "this",
        "from",
        "into",
        "make",
        "please",
        "bars",
        "bar",
    }
    return [t for t in tokens if len(t) > 2 and t not in stop][:24]


def _excerpt(body: str, keywords: Iterable[str], budget: int = 2400) -> str:
    if len(body) <= budget:
        return body
    keys = [k for k in keywords if k]
    if not keys:
        return body[:budget].rsplit("\n", 1)[0]
    best_i = 0
    best_score = -1
    lower = body.lower()
    step = max(200, budget // 3)
    for i in range(0, max(1, len(body) - budget), step):
        window = lower[i : i + budget]
        score = sum(window.count(k) for k in keys)
        if score > best_score:
            best_score = score
            best_i = i
    chunk = body[best_i : best_i + budget]
    return chunk.rsplit("\n", 1)[0] if "\n" in chunk else chunk
