"""Layered SQLite memory for the seven-stage cognitive pipeline."""
from __future__ import annotations

import hashlib
import json
import sqlite3
import threading
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DB = REPO_ROOT / "data" / "style_library.db"
MIGRATION = Path(__file__).with_name("migrations") / "001_cognitive_memory.sql"
SEED_DIR = REPO_ROOT / "seeds"


SUBGENRE_ALIASES = {
    "deep house": "deep_house",
    "deep_house": "deep_house",
    "tech house": "tech_house",
    "tech_house": "tech_house",
    "classic": "classic",
    "chicago": "classic",
    "french": "french",
    "progressive": "prog_melodic",
    "prog melodic": "prog_melodic",
    "prog_melodic": "prog_melodic",
    "afro house": "afro",
    "afro": "afro",
    "garage": "garage",
    "uk garage": "garage",
    "piano house": "piano_house",
    "piano_house": "piano_house",
}


def normalize_subgenre(value: str | None) -> str | None:
    if not value:
        return None
    cleaned = value.strip().lower().replace("-", " ")
    return SUBGENRE_ALIASES.get(cleaned, cleaned.replace(" ", "_"))


@dataclass
class SessionMemory:
    session_id: str
    user_id: str = "default"
    key: str | None = None
    bpm: float | None = None
    loop_bars: int | None = None
    subgenre: str | None = None
    elements: dict[str, Any] = field(default_factory=dict)
    last_plan: dict[str, Any] = field(default_factory=dict)
    summary: str = ""

    def compact(self) -> dict[str, Any]:
        return {
            "session_id": self.session_id,
            "key": self.key,
            "bpm": self.bpm,
            "loop_bars": self.loop_bars,
            "subgenre": self.subgenre,
            "elements": self.elements,
            "last_plan": self.last_plan,
        }


class CognitiveMemory:
    """Persistent session, episodic, and semantic memory."""

    def __init__(self, db_path: Path | str | None = None, seed: bool = True):
        self.db_path = Path(db_path) if db_path else DEFAULT_DB
        self._session_cache: dict[str, SessionMemory] = {}
        self._cache_lock = threading.Lock()
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self.migrate()
        if seed:
            self.seed_defaults()

    def connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self.db_path, timeout=10)
        connection.row_factory = sqlite3.Row
        connection.execute("PRAGMA foreign_keys=ON")
        return connection

    def migrate(self) -> None:
        script = MIGRATION.read_text(encoding="utf-8")
        with self.connect() as db:
            # Existing v1 databases need additive columns before index creation.
            self._upgrade_legacy_tables(db)
            db.executescript(script)
            self._rebuild_generation_fts_if_needed(db)
            db.execute(
                "INSERT OR IGNORE INTO schema_migrations(version) VALUES (1)"
            )
            self._migrate_legacy_taste(db)

    def _rebuild_generation_fts_if_needed(
        self, db: sqlite3.Connection
    ) -> None:
        generation_count = int(
            db.execute("SELECT COUNT(*) FROM generations").fetchone()[0]
        )
        fts_count = int(
            db.execute("SELECT COUNT(*) FROM generations_fts").fetchone()[0]
        )
        if generation_count == fts_count:
            return
        db.execute("DELETE FROM generations_fts")
        for row in db.execute(
            "SELECT id, request, plan_json FROM generations"
        ).fetchall():
            db.execute(
                """
                INSERT INTO generations_fts(generation_id, prompt, plan)
                VALUES (?, ?, ?)
                """,
                (row["id"], row["request"], row["plan_json"]),
            )

    def _upgrade_legacy_tables(self, db: sqlite3.Connection) -> None:
        existing = {
            row["name"]
            for row in db.execute(
                "SELECT name FROM sqlite_master WHERE type='table'"
            ).fetchall()
        }
        columns = {
            "fingerprints": {
                "kept_rate": "REAL NOT NULL DEFAULT 0.5",
                "use_count": "INTEGER NOT NULL DEFAULT 0",
                "search_text": "TEXT NOT NULL DEFAULT ''",
            },
            "generations": {
                "user_id": "TEXT NOT NULL DEFAULT 'default'",
                "element": "TEXT",
                "intent_json": "TEXT NOT NULL DEFAULT '{}'",
                "context_json": "TEXT NOT NULL DEFAULT '{}'",
                "plan_json": "TEXT NOT NULL DEFAULT '{}'",
                "warnings_json": "TEXT NOT NULL DEFAULT '[]'",
                "fingerprint_ids_json": "TEXT NOT NULL DEFAULT '[]'",
                "trace_id": "TEXT",
                "revision_count": "INTEGER NOT NULL DEFAULT 0",
            },
            "feedback": {
                "session_id": "TEXT",
                "user_id": "TEXT NOT NULL DEFAULT 'default'",
                "created_at": "TEXT",
            },
        }
        for table, additions in columns.items():
            if table not in existing:
                continue
            present = {
                row["name"]
                for row in db.execute(f"PRAGMA table_info({table})").fetchall()
            }
            for name, definition in additions.items():
                if name not in present:
                    db.execute(
                        f"ALTER TABLE {table} ADD COLUMN {name} {definition}"
                    )

    def _migrate_legacy_taste(self, db: sqlite3.Connection) -> None:
        legacy = db.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='taste_profile'"
        ).fetchone()
        if not legacy:
            return
        for row in db.execute("SELECT user_id, data_json FROM taste_profile"):
            db.execute(
                """
                INSERT OR IGNORE INTO taste_profiles(user_id, data_json)
                VALUES (?, ?)
                """,
                (row["user_id"], row["data_json"]),
            )

    def seed_defaults(self, replace: bool = False) -> None:
        fingerprints = _read_seed("fingerprints.json")
        moods = _read_seed("mood_table.json")
        cards = _read_seed("genre_cards.json")
        sound_types = _read_seed("sound_types.json")
        with self.connect() as db:
            if replace:
                db.execute("DELETE FROM fingerprints")
                db.execute("DELETE FROM fingerprints_fts")
                db.execute("DELETE FROM mood_table")
                db.execute("DELETE FROM genre_cards")
                db.execute("DELETE FROM sound_types")
            existing_tracks = {
                row["track"]
                for row in db.execute("SELECT track FROM fingerprints").fetchall()
            }
            missing = [
                item for item in fingerprints
                if item.get("track") not in existing_tracks
            ]
            if missing:
                self._seed_fingerprints_in(db, missing)
            self._rebuild_fts_if_needed(db)
            for mood in moods:
                db.execute(
                    """
                    INSERT INTO mood_table(word, params_json) VALUES (?, ?)
                    ON CONFLICT(word) DO UPDATE SET params_json=excluded.params_json
                    """,
                    (mood["word"], _json(mood)),
                )
            for card in cards:
                normalized = normalize_subgenre(card["subgenre"])
                card = {**card, "subgenre": normalized}
                db.execute(
                    """
                    INSERT INTO genre_cards(subgenre, card_json) VALUES (?, ?)
                    ON CONFLICT(subgenre) DO UPDATE SET card_json=excluded.card_json
                    """,
                    (normalized, _json(card)),
                )
            for sound_type in sound_types:
                db.execute(
                    """
                    INSERT INTO sound_types(type, constraints_json) VALUES (?, ?)
                    ON CONFLICT(type) DO UPDATE SET
                        constraints_json=excluded.constraints_json
                    """,
                    (sound_type["type"], _json(sound_type)),
                )

    def _rebuild_fts_if_needed(self, db: sqlite3.Connection) -> None:
        fingerprint_count = int(
            db.execute("SELECT COUNT(*) FROM fingerprints").fetchone()[0]
        )
        fts_count = int(
            db.execute("SELECT COUNT(*) FROM fingerprints_fts").fetchone()[0]
        )
        if fts_count == fingerprint_count:
            return
        db.execute("DELETE FROM fingerprints_fts")
        for row in db.execute("SELECT * FROM fingerprints").fetchall():
            data = _loads(row["data_json"])
            normalized = normalize_subgenre(row["subgenre"])
            data["subgenre"] = normalized
            search_text = " ".join(
                [
                    str(row["track"] or ""),
                    str(normalized or ""),
                    " ".join(data.get("signature_moves", [])),
                    str(data.get("bass_archetype", "")),
                ]
            )
            db.execute(
                """
                UPDATE fingerprints
                SET subgenre=?, data_json=?, search_text=?
                WHERE id=?
                """,
                (normalized, _json(data), search_text, row["id"]),
            )
            db.execute(
                """
                INSERT INTO fingerprints_fts(
                    fingerprint_id, track, subgenre, search_text
                ) VALUES (?, ?, ?, ?)
                """,
                (row["id"], row["track"], normalized, search_text),
            )

    def seed_fingerprints(
        self, items: list[dict[str, Any]], replace: bool = False
    ) -> int:
        with self.connect() as db:
            if replace:
                db.execute("DELETE FROM fingerprints")
                db.execute("DELETE FROM fingerprints_fts")
            return self._seed_fingerprints_in(db, items)

    def _seed_fingerprints_in(
        self, db: sqlite3.Connection, items: Iterable[dict[str, Any]]
    ) -> int:
        count = 0
        for item in items:
            normalized = normalize_subgenre(item.get("subgenre"))
            enriched = {**item, "subgenre": normalized}
            search_text = " ".join(
                [
                    str(item.get("track", "")),
                    str(normalized or ""),
                    " ".join(item.get("signature_moves", [])),
                    str(item.get("bass_archetype", "")),
                ]
            )
            cursor = db.execute(
                """
                INSERT INTO fingerprints(
                    track, subgenre, bpm, key, mode, data_json, kept_rate,
                    use_count, search_text
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    item.get("track"),
                    normalized,
                    item.get("bpm"),
                    item.get("key"),
                    item.get("mode"),
                    _json(enriched),
                    float(item.get("kept_rate", 0.5)),
                    int(item.get("use_count", 0)),
                    search_text,
                ),
            )
            db.execute(
                """
                INSERT INTO fingerprints_fts(
                    fingerprint_id, track, subgenre, search_text
                ) VALUES (?, ?, ?, ?)
                """,
                (cursor.lastrowid, item.get("track"), normalized, search_text),
            )
            count += 1
        return count

    def fingerprint_count(self) -> int:
        with self.connect() as db:
            return int(db.execute("SELECT COUNT(*) FROM fingerprints").fetchone()[0])

    def get_mood(self, word: str) -> dict[str, Any] | None:
        with self.connect() as db:
            row = db.execute(
                "SELECT params_json FROM mood_table WHERE word=?",
                (word.lower(),),
            ).fetchone()
        return _loads(row["params_json"]) if row else None

    def moods(self) -> dict[str, dict[str, Any]]:
        with self.connect() as db:
            rows = db.execute("SELECT word, params_json FROM mood_table").fetchall()
        return {row["word"]: _loads(row["params_json"]) for row in rows}

    def get_genre_card(self, subgenre: str | None) -> dict[str, Any]:
        normalized = normalize_subgenre(subgenre)
        if not normalized:
            return {}
        with self.connect() as db:
            row = db.execute(
                "SELECT card_json FROM genre_cards WHERE subgenre=?",
                (normalized,),
            ).fetchone()
        return _loads(row["card_json"]) if row else {}

    def get_sound_type(self, sound_type: str | None) -> dict[str, Any]:
        if not sound_type:
            return {}
        with self.connect() as db:
            row = db.execute(
                "SELECT constraints_json FROM sound_types WHERE type=?",
                (sound_type.lower(),),
            ).fetchone()
        return _loads(row["constraints_json"]) if row else {}

    def retrieve_fingerprints(
        self,
        *,
        subgenre: str | None = None,
        key: str | None = None,
        bpm: float | None = None,
        references: list[str] | None = None,
        query: str | None = None,
        limit: int = 3,
    ) -> list[dict[str, Any]]:
        normalized = normalize_subgenre(subgenre)
        references = references or []
        terms = _fts_terms(" ".join([query or "", *references]))
        with self.connect() as db:
            rows: list[sqlite3.Row]
            if terms:
                try:
                    rows = db.execute(
                        """
                        SELECT f.*, bm25(fingerprints_fts) AS fts_rank
                        FROM fingerprints_fts
                        JOIN fingerprints f
                          ON f.id = fingerprints_fts.fingerprint_id
                        WHERE fingerprints_fts MATCH ?
                        LIMIT 50
                        """,
                        (terms,),
                    ).fetchall()
                except sqlite3.OperationalError:
                    rows = db.execute("SELECT *, 0 AS fts_rank FROM fingerprints").fetchall()
            else:
                rows = db.execute(
                    "SELECT *, 0 AS fts_rank FROM fingerprints"
                ).fetchall()
            if not rows:
                rows = db.execute(
                    "SELECT *, 0 AS fts_rank FROM fingerprints"
                ).fetchall()

        ranked = []
        key_parts = (key or "").split()
        key_root = key_parts[0].lower() if key_parts else ""
        reference_text = " ".join(references).lower()
        for row in rows:
            data = _loads(row["data_json"])
            fp_sub = normalize_subgenre(row["subgenre"])
            sub_score = 4.0 if normalized and fp_sub == normalized else 0.0
            reference_score = (
                3.0
                if reference_text
                and any(
                    token in str(row["track"]).lower()
                    for token in reference_text.split()
                    if len(token) > 2
                )
                else 0.0
            )
            bpm_score = (
                max(0.0, 2.0 - abs(float(row["bpm"] or bpm) - float(bpm)) / 4)
                if bpm is not None
                else 0.5
            )
            key_score = (
                0.75
                if key_root and str(row["key"] or "").lower().startswith(key_root)
                else 0.0
            )
            kept_rate = float(row["kept_rate"])
            score = sub_score + reference_score + bpm_score + key_score + kept_rate
            ranked.append(
                (
                    score,
                    {
                        **data,
                        "id": int(row["id"]),
                        "kept_rate": kept_rate,
                        "use_count": int(row["use_count"]),
                    },
                )
            )
        ranked.sort(key=lambda item: item[0], reverse=True)
        return [item[1] for item in ranked[:limit]]

    def get_session(
        self, session_id: str, user_id: str = "default"
    ) -> SessionMemory:
        with self._cache_lock:
            cached = self._session_cache.get(session_id)
            if cached:
                return cached
        with self.connect() as db:
            row = db.execute(
                "SELECT * FROM sessions WHERE session_id=?",
                (session_id,),
            ).fetchone()
            if not row:
                db.execute(
                    "INSERT INTO sessions(session_id, user_id) VALUES (?, ?)",
                    (session_id, user_id),
                )
                created = SessionMemory(session_id=session_id, user_id=user_id)
                self.cache_session(created)
                return created
        loaded = SessionMemory(
            session_id=row["session_id"],
            user_id=row["user_id"],
            key=row["key"],
            bpm=row["bpm"],
            loop_bars=row["loop_bars"],
            subgenre=row["subgenre"],
            elements=_loads(row["elements_json"]),
            last_plan=_loads(row["last_plan_json"]),
            summary=row["summary"],
        )
        self.cache_session(loaded)
        return loaded

    def cache_session(self, session: SessionMemory) -> None:
        with self._cache_lock:
            self._session_cache[session.session_id] = session

    def save_session(self, session: SessionMemory) -> None:
        self.cache_session(session)
        with self.connect() as db:
            db.execute(
                """
                INSERT INTO sessions(
                    session_id, user_id, key, bpm, loop_bars, subgenre,
                    elements_json, last_plan_json, summary, updated_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
                ON CONFLICT(session_id) DO UPDATE SET
                    user_id=excluded.user_id,
                    key=excluded.key,
                    bpm=excluded.bpm,
                    loop_bars=excluded.loop_bars,
                    subgenre=excluded.subgenre,
                    elements_json=excluded.elements_json,
                    last_plan_json=excluded.last_plan_json,
                    summary=excluded.summary,
                    updated_at=CURRENT_TIMESTAMP
                """,
                (
                    session.session_id,
                    session.user_id,
                    session.key,
                    session.bpm,
                    session.loop_bars,
                    normalize_subgenre(session.subgenre),
                    _json(session.elements),
                    _json(session.last_plan),
                    session.summary,
                ),
            )

    def close_session(self, session_id: str) -> None:
        with self.connect() as db:
            db.execute(
                """
                UPDATE sessions SET closed_at=CURRENT_TIMESTAMP,
                    updated_at=CURRENT_TIMESTAMP
                WHERE session_id=?
                """,
                (session_id,),
            )
        with self._cache_lock:
            self._session_cache.pop(session_id, None)

    def get_taste(self, user_id: str = "default") -> dict[str, Any]:
        with self.connect() as db:
            row = db.execute(
                "SELECT * FROM taste_profiles WHERE user_id=?",
                (user_id,),
            ).fetchone()
        if not row:
            return {
                "preferred_keys": {},
                "swing_bias": 0.0,
                "density_bias": 0.0,
                "brightness": 0.0,
                "complexity": 3.0,
                "banned_moves": [],
                "confidence": {},
                "evidence_count": 0,
            }
        base = _loads(row["data_json"])
        return {
            **base,
            "preferred_keys": _loads(row["preferred_keys_json"]),
            "swing_bias": row["swing_bias"],
            "density_bias": row["density_bias"],
            "brightness": row["brightness"],
            "complexity": row["complexity"],
            "banned_moves": _loads(row["banned_moves_json"]),
            "confidence": _loads(row["confidence_json"]),
            "evidence_count": row["evidence_count"],
        }

    def update_taste(
        self, updates: dict[str, Any], user_id: str = "default"
    ) -> dict[str, Any]:
        current = self.get_taste(user_id)
        merged = {**current, **updates}
        merged["evidence_count"] = int(current.get("evidence_count", 0)) + 1
        confidence = dict(current.get("confidence", {}))
        for key in updates:
            if key not in {"confidence", "evidence_count"}:
                confidence[key] = min(1.0, float(confidence.get(key, 0)) + 0.1)
        merged["confidence"] = confidence
        with self.connect() as db:
            db.execute(
                """
                INSERT INTO taste_profiles(
                    user_id, preferred_keys_json, swing_bias, density_bias,
                    brightness, complexity, banned_moves_json, confidence_json,
                    evidence_count, data_json, updated_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
                ON CONFLICT(user_id) DO UPDATE SET
                    preferred_keys_json=excluded.preferred_keys_json,
                    swing_bias=excluded.swing_bias,
                    density_bias=excluded.density_bias,
                    brightness=excluded.brightness,
                    complexity=excluded.complexity,
                    banned_moves_json=excluded.banned_moves_json,
                    confidence_json=excluded.confidence_json,
                    evidence_count=excluded.evidence_count,
                    data_json=excluded.data_json,
                    updated_at=CURRENT_TIMESTAMP
                """,
                (
                    user_id,
                    _json(merged.get("preferred_keys", {})),
                    float(merged.get("swing_bias", 0)),
                    float(merged.get("density_bias", 0)),
                    float(merged.get("brightness", 0)),
                    float(merged.get("complexity", 3)),
                    _json(merged.get("banned_moves", [])),
                    _json(confidence),
                    merged["evidence_count"],
                    _json(merged),
                ),
            )
        return merged

    def reset_taste(self, user_id: str = "default") -> None:
        with self.connect() as db:
            db.execute("DELETE FROM taste_profiles WHERE user_id=?", (user_id,))
            db.execute("DELETE FROM heuristics WHERE user_id=?", (user_id,))

    def delete_taste_belief(
        self, belief_id: str, user_id: str = "default"
    ) -> None:
        taste = self.get_taste(user_id)
        if belief_id == "keys":
            taste["preferred_keys"] = {}
        elif belief_id == "swing":
            taste["swing_bias"] = 0
        elif belief_id == "density":
            taste["density_bias"] = 0
        elif belief_id == "brightness":
            taste["brightness"] = 0
        elif belief_id == "complexity":
            taste["complexity"] = 3
        else:
            taste.pop(belief_id, None)
        self.update_taste(taste, user_id)

    def set_heuristic_active(self, heuristic_id: int, active: bool) -> None:
        with self.connect() as db:
            db.execute(
                """
                UPDATE heuristics SET active=?, updated_at=CURRENT_TIMESTAMP
                WHERE id=?
                """,
                (int(active), heuristic_id),
            )

    def active_heuristics(
        self,
        *,
        user_id: str,
        subgenre: str | None,
        element: str | None,
        limit: int = 5,
    ) -> list[dict[str, Any]]:
        normalized = normalize_subgenre(subgenre) or ""
        with self.connect() as db:
            rows = db.execute(
                """
                SELECT * FROM heuristics
                WHERE user_id=? AND active=1
                ORDER BY evidence DESC, id DESC
                """,
                (user_id,),
            ).fetchall()
        matched = []
        for row in rows:
            trigger = row["trigger_condition"].lower().replace(" ", "_")
            if "subgenre=" in trigger and normalized not in trigger:
                continue
            if "element=" in trigger and (element or "").lower() not in trigger:
                continue
            matched.append(dict(row))
        return matched[:limit]

    def add_heuristic(
        self,
        trigger: str,
        rule: str,
        *,
        user_id: str = "default",
        evidence: int = 1,
        active: bool = False,
    ) -> int:
        with self.connect() as db:
            db.execute(
                """
                INSERT INTO heuristics(
                    user_id, trigger_condition, rule, evidence, active
                ) VALUES (?, ?, ?, ?, ?)
                ON CONFLICT(user_id, trigger_condition, rule) DO UPDATE SET
                    evidence=heuristics.evidence + excluded.evidence,
                    active=MAX(heuristics.active, excluded.active),
                    updated_at=CURRENT_TIMESTAMP
                """,
                (user_id, trigger, rule, evidence, int(active)),
            )
            row = db.execute(
                """
                SELECT id FROM heuristics
                WHERE user_id=? AND trigger_condition=? AND rule=?
                """,
                (user_id, trigger, rule),
            ).fetchone()
        return int(row["id"])

    def record_heuristic_outcome(
        self,
        *,
        user_id: str,
        subgenre: str | None,
        element: str,
        kept: bool,
    ) -> None:
        matched = self.active_heuristics(
            user_id=user_id,
            subgenre=subgenre,
            element=element,
            limit=5,
        )
        if not matched:
            return
        field = "evidence" if kept else "contradictions"
        with self.connect() as db:
            for item in matched:
                db.execute(
                    f"""
                    UPDATE heuristics SET {field}={field} + 1,
                        updated_at=CURRENT_TIMESTAMP
                    WHERE id=?
                    """,
                    (item["id"],),
                )

    def recent_episodes(
        self, session_id: str, element: str, limit: int = 2
    ) -> list[dict[str, Any]]:
        with self.connect() as db:
            rows = db.execute(
                """
                SELECT g.id, g.request, g.plan_json, g.output_json, g.kept,
                       g.validation_errors, f.rating, f.text AS feedback_text
                FROM generations g
                LEFT JOIN feedback f ON f.generation_id=g.id
                WHERE g.session_id=? AND g.element=?
                ORDER BY g.id DESC
                LIMIT ?
                """,
                (session_id, element, limit),
            ).fetchall()
        return [
            {
                "generation_id": row["id"],
                "request": row["request"],
                "plan": _loads(row["plan_json"]),
                "kept": bool(row["kept"]),
                "rejection_reason": row["feedback_text"]
                if row["rating"] is not None and row["rating"] <= 0
                else row["validation_errors"],
            }
            for row in rows
        ]

    def cache_get(self, session_id: str, intent_hash: str) -> dict[str, Any] | None:
        cache_key = f"{session_id}:{intent_hash}"
        with self.connect() as db:
            row = db.execute(
                "SELECT context_json FROM context_cache WHERE cache_key=?",
                (cache_key,),
            ).fetchone()
        return _loads(row["context_json"]) if row else None

    def cache_latest(self, session_id: str) -> dict[str, Any] | None:
        with self.connect() as db:
            row = db.execute(
                """
                SELECT context_json FROM context_cache
                WHERE session_id=? ORDER BY created_at DESC LIMIT 1
                """,
                (session_id,),
            ).fetchone()
        return _loads(row["context_json"]) if row else None

    def cache_put(
        self, session_id: str, intent_hash: str, context: dict[str, Any]
    ) -> None:
        cache_key = f"{session_id}:{intent_hash}"
        with self.connect() as db:
            db.execute(
                """
                INSERT INTO context_cache(
                    cache_key, session_id, intent_hash, context_json
                ) VALUES (?, ?, ?, ?)
                ON CONFLICT(cache_key) DO UPDATE SET
                    context_json=excluded.context_json,
                    created_at=CURRENT_TIMESTAMP
                """,
                (cache_key, session_id, intent_hash, _json(context)),
            )

    def create_trace(
        self, trace_id: str, session_id: str, request: str
    ) -> None:
        with self.connect() as db:
            db.execute(
                """
                INSERT INTO traces(trace_id, session_id, request)
                VALUES (?, ?, ?)
                """,
                (trace_id, session_id, request),
            )

    def save_trace(
        self,
        trace_id: str,
        stages: list[dict[str, Any]],
        models: dict[str, str],
        total_tokens: int,
        verdict_scores: dict[str, Any],
    ) -> None:
        with self.connect() as db:
            db.execute(
                """
                UPDATE traces SET stages_json=?, models_json=?, total_tokens=?,
                    verdict_scores_json=?, completed_at=CURRENT_TIMESTAMP
                WHERE trace_id=?
                """,
                (
                    _json(stages),
                    _json(models),
                    total_tokens,
                    _json(verdict_scores),
                    trace_id,
                ),
            )

    def get_trace(self, trace_id: str) -> dict[str, Any] | None:
        with self.connect() as db:
            row = db.execute(
                "SELECT * FROM traces WHERE trace_id=?", (trace_id,)
            ).fetchone()
        if not row:
            return None
        value = dict(row)
        for key in ("stages_json", "models_json", "verdict_scores_json"):
            value[key.removesuffix("_json")] = _loads(value.pop(key))
        return value

    def log_generation(
        self,
        *,
        request: str,
        raw_response: str = "",
        output_json: str | None,
        validation_ok: bool,
        validation_errors: str = "",
        latency_ms: int = 0,
        session_id: str | None = None,
        user_id: str = "default",
        element: str | None = None,
        intent: dict[str, Any] | None = None,
        context: dict[str, Any] | None = None,
        plan: dict[str, Any] | None = None,
        warnings: list[str] | None = None,
        fingerprint_ids: list[int] | None = None,
        trace_id: str | None = None,
        revision_count: int = 0,
    ) -> int:
        with self.connect() as db:
            cursor = db.execute(
                """
                INSERT INTO generations(
                    request, raw_response, output_json, validation_ok,
                    validation_errors, latency_ms, session_id, user_id,
                    element, intent_json, context_json, plan_json, warnings_json,
                    fingerprint_ids_json, trace_id, revision_count
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    request,
                    raw_response,
                    output_json,
                    int(validation_ok),
                    validation_errors,
                    latency_ms,
                    session_id,
                    user_id,
                    element,
                    _json(intent or {}),
                    _json(context or {}),
                    _json(plan or {}),
                    _json(warnings or []),
                    _json(fingerprint_ids or []),
                    trace_id,
                    revision_count,
                ),
            )
            generation_id = int(cursor.lastrowid)
            db.execute(
                """
                INSERT INTO generations_fts(generation_id, prompt, plan)
                VALUES (?, ?, ?)
                """,
                (generation_id, request, _json(plan or {})),
            )
            return generation_id

    def history(
        self,
        *,
        session_id: str | None,
        query: str = "",
        limit: int = 100,
    ) -> list[dict[str, Any]]:
        params: list[Any] = []
        join = ""
        where = []
        if query.strip():
            terms = _fts_terms(query)
            if terms:
                join = (
                    "JOIN generations_fts "
                    "ON generations_fts.generation_id=g.id"
                )
                where.append("generations_fts MATCH ?")
                params.append(terms)
            else:
                where.append("(g.request LIKE ? OR g.plan_json LIKE ?)")
                like = f"%{query.strip()}%"
                params.extend([like, like])
        if session_id:
            where.append("g.session_id=?")
            params.append(session_id)
        clause = "WHERE " + " AND ".join(where) if where else ""
        params.append(limit)
        with self.connect() as db:
            rows = db.execute(
                f"""
                SELECT g.*, f.rating
                FROM generations g
                {join}
                LEFT JOIN feedback f ON f.generation_id=g.id
                {clause}
                ORDER BY g.id DESC
                LIMIT ?
                """,
                params,
            ).fetchall()
        history = []
        for row in rows:
            generation = _loads(row["output_json"])
            plan = _loads(row["plan_json"])
            element = row["element"] or (
                generation.get("tracks", [{}])[0].get("role", "chords")
                if generation.get("tracks")
                else "chords"
            )
            history.append(
                {
                    "id": row["id"],
                    "session_id": row["session_id"],
                    "created_at": row["created_at"],
                    "prompt": row["request"],
                    "element": element,
                    "plan_summary": _plan_one_liner(plan, generation),
                    "kept": bool(row["kept"]),
                    "rejected": row["rating"] is not None
                    and int(row["rating"]) < 0,
                    "generation": generation,
                }
            )
        return history

    def list_fingerprints(self) -> list[dict[str, Any]]:
        with self.connect() as db:
            rows = db.execute(
                """
                SELECT id, track, subgenre, bpm, key, data_json, kept_rate
                FROM fingerprints ORDER BY kept_rate DESC, id DESC
                """
            ).fetchall()
        return [
            {
                **_loads(row["data_json"]),
                "id": row["id"],
                "track": row["track"],
                "subgenre": row["subgenre"],
                "bpm": row["bpm"],
                "key": row["key"],
                "groove": (
                    (_loads(row["data_json"]).get("signature_moves") or ["—"])[0]
                ),
                "kept_rate": row["kept_rate"],
            }
            for row in rows
        ]

    def delete_fingerprint(self, fingerprint_id: int) -> None:
        with self.connect() as db:
            db.execute(
                "DELETE FROM fingerprints_fts WHERE fingerprint_id=?",
                (fingerprint_id,),
            )
            db.execute("DELETE FROM fingerprints WHERE id=?", (fingerprint_id,))

    def log_verdict(
        self,
        *,
        generation_id: int | None,
        trace_id: str,
        session_id: str,
        element: str,
        attempt: int,
        code_passed: bool,
        code_errors: list[str],
        scores: dict[str, Any],
        revision_note: str | None,
    ) -> int:
        with self.connect() as db:
            cursor = db.execute(
                """
                INSERT INTO critic_verdicts(
                    generation_id, trace_id, session_id, element, attempt,
                    code_passed, code_errors_json, scores_json, revision_note
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    generation_id,
                    trace_id,
                    session_id,
                    element,
                    attempt,
                    int(code_passed),
                    _json(code_errors),
                    _json(scores),
                    revision_note,
                ),
            )
            return int(cursor.lastrowid)

    def link_verdicts(self, trace_id: str, generation_id: int) -> None:
        with self.connect() as db:
            db.execute(
                """
                UPDATE critic_verdicts SET generation_id=?
                WHERE trace_id=? AND generation_id IS NULL
                """,
                (generation_id, trace_id),
            )

    def add_feedback(
        self,
        generation_id: int,
        rating: int,
        text: str = "",
        diff: dict[str, Any] | None = None,
        *,
        user_id: str = "default",
        session_id: str | None = None,
    ) -> None:
        with self.connect() as db:
            db.execute(
                """
                INSERT INTO feedback(
                    generation_id, session_id, user_id, rating, text, diff_json
                ) VALUES (?, ?, ?, ?, ?, ?)
                """,
                (
                    generation_id,
                    session_id,
                    user_id,
                    rating,
                    text,
                    _json(diff or {}),
                ),
            )
            if rating > 0:
                db.execute(
                    "UPDATE generations SET kept=1 WHERE id=?",
                    (generation_id,),
                )
            row = db.execute(
                "SELECT fingerprint_ids_json FROM generations WHERE id=?",
                (generation_id,),
            ).fetchone()
            if row:
                self._update_fingerprints_in(
                    db, _loads(row["fingerprint_ids_json"]), rating > 0
                )

    def _update_fingerprints_in(
        self, db: sqlite3.Connection, ids: list[int], kept: bool
    ) -> None:
        target = 1.0 if kept else 0.0
        for fingerprint_id in ids:
            db.execute(
                """
                UPDATE fingerprints
                SET kept_rate=(kept_rate * 0.8) + (? * 0.2),
                    use_count=use_count
                WHERE id=?
                """,
                (target, fingerprint_id),
            )

    def mark_fingerprints_used(self, ids: list[int]) -> None:
        with self.connect() as db:
            for fingerprint_id in ids:
                db.execute(
                    """
                    UPDATE fingerprints SET use_count=use_count + 1
                    WHERE id=?
                    """,
                    (fingerprint_id,),
                )

    def record_regeneration(self, ids: list[int]) -> None:
        with self.connect() as db:
            self._update_fingerprints_in(db, ids, kept=False)


def intent_digest(value: dict[str, Any]) -> str:
    stable = _json(value)
    return hashlib.sha256(stable.encode("utf-8")).hexdigest()[:20]


def _read_seed(name: str) -> list[dict[str, Any]]:
    return json.loads((SEED_DIR / name).read_text(encoding="utf-8"))


def _json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), default=str)


def _loads(value: str | None) -> Any:
    if not value:
        return {}
    try:
        return json.loads(value)
    except (TypeError, json.JSONDecodeError):
        return {}


def _fts_terms(text: str) -> str:
    terms = [
        "".join(ch for ch in token.lower() if ch.isalnum() or ch == "_")
        for token in text.split()
    ]
    return " OR ".join(term for term in terms if len(term) > 2)


def _plan_one_liner(
    plan: dict[str, Any], generation: dict[str, Any]
) -> str:
    meta = generation.get("meta", {})
    progression = plan.get("progression") or []
    bass = plan.get("bass_archetype")
    parts = [
        str(plan.get("key") or meta.get("key") or "—"),
        "–".join(str(chord) for chord in progression[:4]),
        str(bass or ""),
        (
            f"{plan.get('swing_pct', meta.get('swing_pct'))}% swing"
            if plan.get("swing_pct", meta.get("swing_pct")) is not None
            else ""
        ),
    ]
    return " · ".join(part for part in parts if part)

