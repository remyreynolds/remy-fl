"""Stage 1: rules-first intent perception with optional cheap-model fallback."""
from __future__ import annotations

import json
import re
from typing import Any

from app.memory import CognitiveMemory, normalize_subgenre
from app.schema import Intent


SUBGENRE_TERMS = {
    "deep house": "deep_house",
    "tech house": "tech_house",
    "chicago": "classic",
    "classic house": "classic",
    "french": "french",
    "filter house": "french",
    "progressive": "prog_melodic",
    "melodic house": "prog_melodic",
    "afro": "afro",
    "garage": "garage",
    "ukg": "garage",
    "piano house": "piano_house",
}
ELEMENT_TERMS = {
    "chord": "chords",
    "piano": "chords",
    "bass": "bass",
    "melody": "melody",
    "lead": "melody",
    "arp": "arp",
    "percussion": "perc",
    "perc": "perc",
    "drum": "perc",
    "stack": "stack",
    "full track": "stack",
}
REVISION_TERMS = (
    "make it",
    "less ",
    "more ",
    "change ",
    "revise",
    "variation",
    "bar ",
    "shorter",
    "longer",
)
COLLAB_MODES = {
    "fix groove": "fix_groove",
    "fix my groove": "fix_groove",
    "reharmonize": "reharmonize",
    "continue": "continue",
    "bass under": "bass_under",
}


class PerceptionStage:
    def __init__(self, memory: CognitiveMemory, llm: Any | None = None):
        self.memory = memory
        self.llm = llm

    def perceive(
        self,
        message: str,
        session: dict[str, Any] | None = None,
        *,
        mode: str | None = None,
        input_notes: list[dict[str, Any]] | None = None,
        sound_type: str | None = None,
    ) -> Intent:
        low = message.lower().strip()
        session = session or {}
        subgenre = next(
            (value for term, value in SUBGENRE_TERMS.items() if term in low),
            None,
        )
        element = next(
            (value for term, value in ELEMENT_TERMS.items() if term in low),
            None,
        )
        normalized_mode = (mode or "").strip().lower()
        collab_type = COLLAB_MODES.get(normalized_mode) or next(
            (value for term, value in COLLAB_MODES.items() if term in low),
            None,
        )
        action = (
            "collaborate"
            if collab_type
            else self._action(low, bool(session.get("elements")))
        )
        if collab_type == "bass_under":
            element = "bass"
        references = self._references(message)
        constraints = {
            "key": self._key(message),
            "bpm": self._number(message, r"\b(\d{2,3})\s*bpm\b"),
            "bars": self._number(message, r"\b(\d{1,2})\s*bars?\b"),
        }
        moods = [word for word in self.memory.moods() if word in low]
        mood_parameters = self._merge_moods(moods)

        confidence = 0.45
        if element:
            confidence += 0.25
        if subgenre or references:
            confidence += 0.2
        if any(value is not None for value in constraints.values()):
            confidence += 0.1
        if action == "revise":
            confidence = max(confidence, 0.8)

        intent = Intent(
            action=action,
            element=element or self._session_element(session) or "chords",
            subgenre=normalize_subgenre(subgenre or session.get("subgenre")),
            references=references,
            constraints=constraints,
            mood_words=moods,
            mood_parameters=mood_parameters,
            revision_target=self._revision_target(message) if action == "revise" else None,
            collab_type=collab_type,
            input_notes=input_notes or [],
            sound_type=sound_type,
            request_text=message,
            confidence=min(confidence, 1.0),
        )

        if (
            intent.action != "collaborate"
            and
            intent.confidence < 0.6
            and intent.subgenre is None
            and not intent.references
        ):
            fallback = self._llm_fallback(message, session)
            if fallback:
                intent = fallback
            if (
                intent.confidence < 0.6
                and intent.subgenre is None
                and not intent.references
            ):
                intent.clarifying_question = (
                    "Which house direction should I use—deep, tech, classic, "
                    "French, melodic, afro, garage, or piano house?"
                )
        return intent

    def _llm_fallback(
        self, message: str, session: dict[str, Any]
    ) -> Intent | None:
        if not self.llm or not getattr(self.llm, "has_key", lambda: False)():
            return None
        result = self.llm.call_json(
            stage="perception",
            system=(
                "Normalize the request into the Intent JSON contract. "
                "Do not invent references. Return JSON only."
            ),
            user=json.dumps({"message": message, "session": session}),
            tier="small",
        )
        if not result.ok:
            return None
        try:
            return Intent.model_validate(json.loads(result.text))
        except Exception:
            return None

    def _merge_moods(self, moods: list[str]) -> dict[str, Any]:
        merged: dict[str, Any] = {}
        numeric = {"register_bias", "swing_delta", "density_delta"}
        for word in moods:
            values = self.memory.get_mood(word) or {}
            for key, value in values.items():
                if key == "word":
                    continue
                if key in numeric:
                    merged[key] = merged.get(key, 0) + value
                elif key not in merged:
                    merged[key] = value
        return merged

    @staticmethod
    def _action(low: str, has_history: bool) -> str:
        if any(word in low for word in ("thumb", "feedback", "i like", "i don't")):
            return "feedback"
        if any(word in low for word in ("analyze", "reference")):
            return "analyze_reference"
        if has_history and any(term in low for term in REVISION_TERMS):
            return "revise"
        if low.endswith("?") and not any(word in low for word in ELEMENT_TERMS):
            return "question"
        return "generate"

    @staticmethod
    def _references(message: str) -> list[str]:
        quoted = re.findall(r'["“]([^"”]+)["”]', message)
        explicit = re.findall(
            r"(?:like|reference|inspired by)\s+([A-Z][^,;.]+(?:\s+-\s+[^,;.]+)?)",
            message,
        )
        return list(dict.fromkeys([*quoted, *explicit]))[:5]

    @staticmethod
    def _key(message: str) -> str | None:
        match = re.search(
            r"\b([A-G](?:#|b)?)\s*(minor|major|min|maj|dorian|phrygian)\b",
            message,
            re.IGNORECASE,
        )
        if not match:
            return None
        mode = match.group(2).lower()
        mode = "minor" if mode == "min" else "major" if mode == "maj" else mode
        return f"{match.group(1)} {mode}"

    @staticmethod
    def _number(message: str, pattern: str) -> int | None:
        match = re.search(pattern, message, re.IGNORECASE)
        return int(match.group(1)) if match else None

    @staticmethod
    def _session_element(session: dict[str, Any]) -> str | None:
        elements = list((session.get("elements") or {}).keys())
        return elements[-1] if elements else None

    @staticmethod
    def _revision_target(message: str) -> str:
        match = re.search(
            r"(bar\s+\d+|bass|chords?|melody|arp|perc|swing|density|"
            r"register|rhythm|velocity|harmony)",
            message,
            re.IGNORECASE,
        )
        dimension = match.group(1).lower() if match else "requested dimension"
        return f"latest generation: {dimension}; request={message.lower()}"

