"""Session state — key/BPM/elements lock across turns."""
from __future__ import annotations

import json
from dataclasses import dataclass, field, asdict
from typing import Any, Dict, List, Optional


@dataclass
class SessionState:
    session_id: str
    key: Optional[str] = None
    bpm: Optional[float] = None
    loop_bars: Optional[int] = None
    subgenre: Optional[str] = None
    elements: Dict[str, Any] = field(default_factory=dict)  # role -> last generation summary
    history: List[str] = field(default_factory=list)

    def blob(self) -> str:
        return json.dumps(
            {
                "key": self.key,
                "bpm": self.bpm,
                "loop_bars": self.loop_bars,
                "subgenre": self.subgenre,
                "elements": list(self.elements.keys()),
                "element_roots": {
                    k: v.get("roots") for k, v in self.elements.items() if isinstance(v, dict)
                },
            },
            indent=2,
        )

    def update_from_generation(self, gen_dict: Dict[str, Any]) -> None:
        meta = gen_dict.get("meta", {})
        self.key = meta.get("key", self.key)
        self.bpm = meta.get("bpm", self.bpm)
        self.loop_bars = meta.get("loop_bars", self.loop_bars)
        for t in gen_dict.get("tracks", []):
            role = t.get("role")
            notes = t.get("notes", [])
            roots = sorted({(n["pitch"] % 12) for n in notes})[:8]
            self.elements[role] = {
                "name": t.get("name"),
                "note_count": len(notes),
                "roots": roots,
            }


class SessionManager:
    def __init__(self):
        self._sessions: Dict[str, SessionState] = {}

    def get(self, session_id: str = "default") -> SessionState:
        if session_id not in self._sessions:
            self._sessions[session_id] = SessionState(session_id=session_id)
        return self._sessions[session_id]
