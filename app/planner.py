"""Stage 3: make coherent musical decisions before generating notes."""
from __future__ import annotations

import json
from copy import deepcopy
from typing import Any

from app.schema import ContextPack, GenerationPlan, Intent


DEFAULTS = {
    "key": "F minor",
    "mode": "aeolian",
    "bpm": 124.0,
    "bars": 8,
    "swing_pct": 54.0,
}
REGISTERS = {
    "bass": [28, 40],
    "chords": [48, 72],
    "melody": [60, 84],
    "arp": [55, 84],
    "perc": [35, 81],
}
COLLAB_INSTRUCTIONS = {
    "fix_groove": (
        "Preserve every pitch and its order. ONLY change start_tick, "
        "length_ticks, and velocity. Target taste/profile swing unless the "
        "input estimate is already at least 54%; preserve top-quartile accents."
    ),
    "reharmonize": (
        "Preserve the exact onset and length multisets. Re-voice in the "
        "detected or explicit key; keep top-note contour within ±3 semitones."
    ),
    "continue": (
        "Treat input as bars 1-N. Continue with its interval and rhythm "
        "vocabulary, introduce exactly one variation, then turn around to bar 1."
    ),
    "bass_under": (
        "Input is chords. Generate bass with roots at least 70%, kick "
        "avoidance, and the subgenre/taste archetype."
    ),
}


class PlannerStage:
    def __init__(self, llm: Any | None = None):
        self.llm = llm

    def plan(self, intent: Intent, context: ContextPack) -> GenerationPlan:
        if intent.sound_type == "sub" and intent.element not in {"bass", "stack"}:
            raise ValueError(
                "Sound type 'sub' is incompatible with melody/chord roles; "
                "choose Lead or generate Bass."
            )
        resolved = resolve_constraints(intent, context)
        if intent.action == "revise" and context.session.get("last_plan"):
            baseline = GenerationPlan.model_validate(context.session["last_plan"])
            deterministic = self._revision_plan(baseline, intent, resolved)
        else:
            deterministic = self._deterministic_plan(intent, context, resolved)

        if self.llm and getattr(self.llm, "has_key", lambda: False)():
            candidate = self._llm_plan(intent, context, deterministic)
            if candidate:
                deterministic = (
                    _scope_revision(
                        GenerationPlan.model_validate(
                            context.session["last_plan"]
                        ),
                        candidate,
                        deterministic,
                        intent,
                    )
                    if intent.action == "revise"
                    and context.session.get("last_plan")
                    else candidate
                )

        # Hard reconciliation happens after the model call.
        data = deterministic.model_dump()
        for field in ("key", "bpm", "bars"):
            if resolved[field] is not None:
                data[field] = resolved[field]
        data["mode"] = resolved["mode"]
        data["swing_pct"] = max(50, min(70, float(data["swing_pct"])))
        data["collab_type"] = intent.collab_type
        data["collab_instructions"] = COLLAB_INSTRUCTIONS.get(
            intent.collab_type or "", ""
        )
        data["notes_profile"] = context.notes_profile
        data["sound_type"] = intent.sound_type
        data["sound_type_constraints"] = context.sound_type_constraints
        data["theory_guide_excerpts"] = context.theory_guides
        data["explicit_long_notes"] = any(
            phrase in (intent.request_text or "").lower()
            for phrase in ("long notes", "long sustained", "sustained notes", "legato")
        )
        if intent.sound_type and intent.element != "stack":
            sound_register = context.sound_type_constraints.get("register")
            if sound_register:
                data["register_map"][intent.element] = list(sound_register)
        return GenerationPlan.model_validate(data)

    def _llm_plan(
        self,
        intent: Intent,
        context: ContextPack,
        baseline: GenerationPlan,
    ) -> GenerationPlan | None:
        result = self.llm.call_json(
            stage="planner",
            system=(
                "You are the Planner. Return a GenerationPlan JSON object and "
                "never output notes. Session locks beat fingerprint profile, "
                "taste, then genre defaults. Obey context.theory_guides "
                "(chord formulas, melody steps, house theory) when choosing "
                "progression, swing, and density. On revise, change only the "
                "revision_target dimension and include revision_diff. "
                f"Collaborator rule: {COLLAB_INSTRUCTIONS.get(intent.collab_type or '', 'none')}"
                " Sound-type constraints are hard unless explicit request "
                "words conflict, in which case the explicit words win."
            ),
            user=json.dumps(
                {
                    "intent": intent.model_dump(),
                    "context": context.model_dump(),
                    "baseline": baseline.model_dump(),
                }
            ),
            tier="mid",
        )
        if not result.ok:
            return None
        try:
            return GenerationPlan.model_validate(json.loads(result.text))
        except Exception:
            return None

    def _deterministic_plan(
        self,
        intent: Intent,
        context: ContextPack,
        resolved: dict[str, Any],
    ) -> GenerationPlan:
        profile = context.fingerprints[0] if context.fingerprints else {}
        card = context.genre_card
        mood = intent.mood_parameters
        progression = profile.get("progression") or _progression_for(
            resolved["key"], mood.get("chord_color")
        )
        density = profile.get("density") or {}
        density_bias = float(context.taste_profile.get("density_bias", 0))
        mood_density = float(mood.get("density_delta", 0))
        heuristic_density = sum(
            -1
            if any(
                word in str(item.get("rule", "")).lower()
                for word in ("reduce", "sparse", "fewer")
            )
            else 1
            if any(
                word in str(item.get("rule", "")).lower()
                for word in ("busier", "denser", "more notes")
            )
            else 0
            for item in context.heuristics
        )
        sound_density = float(
            context.sound_type_constraints.get("density_bias", 0)
        )
        target_density = _density_label(
            density_bias + mood_density + heuristic_density + sound_density
        )
        for role in ("chords", "bass", "melody", "arp", "perc"):
            density.setdefault(role, target_density)
        citation_ids = [
            str(item.get("id")) for item in context.fingerprints if item.get("id")
        ]
        guide_ids = [
            str(item.get("id")) for item in context.theory_guides if item.get("id")
        ]
        influence = (
            f"groove: fingerprints {','.join(citation_ids) or 'none'}; "
            f"guides {','.join(guide_ids) or 'none'}; "
            f"harmony: {card.get('subgenre', 'house defaults')}"
        )
        swing = (
            float(resolved["swing_pct"])
            + float(context.taste_profile.get("swing_bias", 0))
            + float(mood.get("swing_delta", 0))
        )
        register_map = deepcopy(REGISTERS)
        if (
            intent.collab_type in {"fix_groove", "reharmonize"}
            and context.notes_profile.get("register")
            and intent.element in register_map
        ):
            register_map[intent.element] = list(
                context.notes_profile["register"]
            )
        sound_register = context.sound_type_constraints.get("register")
        if sound_register and intent.element in register_map:
            register_map[intent.element] = list(sound_register)
        return GenerationPlan(
            key=resolved["key"],
            mode=resolved["mode"],
            bpm=resolved["bpm"],
            bars=resolved["bars"],
            swing_pct=max(50, min(70, swing)),
            progression=[str(value) for value in progression],
            harmonic_rhythm=_harmonic_rhythm(profile.get("harmonic_rhythm")),
            bass_archetype=str(
                profile.get("bass_archetype")
                or card.get("bass", "offbeat")
            ),
            rhythm_grid={
                "chords": ".x...x.......... per 16ths",
                "bass": ".x.x.x.x........ per 16ths",
                "melody": "motif with one-beat breath",
                "arp": "16ths with gated rests",
                "perc": "kick quarters; shuffled offbeats",
            },
            density_budget={
                role: _valid_density(density.get(role, target_density))
                for role in ("chords", "bass", "melody", "arp", "perc")
            },
            register_map=register_map,
            energy_curve="bars 1-6 hypnotic, bar 7 lift, bar 8 breath",
            influence_citation=influence,
            self_check=(
                f"{intent.element} follows {intent.subgenre or 'house'} "
                "constraints while preserving session locks."
            ),
        )

    def _revision_plan(
        self,
        baseline: GenerationPlan,
        intent: Intent,
        resolved: dict[str, Any],
    ) -> GenerationPlan:
        data = baseline.model_dump()
        target = (intent.revision_target or "").lower()
        diff: dict[str, Any] = {}
        if any(word in target for word in ("density", "busy", "bar")):
            old = data["density_budget"].get(intent.element, "med")
            new = "low" if "less" in target else "high"
            data["density_budget"][intent.element] = new
            diff["density_budget"] = {"from": old, "to": new}
        elif "swing" in target:
            old = data["swing_pct"]
            data["swing_pct"] = max(50, min(70, old + 2))
            diff["swing_pct"] = {"from": old, "to": data["swing_pct"]}
        elif "register" in target:
            role = intent.element if intent.element != "stack" else "melody"
            old = data["register_map"][role]
            data["register_map"][role] = [old[0] + 12, old[1] + 12]
            diff["register_map"] = {
                "from": old,
                "to": data["register_map"][role],
            }
        else:
            diff["target"] = intent.revision_target or "requested dimension"
        data["revision_diff"] = diff
        # Locks still win even if the revision text contains another key/BPM.
        data.update(
            {
                "key": resolved["key"],
                "mode": resolved["mode"],
                "bpm": resolved["bpm"],
                "bars": resolved["bars"],
            }
        )
        return GenerationPlan.model_validate(data)


def resolve_constraints(
    intent: Intent, context: ContextPack
) -> dict[str, Any]:
    """Resolve session > explicit/profile > taste > genre > hard default."""
    session = context.session
    profile = context.fingerprints[0] if context.fingerprints else {}
    notes_profile = context.notes_profile
    taste = context.taste_profile
    card = context.genre_card
    preferred_keys = taste.get("preferred_keys") or {}
    taste_key = (
        max(preferred_keys, key=preferred_keys.get) if preferred_keys else None
    )
    explicit = intent.constraints
    key = (
        session.get("key")
        or explicit.get("key")
        or notes_profile.get("detected_key")
        or profile.get("key")
        or taste_key
        or card.get("default_key")
        or DEFAULTS["key"]
    )
    bpm = (
        session.get("bpm")
        or explicit.get("bpm")
        or profile.get("bpm")
        or card.get("default_bpm")
        or DEFAULTS["bpm"]
    )
    if intent.collab_type == "continue" and notes_profile.get("bar_span"):
        bars = int(notes_profile["bar_span"][1]) + int(
            explicit.get("bars") or DEFAULTS["bars"]
        )
    else:
        bars = (
            session.get("loop_bars")
            or explicit.get("bars")
            or DEFAULTS["bars"]
        )
    locked_key = session.get("key")
    explicit_key = explicit.get("key")
    mode = (
        _mode_from_key(str(locked_key))
        if locked_key
        else _mode_from_key(str(explicit_key))
        if explicit_key
        else intent.mood_parameters.get("mode")
        or profile.get("mode")
        or card.get("default_mode")
        or _mode_from_key(str(key))
    )
    swing = (
        profile.get("swing_pct")
        or card.get("swing_pct")
        or DEFAULTS["swing_pct"]
    )
    return {
        "key": str(key),
        "mode": str(mode),
        "bpm": float(bpm),
        "bars": max(1, min(16, int(bars))),
        "swing_pct": float(swing),
    }


def _progression_for(key: str, color: str | None) -> list[str]:
    root = key.split()[0]
    suffix = "m11" if color == "minor11" else "m9"
    return [f"{root}{suffix}", "bVImaj7", "bIIImaj9", "bVIIm7"]


def _harmonic_rhythm(value: Any) -> int:
    if isinstance(value, (int, float)):
        return max(1, int(value))
    text = str(value or "2")
    for token in text.split():
        if token.isdigit():
            return max(1, int(token))
    return 2


def _density_label(value: float) -> str:
    return "low" if value < -0.5 else "high" if value > 0.5 else "med"


def _valid_density(value: Any) -> str:
    text = str(value).lower()
    return text if text in {"low", "med", "high"} else "med"


def _mode_from_key(key: str) -> str:
    low = key.lower()
    if "major" in low:
        return "major"
    if "dorian" in low:
        return "dorian"
    if "phrygian" in low:
        return "phrygian"
    return "aeolian"


def _scope_revision(
    baseline: GenerationPlan,
    candidate: GenerationPlan,
    deterministic: GenerationPlan,
    intent: Intent,
) -> GenerationPlan:
    """Prevent a revision model from changing untargeted plan dimensions."""
    target = (intent.revision_target or "").lower()
    data = baseline.model_dump()
    candidate_data = candidate.model_dump()
    role = intent.element if intent.element != "stack" else "melody"
    if any(word in target for word in ("density", "busy", "bar")):
        data["density_budget"][role] = candidate_data["density_budget"][role]
    elif "swing" in target:
        data["swing_pct"] = candidate_data["swing_pct"]
    elif "register" in target:
        data["register_map"][role] = candidate_data["register_map"][role]
    elif "rhythm" in target:
        data["rhythm_grid"][role] = candidate_data["rhythm_grid"][role]
    elif "harmony" in target or "chord" in target:
        data["progression"] = candidate_data["progression"]
        data["harmonic_rhythm"] = candidate_data["harmonic_rhythm"]
    else:
        # Unknown dimensions use the deterministic rules-only diff.
        data = deterministic.model_dump()
    data["revision_diff"] = deterministic.revision_diff
    return GenerationPlan.model_validate(data)

