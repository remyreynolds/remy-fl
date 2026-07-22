"""Generate brain_report.md from a deterministic simulated week."""
from __future__ import annotations

import tempfile
from pathlib import Path

from app.learning import Consolidator
from app.memory import CognitiveMemory


def generate_report(output: Path | None = None) -> Path:
    target = output or Path(__file__).resolve().parents[1] / "brain_report.md"
    with tempfile.TemporaryDirectory(prefix="midi-brain-week-") as directory:
        memory = CognitiveMemory(Path(directory) / "week.db")
        fingerprints = memory.retrieve_fingerprints(
            subgenre="tech_house", limit=3
        )
        ids = [item["id"] for item in fingerprints]
        for day in range(7):
            generation_id = memory.log_generation(
                request=f"simulated tech house bass day {day + 1}",
                output_json='{"meta":{},"tracks":[]}',
                validation_ok=True,
                session_id=f"week-{day}",
                element="bass",
                fingerprint_ids=ids,
                trace_id=f"sim-{day}",
                revision_count=1 if day in {1, 4} else 0,
            )
            memory.add_feedback(
                generation_id,
                1 if day not in {1, 4} else -1,
                "kept the tight bass" if day not in {1, 4} else "less busy",
            )
            memory.log_verdict(
                generation_id=generation_id,
                trace_id=f"sim-{day}",
                session_id=f"week-{day}",
                element="bass",
                attempt=1,
                code_passed=True,
                code_errors=[],
                scores={
                    "groove_pocket": 4,
                    "harmonic_color": 3,
                    "repetition_fatigue": 3 if day != 4 else 2,
                    "genre_authenticity": 4,
                },
                revision_note=None,
            )
        memory.add_heuristic(
            "subgenre=tech_house AND element=bass",
            "user keeps outputs with fewer than 6 distinct pitches",
            evidence=4,
        )
        memory.add_heuristic(
            "element=chords",
            "shorten stabs near the loop boundary",
            evidence=3,
        )
        memory.add_heuristic(
            "element=melody",
            "prefer octave jumps",
            evidence=1,
        )
        Consolidator(memory).run(target)
    return target


if __name__ == "__main__":
    print(generate_report())

