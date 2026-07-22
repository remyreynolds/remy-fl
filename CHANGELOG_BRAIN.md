# CHANGELOG_BRAIN.md — implementation interpretations

Places the master prompt was ambiguous and how this Python pipeline coded them:

1. **§6 schema vs JUCE plugin schema**  
   The brain’s tick-based multi-track JSON (`meta` + `tracks` + `start_tick`) is the canonical format for this Python pipeline. The existing C++ VST still uses pitch-name / `startBeat` JSON. Both brains coexist: `prompts/house-midi-agent-master-prompt.md` (ticks) and `brain/master-system-prompt.md` (plugin beats).

2. **Perc channel**  
   Spec says channel 9 for perc. We force `channel=9` (0-indexed) → MIDI channel 10 in mido when `role=perc`.

3. **Bass register E1–E2**  
   Interpreted as MIDI 28–40 inclusive. Occasional pitches 41–52 are flagged as errors (no soft pass) to keep the gate strict.

4. **Chromatic allowance “≤1 per bar per track”**  
   Counted as pitch-classes outside the declared key/mode scale; perc tracks skipped.

5. **Swing math**  
   MPC-style: even 16ths unmoved; odd 16ths delayed by `((swing_pct-50)/50) * (sixteenth/2)` ticks at the declared PPQ.

6. **Ambiguous “make me something groovy”**  
   Returns exactly one clarifying question (deep vs tech vs other) instead of generating, matching §7.

7. **Offline / missing API key**  
   Deterministic fallback generator produces schema+music-valid MIDI so unit/integration tests and FL smoke files work without a live LLM. Live calls use Anthropic/OpenAI when keys are set.

8. **Style Library seeding**  
   18 hand-written fingerprints (not real copyrighted transcriptions) across the 8 profiles so retrieval works day one.

9. **Session lock “roots ≥70%”**  
   Enforced in tests against offline bass fallback (F root pc). Live LLM bass is validated for register/scale; root match is logged via session element roots for future scoring.
