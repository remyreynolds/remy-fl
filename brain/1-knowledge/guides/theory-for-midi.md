# Theory for MIDI generation (house-focused)

Operational rules distilled for Groovewright. Prefer groove over academia.

## Scales (build with W = whole, H = half)
- **Major:** W W H W W W H
- **Natural minor:** W H W W H W W
- **Harmonic minor:** raise 7 of natural minor (leads to tonic)
- **Melodic minor (asc):** raise 6 and 7 of natural minor
- **Modes from major:** Ionian, Dorian (♭3 ♭7), Phrygian (♭2), Lydian (♯4),
  Mixolydian (♭7), Aeolian, Locrian
- **Pentatonic:** omit avoid-notes; extremely safe for hooks

House defaults: Aeolian / Dorian; Phrygian for dark tech; Mixolydian for
French/funky; harmonic minor sparingly for Afro/melodic tension.

## Melody over chords
- Strong beats → chord tones (1, 3, 5, 7)
- Weak beats → neighbor / passing / approach tones
- Shared tones between adjacent chords = glue
- Avoid landing the ♭3 against a major chord’s 2/9 clash unless intentional

## Harmony toolkit
- Triads + 7ths first; add 9/11 for deep/pad color
- Inversions for bass/voice-leading (don’t always root-position block)
- Partial chords / omit root when bass carries it (stabs)
- Extended: 9, 11, 13 — keep top-note motion smooth
- Voice motions: parallel, similar, contrary, oblique — prefer contrary or
  oblique for pads; compact parallel OK for stabs

## Progression craft
- Movement patterns: 3rds, 4ths, 5ths, 6ths (descending 3rds / 5ths common)
- Mixture: borrow from parallel major/minor (e.g. iv in major, ♭VI in minor)
- Applied/secondary dominants for tension into a target chord
- Suspensions resolve (4→3, 2→1) — use for anticipations into downbeats
- Analyze: find home chord → diatonic set → explain non-diatonic (mixture /
  applied / chromatic passing)

## Rhythm (MIDI)
- 4/4 grid; assume kick on quarters even if not generated
- Swing late offbeat 16ths (~54–58% deep/garage; ~50–53% tech/prog)
- Syncopation budget: 2–4 hits/bar for stabs; more → UKG/garage feel

## Role registers (MIDI)
- Bass fundamentals: ~E1–E2 (MIDI 28–40)
- Chords/stabs: ~C3–C5
- Melody/lead: ~C4–C6
- Leave low-end space for kick; bass avoids downbeat collisions
