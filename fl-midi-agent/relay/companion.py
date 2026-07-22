"""Local companion-app state and the honest FL handoff queue."""
from __future__ import annotations

import json
import os
import threading
from pathlib import Path
from typing import Any

from .models import NoteContract


ROLES = ("chords", "bass", "melody", "arp", "perc")


class CompanionStore:
    def __init__(self, path: Path):
        self.path = path
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._lock = threading.Lock()

    def session(self, session_id: str) -> dict[str, Any]:
        with self._lock:
            data = self._read()
            saved = data["sessions"].get(session_id)
        if saved:
            return saved
        return {
            "session_id": session_id,
            "key": "F minor",
            "bpm": 124,
            "bars": 8,
            "slots": [
                {
                    "role": role,
                    "label": "Perc" if role == "perc" else role.title(),
                    "locked": False,
                }
                for role in ROLES
            ],
        }

    def save_session(self, session_id: str, session: dict[str, Any]) -> None:
        with self._lock:
            data = self._read()
            data["sessions"][session_id] = {**session, "session_id": session_id}
            self._write(data)

    def enqueue(
        self,
        generation: dict[str, Any],
        roles: list[str] | None = None,
    ) -> int:
        contract = NoteContract.model_validate(generation)
        selected = {role.lower() for role in roles} if roles else None
        queued = 0
        with self._lock:
            data = self._read()
            for track in contract.tracks:
                if selected is not None and track.role not in selected:
                    continue
                one_track = {
                    "meta": contract.meta.model_dump(),
                    "tracks": [track.model_dump()],
                }
                data["queue"].setdefault(track.role, []).append(one_track)
                queued += 1
            self._write(data)
        return queued

    def dequeue(self, role: str) -> NoteContract | None:
        normalized = role.lower()
        with self._lock:
            data = self._read()
            queue = data["queue"].get(normalized, [])
            if not queue:
                return None
            value = queue.pop(0)
            self._write(data)
        return NoteContract.model_validate(value)

    def _read(self) -> dict[str, Any]:
        if not self.path.exists():
            return {"sessions": {}, "queue": {}}
        try:
            value = json.loads(self.path.read_text(encoding="utf-8"))
            value.setdefault("sessions", {})
            value.setdefault("queue", {})
            return value
        except (OSError, ValueError):
            return {"sessions": {}, "queue": {}}

    def _write(self, value: dict[str, Any]) -> None:
        temporary = self.path.with_suffix(".tmp")
        with temporary.open("w", encoding="utf-8") as handle:
            json.dump(value, handle, separators=(",", ":"))
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, self.path)

