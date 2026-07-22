# 3-memory — WHAT THE BRAIN LEARNED (auto-generated — do NOT hand-edit)

Everything in here is written by the brain itself. You read it; you don't
edit it.

## Files

### `style_library.db` (created on first run, not committed)
The SQLite memory: sessions, episodic generation history, critic verdicts,
your feedback, taste profiles, promoted heuristics, seeded fingerprints.
Delete it to start the brain from a blank slate (seeds reload from
`1-knowledge/`).

### `brain_report.md`
Human-readable report from the nightly consolidator: what heuristics got
promoted/retired, fingerprint kept-rates, taste drift. Regenerate any time:

```bash
python -m app.cli --consolidate       # real data
python -m app.simulate_week           # deterministic demo report
```

## How learning gets IN here
1. **Feedback** — every 👍/👎 via `POST /feedback` updates your taste profile.
2. **MIDI edits** — edits you make become evidence for heuristic candidates.
3. **Consolidation** — promotes rules at 3 pieces of evidence, retires at 2
   contradictions.

If this folder never changes, the feedback loop isn't wired up — that means
the brain is NOT learning, and fixing that plumbing is the priority.
