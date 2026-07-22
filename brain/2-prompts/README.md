# 2-prompts — WHAT THE BRAIN IS TOLD (edit rarely, carefully)

The system prompts loaded verbatim at runtime. These define the AI's job,
its output contract, and its musical rules. **Editing these changes behavior
globally — prefer editing `1-knowledge/` for taste changes.**

## Files

### `house-midi-agent-master-prompt.md` ← THE ACTIVE PROMPT
Loaded by `app/prompt_loader.py` for every LLM call in the pipeline.
Guardrails: the loader refuses to start if this file is missing, empty, or
doesn't look like the house-agent master file. Keep the "MASTER SYSTEM
PROMPT" / "Groovewright" heading intact.

### `master-system-prompt.md`
The original long-form master spec (reference/history). Not loaded at
runtime. Fold improvements into the active prompt above deliberately.

## Rules
- One prompt change at a time; generate a few patterns; compare before/after.
- Musical taste belongs in `1-knowledge/`, not hardcoded in prompts.
- Never inline prompts as string literals in `app/` code — files only.
