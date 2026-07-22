"""Stage 7 and learning loops: taste updates, heuristics, consolidation."""
from __future__ import annotations

import json
from concurrent.futures import Future, ThreadPoolExecutor
from pathlib import Path
from typing import Any

from app.memory import CognitiveMemory
from app.schema import TasteDelta


class TasteUpdater:
    def __init__(self, memory: CognitiveMemory, llm: Any | None = None):
        self.memory = memory
        self.llm = llm

    def update(
        self,
        *,
        output: dict[str, Any],
        feedback_text: str,
        rating: int,
        diff: dict[str, Any] | None,
        user_id: str,
        subgenre: str | None,
        element: str,
    ) -> TasteDelta:
        diff = diff or {}
        delta = self._llm_delta(
            output, feedback_text, rating, diff, subgenre, element
        )
        if delta is None:
            delta = self._rules_delta(
                feedback_text, rating, diff, subgenre, element
            )
        kept_key = (output.get("meta") or {}).get("key")
        if rating > 0 and kept_key:
            preferred = dict(
                self.memory.get_taste(user_id).get("preferred_keys", {})
            )
            preferred[kept_key] = int(preferred.get(kept_key, 0)) + 1
            delta.updates["preferred_keys"] = preferred
        self.memory.update_taste(delta.updates, user_id)
        for item in delta.candidate_heuristics:
            self.memory.add_heuristic(
                item["when"],
                item["rule"],
                user_id=user_id,
                evidence=int(item.get("evidence", 1)),
                active=False,
            )
        return delta

    def _llm_delta(
        self,
        output: dict[str, Any],
        feedback_text: str,
        rating: int,
        diff: dict[str, Any],
        subgenre: str | None,
        element: str,
    ) -> TasteDelta | None:
        if not self.llm or not getattr(self.llm, "has_key", lambda: False)():
            return None
        result = self.llm.call_json(
            stage="taste_updater",
            system=(
                "Return TasteDelta JSON: updates and candidate_heuristics. "
                "Translate MIDI edits into precise preferences. JSON only."
            ),
            user=json.dumps(
                {
                    "output": output,
                    "feedback": feedback_text,
                    "rating": rating,
                    "midi_diff": diff,
                    "subgenre": subgenre,
                    "element": element,
                }
            ),
            tier="small",
        )
        if not result.ok:
            return None
        try:
            return TasteDelta.model_validate(json.loads(result.text))
        except Exception:
            return None

    @staticmethod
    def _rules_delta(
        text: str,
        rating: int,
        diff: dict[str, Any],
        subgenre: str | None,
        element: str,
    ) -> TasteDelta:
        low = text.lower()
        updates: dict[str, Any] = {"last_kept": rating > 0}
        candidates = []
        if "less busy" in low or "sparser" in low or diff.get("removed", 0) > diff.get("added", 0):
            updates["density_bias"] = -1.0
            candidates.append(
                {
                    "when": f"subgenre={subgenre} AND element={element}",
                    "rule": "reduce note density on regeneration",
                    "evidence": 1,
                }
            )
        if "more swing" in low:
            updates["swing_bias"] = 2.0
        if "brighter" in low:
            updates["brightness"] = 0.5
        if "darker" in low:
            updates["brightness"] = -0.5
        length_ratio = diff.get("mean_length_ratio")
        if length_ratio is not None and length_ratio < 0.8:
            candidates.append(
                {
                    "when": f"subgenre={subgenre} AND element={element}",
                    "rule": f"user shortens {element} notes by about {round((1-length_ratio)*100)}%",
                    "evidence": 1,
                }
            )
        return TasteDelta(updates=updates, candidate_heuristics=candidates)


class MemoryWriter:
    _shared_executor = ThreadPoolExecutor(
        max_workers=1, thread_name_prefix="brain-memory"
    )

    def __init__(self, memory: CognitiveMemory):
        self.memory = memory

    def submit(self, function, *args, **kwargs) -> Future:
        return self._shared_executor.submit(function, *args, **kwargs)

    def update_session(
        self,
        *,
        session_id: str,
        user_id: str,
        subgenre: str | None,
        plan: dict[str, Any],
        tracks: list[dict[str, Any]],
        persist: bool = True,
    ) -> None:
        session = self.memory.get_session(session_id, user_id)
        session.key = plan["key"]
        session.bpm = plan["bpm"]
        session.loop_bars = plan["bars"]
        session.subgenre = subgenre or session.subgenre
        session.last_plan = plan
        for track in tracks:
            session.elements[track["role"]] = compact_track_summary(
                track, plan["bars"]
            )
        if persist:
            self.memory.save_session(session)
        else:
            self.memory.cache_session(session)


class Consolidator:
    def __init__(self, memory: CognitiveMemory):
        self.memory = memory

    def run(self, report_path: Path) -> dict[str, Any]:
        with self.memory.connect() as db:
            promoted = db.execute(
                """
                UPDATE heuristics SET active=1, updated_at=CURRENT_TIMESTAMP
                WHERE evidence>=3 AND contradictions<2 AND active=0
                """
            ).rowcount
            retired = db.execute(
                """
                UPDATE heuristics SET active=0, updated_at=CURRENT_TIMESTAMP
                WHERE contradictions>=2 AND active=1
                """
            ).rowcount
            sessions = db.execute(
                """
                SELECT s.session_id, s.user_id, s.summary, s.closed_at,
                       COUNT(g.id) AS generation_count
                FROM sessions s
                LEFT JOIN generations g ON g.session_id=s.session_id
                WHERE s.closed_at IS NOT NULL
                GROUP BY s.session_id
                """
            ).fetchall()
            compressed = 0
            for row in sessions:
                exists = db.execute(
                    "SELECT 1 FROM session_summaries WHERE session_id=?",
                    (row["session_id"],),
                ).fetchone()
                if not exists:
                    db.execute(
                        """
                        INSERT INTO session_summaries(
                            session_id, user_id, summary, generation_count,
                            closed_at
                        ) VALUES (?, ?, ?, ?, ?)
                        """,
                        (
                            row["session_id"],
                            row["user_id"],
                            row["summary"],
                            row["generation_count"],
                            row["closed_at"],
                        ),
                    )
                    compressed += 1
            outcomes: dict[int, list[int]] = {}
            for row in db.execute(
                """
                SELECT g.fingerprint_ids_json, f.rating
                FROM generations g
                JOIN feedback f ON f.generation_id=g.id
                WHERE f.rating != 0
                """
            ):
                try:
                    ids = json.loads(row["fingerprint_ids_json"])
                except Exception:
                    ids = []
                for fingerprint_id in ids:
                    outcomes.setdefault(int(fingerprint_id), []).append(
                        1 if row["rating"] > 0 else 0
                    )
            for fingerprint_id, kept in outcomes.items():
                # Replay in order as an EMA, matching the immediate loop.
                rate = 0.5
                for value in kept:
                    rate = rate * 0.8 + value * 0.2
                db.execute(
                    "UPDATE fingerprints SET kept_rate=? WHERE id=?",
                    (rate, fingerprint_id),
                )
            heuristics = [
                dict(row)
                for row in db.execute(
                    """
                    SELECT trigger_condition, rule, evidence, contradictions,
                           active
                    FROM heuristics
                    ORDER BY evidence DESC, id DESC LIMIT 20
                    """
                )
            ]
            axes = [
                dict(row)
                for row in db.execute(
                    """
                    SELECT scores_json FROM critic_verdicts
                    WHERE created_at >= datetime('now', '-7 days')
                    """
                )
            ]
            fp_gaps = [
                dict(row)
                for row in db.execute(
                    """
                    SELECT subgenre, COUNT(*) AS count,
                           ROUND(AVG(kept_rate), 3) AS kept_rate
                    FROM fingerprints GROUP BY subgenre ORDER BY count ASC
                    """
                )
            ]
        axis_summary = _axis_summary(axes)
        report = _render_report(
            promoted, retired, compressed, heuristics, axis_summary, fp_gaps
        )
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(report, encoding="utf-8")
        return {
            "promoted": promoted,
            "retired": retired,
            "compressed_sessions": compressed,
            "report": str(report_path),
        }


def compact_track_summary(track: dict[str, Any], bars: int) -> dict[str, Any]:
    by_bar: dict[int, list[dict[str, Any]]] = {}
    for note in track.get("notes", []):
        bar = min(bars - 1, int(note["start_tick"]) // 3840)
        by_bar.setdefault(bar, []).append(note)
    descriptors = []
    for bar in sorted(by_bar):
        notes = by_bar[bar]
        pitches = sorted({note["pitch"] for note in notes})
        onsets = sorted({round((note["start_tick"] % 3840) / 960, 2) for note in notes})
        descriptors.append(
            f"bar{bar + 1}: {len(notes)} notes; pcs "
            f"{[pitch % 12 for pitch in pitches[:6]]}; beats {onsets[:8]}"
        )
    return {
        "note_count": len(track.get("notes", [])),
        "bars": descriptors,
    }


def _axis_summary(rows: list[dict[str, Any]]) -> dict[str, float]:
    values: dict[str, list[int]] = {}
    for row in rows:
        try:
            scores = json.loads(row["scores_json"])
        except Exception:
            continue
        for axis, score in scores.items():
            values.setdefault(axis, []).append(int(score))
    return {
        axis: round(sum(scores) / len(scores), 2)
        for axis, scores in values.items()
        if scores
    }


def _render_report(
    promoted: int,
    retired: int,
    compressed: int,
    heuristics: list[dict[str, Any]],
    axes: dict[str, float],
    gaps: list[dict[str, Any]],
) -> str:
    lines = [
        "# Brain Report",
        "",
        "## Consolidation",
        f"- Promoted heuristics: {promoted}",
        f"- Retired heuristics: {retired}",
        f"- Compressed sessions: {compressed}",
        "",
        "## Top learned heuristics",
    ]
    lines.extend(
        f"- `{item['trigger_condition']}` → {item['rule']} "
        f"(evidence {item['evidence']}, active {bool(item['active'])})"
        for item in heuristics
    )
    lines.extend(["", "## Weekly critic axes"])
    lines.extend(f"- {axis}: {score}/5" for axis, score in sorted(axes.items()))
    lines.extend(["", "## Fingerprint coverage gaps"])
    lines.extend(
        f"- {item['subgenre']}: {item['count']} fingerprints, "
        f"kept-rate {item['kept_rate']}"
        for item in gaps
    )
    return "\n".join(lines) + "\n"

