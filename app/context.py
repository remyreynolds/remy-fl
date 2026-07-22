"""Stage 2: deterministic, priority-ordered context retrieval."""
from __future__ import annotations

import json
from typing import Any

from app.analysis import analyze_notes
from app.memory import CognitiveMemory, intent_digest
from app.schema import ContextPack, Intent


class ContextAssembler:
    def __init__(self, memory: CognitiveMemory, token_budget: int = 2500):
        self.memory = memory
        self.token_budget = token_budget

    def assemble(
        self,
        intent: Intent,
        *,
        session_id: str,
        user_id: str = "default",
        request_text: str = "",
        reuse_cache: bool = True,
    ) -> ContextPack:
        digest = intent_digest(intent.model_dump())
        if reuse_cache:
            cached = (
                self.memory.cache_latest(session_id)
                if intent.action == "revise"
                else self.memory.cache_get(session_id, digest)
            )
            if cached:
                # Session and taste are always live; only retrieval layers are reused.
                session = self.memory.get_session(session_id, user_id)
                return ContextPack.model_validate(
                    {
                        **cached,
                        "session": session.compact(),
                        "taste_profile": self.memory.get_taste(user_id),
                        "cache_hit": True,
                    }
                )

        session = self.memory.get_session(session_id, user_id)
        taste = self.memory.get_taste(user_id)
        pack: dict[str, Any] = {
            "session": session.compact(),
            "taste_profile": taste,
            "fingerprints": [],
            "episodes": [],
            "genre_card": {},
            "heuristics": [],
            "notes_profile": {},
            "sound_type_constraints": {},
            "cache_hit": False,
        }
        if intent.action == "collaborate" and intent.input_notes:
            pack["notes_profile"] = analyze_notes(
                [note.model_dump() for note in intent.input_notes]
            ).model_dump(by_alias=True)
        if intent.sound_type:
            pack["sound_type_constraints"] = self.memory.get_sound_type(
                intent.sound_type
            )
        used = (
            _tokens(pack["session"])
            + _tokens(taste)
            + _tokens(pack["notes_profile"])
            + _tokens(pack["sound_type_constraints"])
        )

        # Priority 3: top fingerprints.
        candidates = self.memory.retrieve_fingerprints(
            subgenre=intent.subgenre,
            key=intent.constraints.get("key") or session.key,
            bpm=intent.constraints.get("bpm") or session.bpm,
            references=intent.references,
            query=request_text,
            limit=3,
        )
        pack["fingerprints"], used = self._fit_list(candidates, used)

        # Priority 4: two most relevant episodes, including rejections.
        episodes = self.memory.recent_episodes(
            session_id, intent.element, limit=2
        )
        pack["episodes"], used = self._fit_list(episodes, used)

        # Priority 5: genre card.
        card = self.memory.get_genre_card(intent.subgenre or session.subgenre)
        if card and used + _tokens(card) <= self.token_budget:
            pack["genre_card"] = card
            used += _tokens(card)

        # Learned rules are capped at five and only fill remaining budget.
        heuristics = self.memory.active_heuristics(
            user_id=user_id,
            subgenre=intent.subgenre or session.subgenre,
            element=intent.element,
            limit=5,
        )
        pack["heuristics"], used = self._fit_list(heuristics, used)
        pack["token_estimate"] = used
        result = ContextPack.model_validate(pack)
        self.memory.cache_put(session_id, digest, result.model_dump())
        return result

    def _fit_list(
        self, candidates: list[dict[str, Any]], used: int
    ) -> tuple[list[dict[str, Any]], int]:
        selected = []
        for item in candidates:
            size = _tokens(item)
            if used + size > self.token_budget:
                break
            selected.append(item)
            used += size
        return selected, used


def _tokens(value: Any) -> int:
    return max(1, len(json.dumps(value, separators=(",", ":"), default=str)) // 4)

