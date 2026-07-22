"""Seven-stage cognitive pipeline with a hard seven-call budget."""
from __future__ import annotations

import json
import threading
import time
import uuid
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

from app.context import ContextAssembler
from app.converter import write_combined_and_stems
from app.critic import CriticStage
from app.db import StyleLibrary
from app.finalizer import FinalizerStage
from app.generator import GeneratorStage
from app.learning import MemoryWriter
from app.llm_client import LlmClient, LlmResult
from app.memory import CognitiveMemory
from app.perception import PerceptionStage
from app.planner import PlannerStage
from app.schema import Generation, NoteJSON, Track, Verdict


@dataclass
class PipelineResult:
    ok: bool
    generation: Generation | None = None
    finalized: dict[str, Any] | None = None
    midi_paths: dict[str, Path] = field(default_factory=dict)
    warnings: list[str] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    generation_id: int | None = None
    raw_response: str = ""
    clarifying_question: str | None = None
    attempts: int = 0
    trace_id: str | None = None


class AgentPipeline:
    def __init__(
        self,
        llm: Any | None = None,
        library: CognitiveMemory | None = None,
        sessions: Any | None = None,
        out_dir: Path | None = None,
        *,
        max_calls: int = 7,
    ):
        self.llm = llm or LlmClient()
        self.library = library or StyleLibrary()
        self.sessions = sessions  # accepted for backward compatibility
        self.out_dir = (
            Path(out_dir)
            if out_dir
            else Path(__file__).resolve().parents[1] / "samples"
        )
        self.max_calls = max_calls

    def generate(
        self,
        user_request: str,
        *,
        session_id: str = "default",
        user_id: str = "default",
        max_retries: int = 2,
        require_midi: bool = True,
        mode: str = "Generate",
        input_notes: list[dict[str, Any]] | None = None,
        selection_only: bool = False,
        roll_ppq: int | None = None,
        sound_type: str | None = None,
    ) -> PipelineResult:
        trace_id = uuid.uuid4().hex
        result = PipelineResult(ok=False, trace_id=trace_id)
        self.library.create_trace(trace_id, session_id, user_request)
        trace: list[dict[str, Any]] = []
        budget = _BudgetedLlm(self.llm, self.max_calls)
        input_notes = input_notes or []

        try:
            session = self.library.get_session(session_id, user_id)
            perception = PerceptionStage(self.library, budget)
            intent = _timed(
                trace,
                "perception",
                lambda: perception.perceive(
                    user_request,
                    session.compact(),
                    mode=mode,
                    input_notes=input_notes,
                    sound_type=sound_type,
                ),
            )
            if intent.clarifying_question:
                result.ok = True
                result.clarifying_question = intent.clarifying_question
                self._finish_trace(trace_id, trace, budget, {})
                return result
            if not require_midi and intent.action == "question":
                result.ok = True
                self._finish_trace(trace_id, trace, budget, {})
                return result

            context = _timed(
                trace,
                "context",
                lambda: ContextAssembler(self.library).assemble(
                    intent,
                    session_id=session_id,
                    user_id=user_id,
                    request_text=user_request,
                    reuse_cache=True,
                ),
            )
            plan = _timed(
                trace,
                "planner",
                lambda: PlannerStage(budget).plan(intent, context),
            )

            elements = (
                ["chords", "bass", "melody", "perc"]
                if intent.element == "stack"
                else [intent.element]
            )
            generator = GeneratorStage(budget)
            drafts = _timed(
                trace,
                "generator",
                lambda: self._generate_elements(
                    generator, elements, plan, input_notes
                ),
            )
            combined = _combine(drafts, plan)
            critic = CriticStage(budget)
            verdicts: list[Verdict] = []
            best = combined
            best_rank = (-999, -999)
            warnings: list[str] = []
            max_revision_loops = min(2, max(0, int(max_retries)))

            for attempt in range(1, max_revision_loops + 2):
                verdict = _timed(
                    trace,
                    f"critic_{attempt}",
                    lambda attempt=attempt: critic.review(
                        combined,
                        plan,
                        attempt=attempt,
                        input_notes=input_notes,
                    ),
                )
                verdicts.append(verdict)
                rank = (
                    1 if verdict.code_passed else 0,
                    sum(verdict.scores.values()) - len(verdict.code_errors) * 5,
                )
                if rank > best_rank:
                    best, best_rank = combined, rank
                self.library.log_verdict(
                    generation_id=None,
                    trace_id=trace_id,
                    session_id=session_id,
                    element=intent.element,
                    attempt=attempt,
                    code_passed=verdict.code_passed,
                    code_errors=verdict.code_errors,
                    scores=verdict.scores,
                    revision_note=verdict.revision_note,
                )
                if verdict.passed:
                    best = combined
                    break
                if attempt > max_revision_loops:
                    warnings.append(
                        "Critic revision limit reached; shipped the best attempt."
                    )
                    break
                target = _revision_element(
                    verdict, intent.element, elements
                )
                siblings = [
                    track
                    for draft in drafts
                    for track in draft.tracks
                    if track.role != target
                ]
                revised = _timed(
                    trace,
                    f"generator_revision_{attempt}",
                    lambda target=target, siblings=siblings, verdict=verdict: (
                        generator.generate_element(
                            target,
                            plan,
                            siblings=siblings,
                            revision_note=verdict.revision_note,
                            input_notes=input_notes,
                        )
                    ),
                )
                drafts = [
                    draft
                    for draft in drafts
                    if not any(track.role == target for track in draft.tracks)
                ] + [revised]
                combined = _combine(drafts, plan)

            finalized = _timed(
                trace,
                "finalizer",
                lambda: FinalizerStage().finalize(
                    [best],
                    plan,
                    intent,
                    warnings=warnings,
                    trace_id=trace_id,
                ),
            )
            generation = Generation.model_validate(finalized)
            paths = write_combined_and_stems(
                generation,
                self.out_dir / session_id,
                basename=_slug(user_request)[:40] or "generation",
            )
            fingerprint_ids = [
                int(item["id"])
                for item in context.fingerprints
                if item.get("id") is not None
            ]
            generation_id = self.library.log_generation(
                request=user_request,
                raw_response=json.dumps(finalized),
                output_json=json.dumps(finalized),
                validation_ok=verdicts[-1].code_passed if verdicts else True,
                validation_errors="; ".join(
                    verdicts[-1].code_errors if verdicts else []
                ),
                latency_ms=sum(item["latency_ms"] for item in trace),
                session_id=session_id,
                user_id=user_id,
                element=intent.element,
                intent=intent.model_dump(),
                context=context.model_dump(),
                plan=plan.model_dump(),
                warnings=warnings,
                fingerprint_ids=fingerprint_ids,
                trace_id=trace_id,
                revision_count=max(0, len(verdicts) - 1),
            )
            self.library.link_verdicts(trace_id, generation_id)
            self.library.mark_fingerprints_used(fingerprint_ids)
            for _ in range(max(0, len(verdicts) - 1)):
                self.library.record_regeneration(fingerprint_ids)
            writer = MemoryWriter(self.library)
            # Cache session locks synchronously, persist them off the response path.
            _timed(
                trace,
                "memory_writer",
                lambda: writer.update_session(
                    session_id=session_id,
                    user_id=user_id,
                    subgenre=intent.subgenre,
                    plan=plan.model_dump(),
                    tracks=finalized["tracks"],
                    persist=False,
                ),
            )
            writer.submit(
                writer.update_session,
                session_id=session_id,
                user_id=user_id,
                subgenre=intent.subgenre,
                plan=plan.model_dump(),
                tracks=finalized["tracks"],
                persist=True,
            )
            scores = verdicts[-1].scores if verdicts else {}
            self._finish_trace(trace_id, trace, budget, scores)
            result.ok = True
            result.generation = generation
            result.finalized = finalized
            result.midi_paths = paths
            result.generation_id = generation_id
            result.raw_response = json.dumps(finalized)
            result.warnings = warnings
            result.attempts = max(len(verdicts), budget.calls)
            return result
        except Exception as exc:
            result.errors.append(str(exc))
            result.attempts = budget.calls
            self._finish_trace(trace_id, trace, budget, {})
            return result

    def _generate_elements(
        self,
        generator: GeneratorStage,
        elements: list[str],
        plan,
        input_notes: list[dict[str, Any]] | None = None,
    ) -> list[NoteJSON]:
        drafts: list[NoteJSON] = []
        if elements == ["chords", "bass", "melody", "perc"]:
            chords = generator.generate_element(
                "chords", plan, input_notes=input_notes
            )
            drafts.append(chords)
            bass = generator.generate_element(
                "bass", plan, siblings=chords.tracks, input_notes=input_notes
            )
            drafts.append(bass)
            siblings = [*chords.tracks, *bass.tracks]
            with ThreadPoolExecutor(max_workers=2) as executor:
                future_melody = executor.submit(
                    generator.generate_element,
                    "melody",
                    plan,
                    siblings=siblings,
                    input_notes=input_notes,
                )
                future_perc = executor.submit(
                    generator.generate_element,
                    "perc",
                    plan,
                    siblings=siblings,
                    input_notes=input_notes,
                )
                drafts.extend([future_melody.result(), future_perc.result()])
            return drafts
        siblings: list[Track] = []
        for element in elements:
            draft = generator.generate_element(
                element,
                plan,
                siblings=siblings,
                input_notes=input_notes,
            )
            drafts.append(draft)
            siblings.extend(draft.tracks)
        return drafts

    def _finish_trace(
        self,
        trace_id: str,
        trace: list[dict[str, Any]],
        budget: "_BudgetedLlm",
        scores: dict[str, Any],
    ) -> None:
        self.library.save_trace(
            trace_id,
            trace,
            budget.models,
            budget.tokens,
            scores,
        )


class _BudgetedLlm:
    def __init__(self, inner: Any, maximum: int):
        self.inner = inner
        self.maximum = maximum
        self.calls = 0
        self.tokens = 0
        self.models: dict[str, str] = {}
        self._lock = threading.Lock()

    def has_key(self) -> bool:
        if hasattr(self.inner, "call_json"):
            return bool(getattr(self.inner, "has_key", lambda: True)())
        return hasattr(self.inner, "generate_json")

    def call_json(self, *, stage: str, system: str, user: str, tier: str):
        with self._lock:
            if self.calls >= self.maximum:
                return LlmResult(
                    ok=False,
                    error="LLM call budget exhausted",
                    model="budget-blocked",
                )
            self.calls += 1
        if hasattr(self.inner, "call_json"):
            response = self.inner.call_json(
                stage=stage, system=system, user=user, tier=tier
            )
        else:
            response = self.inner.generate_json(user)
        with self._lock:
            self.tokens += int(getattr(response, "tokens", 0))
            self.models[stage] = getattr(response, "model", "") or tier
        return response


def _timed(
    trace: list[dict[str, Any]],
    name: str,
    function: Callable[[], Any],
) -> Any:
    started = time.perf_counter()
    value = function()
    trace.append(
        {
            "stage": name,
            "latency_ms": int((time.perf_counter() - started) * 1000),
        }
    )
    return value


def _combine(drafts: list[NoteJSON], plan) -> NoteJSON:
    tracks = [track for draft in drafts for track in draft.tracks]
    concerns = [
        draft.plan_concern for draft in drafts if draft.plan_concern
    ]
    return NoteJSON(
        meta={
            "bpm": plan.bpm,
            "key": plan.key,
            "ppq": 960,
            "loop_bars": plan.bars,
            "swing_pct": plan.swing_pct,
        },
        tracks=tracks,
        plan_concern="; ".join(concerns) if concerns else None,
    )


def _revision_element(
    verdict: Verdict,
    requested: str,
    available: list[str],
) -> str:
    text = " ".join(verdict.code_errors).lower()
    for element in available:
        if element in text:
            return element
    if requested in available:
        return requested
    return "bass" if "bass" in available else available[0]


def _slug(value: str) -> str:
    return "".join(
        character if character.isalnum() else "_" for character in value
    ).strip("_").lower()

