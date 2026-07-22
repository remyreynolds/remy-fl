"""Stage 6: pure-code track merge and response metadata."""
from __future__ import annotations

from app.schema import GenerationPlan, Intent, Meta, NoteJSON, Track


class FinalizerStage:
    def finalize(
        self,
        drafts: list[NoteJSON],
        plan: GenerationPlan,
        intent: Intent,
        *,
        warnings: list[str] | None = None,
        trace_id: str | None = None,
    ) -> dict:
        tracks: list[Track] = []
        seen_roles = set()
        for draft in drafts:
            for track in draft.tracks:
                if track.role not in seen_roles:
                    tracks.append(track)
                    seen_roles.add(track.role)
        meta = Meta(
            bpm=plan.bpm,
            key=plan.key,
            ppq=960,
            loop_bars=plan.bars,
            swing_pct=plan.swing_pct,
        )
        return {
            "meta": meta.model_dump(),
            "tracks": [track.model_dump() for track in tracks],
            "influence_citation": plan.influence_citation,
            "description": (
                f"{plan.bars}-bar {intent.subgenre or 'house'} "
                f"{intent.element} in {plan.key} at {plan.bpm:g} BPM."
            ),
            "variation_offers": _variation_offers(intent, plan),
            "warnings": warnings or [],
            "trace_id": trace_id,
            "collab_type": intent.collab_type,
            "input_notes": [
                note.model_dump() for note in intent.input_notes
            ] if intent.collab_type else [],
            "sound_type": intent.sound_type,
        }


def _variation_offers(intent: Intent, plan: GenerationPlan) -> list[str]:
    candidates = []
    current = set(intent.mood_words)
    if "dark" not in current and plan.mode != "phrygian":
        candidates.append((abs(0.8), "Make it darker with lower Phrygian color?"))
    if plan.density_budget.get(intent.element, "med") != "low":
        candidates.append((abs(0.7), "Try a sparser, more spacious variation?"))
    if plan.swing_pct < 58:
        candidates.append((abs(58 - plan.swing_pct) / 10, "Push the swing further?"))
    if plan.density_budget.get(intent.element, "med") != "high":
        candidates.append((abs(0.5), "Build a busier peak-time variation?"))
    candidates.sort(key=lambda item: item[0], reverse=True)
    return [text for _, text in candidates[:2]]

