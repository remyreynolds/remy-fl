# Cognitive brain v2 (Groovewright)

The brain is a seven-stage cognitive pipeline, not a single system prompt. It
uses Python 3.11, FastAPI, SQLite with FTS5, strict Pydantic contracts, and one
provider client with model routing. The existing 960-PPQ MIDI JSON and relay
contracts remain compatible.

## Setup and run

```bash
python3.11 -m venv .venv
.venv/bin/pip install -r requirements-brain.txt

# Optional live provider. Without a key, deterministic generation stays usable.
export ANTHROPIC_API_KEY=sk-ant-…

# API for the FL relay:
.venv/bin/python -m app.cli --serve

# Direct generation:
.venv/bin/python -m app.cli "deep house chords in F minor, 8 bars"
```

Configure the provider and stage models in `config.toml`. Perception and taste
updates use the small model, planning and musical criticism use the mid model,
and note generation uses the strong model. The pipeline enforces seven LLM
calls maximum per request.

## The seven stages

### 1. Perception — `app/perception.py`
Perception turns a raw message and current session into a normalized `Intent`.
Rules parse actions, elements, keys, BPM, bars, revisions, references, and at
least 20 deterministic mood mappings. A cheap-model fallback runs only for
uncertain requests. Exactly one clarification is returned only when confidence
is below 0.6 and neither a subgenre nor reference is known.

### 2. Context assembler — `app/context.py`
The assembler performs no LLM calls. It retrieves the live session and taste
profile first, then three FTS-ranked fingerprints, two relevant episodes,
the genre card, and at most five triggered heuristics. Lower-priority sections
are dropped to respect the approximate 2,500-token budget. Retrieval layers are
cached by session and intent hash; session and taste data are always refreshed.

### 3. Planner — `app/planner.py`
The planner decides key, mode, BPM, progression, rhythm grids, density,
registers, energy, and influences before any notes exist. Locks resolve in
the order session, explicit/fingerprint profile, taste, genre card, and hard
default. Revision plans begin with the previous plan and record only the
targeted change in `revision_diff`.

### 4. Generator — `app/generator.py`
The generator executes one element at a time from the fixed plan at 960 PPQ.
Chords precede bass; melody and percussion can run concurrently after bass.
Each call receives sibling tracks. Swing offsets, deterministic timing jitter,
and velocity waves are applied while all metadata and registers stay locked to
the plan.

### 5. Critic — `app/critic.py`
The code critic checks scale, register, kick avoidance, loop boundaries,
velocity variance, duplicate notes, and at least 80% rhythm-grid adherence.
Only code-clean drafts reach the musical critic, which scores groove pocket,
harmonic color, repetition fatigue, and genre authenticity. Scores of two or
less produce a concrete revision note. Two revisions are allowed before the
best attempt ships with warnings.

### 6. Finalizer — `app/finalizer.py`
The finalizer is pure code. It merges element drafts, stamps plan-locked
metadata, preserves the influence citation, adds a one-line description, and
offers two variations selected by unexplored parameter distance. It emits the
same `meta` and `tracks` structure consumed by validators, MIDI conversion, the
relay, and the FL Studio script.

### 7. Memory writer — `app/learning.py`
The memory writer immediately caches session locks and persists compact
per-bar note summaries asynchronously. Generation records, critic verdicts,
feedback, and traces become episodic memory. Feedback invokes the taste
updater; MIDI edits can create evidence-backed heuristic candidates. The
nightly consolidator promotes candidates at evidence three, retires rules with
two contradictions, replays fingerprint kept rates, compresses closed
sessions, and writes `brain_report.md`.

## Layered memory

`app/memory.py` applies `app/migrations/001_cognitive_memory.sql`. Working
memory exists only during a request. Session memory stores project locks,
compact element summaries, and the latest plan. Episodic tables retain every
generation, feedback item, critic verdict, and trace. Semantic tables contain
20 seeded fingerprints, 22 mood mappings, eight genre cards, taste profiles,
and evolving heuristics.

Useful commands:

```bash
# Forget one user's taste and heuristics:
.venv/bin/python -m app.cli --reset-taste default

# Run nightly consolidation:
.venv/bin/python -m app.cli --consolidate

# Regenerate the deterministic simulated-week report:
.venv/bin/python -m app.simulate_week
```

## API and observability

The API listens on `127.0.0.1:8000` by default:

- `POST /generate`
- `POST /feedback`
- `GET /health`
- `GET /debug/trace/{trace_id}`
- `POST /session/{session_id}/close`
- `POST /admin/reset-taste/{user_id}`
- `POST /admin/consolidate`

Each trace records stage latency, routed model, token usage, and verdict scores.
Relay configuration should point `backend_url` to this API.

## Tests

```bash
PYTHONPATH="$PWD:$PWD/fl-midi-agent" \
  fl-midi-agent/.venv/bin/python -m pytest tests fl-midi-agent/tests
```

The live eight-subgenre test is skipped unless `ANTHROPIC_API_KEY` or
`OPENAI_API_KEY` is configured. Offline tests exercise the same contracts,
critics, revision limits, session locks, feedback learning, consolidation,
FastAPI trace retrieval, relay, and FL script logic.
