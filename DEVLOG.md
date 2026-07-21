# AI MIDI Gen — Development Log

## Phase 1 — Foundation  (status: scaffolded, pending first compile)

### Architecture decisions
- **JUCE 8.0.4 via CMake FetchContent** — no manual JUCE clone; CMake pulls it.
  Chosen so a from-zero machine builds with only Xcode CLT + CMake installed.
- **macOS-first** (overrides the spec's Windows-first, per user's setup). CMake
  stays cross-platform (`CMAKE_OSX_DEPLOYMENT_TARGET`, no OS-specific code yet)
  so Windows/MSVC drops in later with no source changes.
- **Plugin is a MIDI producer, not a synth.** `IS_SYNTH FALSE`,
  `NEEDS_MIDI_OUTPUT TRUE`. It never renders audio (`processBlock` clears the
  buffer). The user's own instrument (Serum/Omnisphere/etc.) makes the sound.
- **Strict AI→instructions→engine pipeline** (per spec): `AIClient` returns a
  structured `MusicParams` + a list of instruments to (re)generate. It never
  emits raw MIDI. `MidiGenerator` renders notes; `validate()` snaps to scale,
  de-overlaps, clamps ranges/velocity, applies swing + humanization.
- **Cloud AI backend = Claude Messages API** (`api.anthropic.com/v1/messages`),
  called on a background `juce::Thread`; result marshalled back to the message
  thread via `MessageManager::callAsync`. Offline `localFallback` keeps the
  plugin useful with no key.
- **Layer separation:** `src/ai`, `src/engine`, `src/gui`, top-level processor/
  editor. Engine + theory have zero JUCE dependency (unit-testable later).

### Completed
- CMake project (VST3 + AU + Standalone), dark `LookAndFeel`.
- `MusicTheory` (scales, root parsing, scale-snapping, diatonic chords).
- `MidiGenerator` with house-oriented generators for all 7 instrument types
  (four-on-floor drums, offbeat bass with gaps, i-VI-III-VII chords, melody
  random-walk, arp, pad) + validator + MIDI-file writer.
- `AIClient` (Claude call + JSON parse + offline fallback).
- Processor: project state, per-part lock/mute, MIDI preview player, save/load.
- Editor: AI chat panel, 7 instrument panels (Generate/Lock/Mute/Drag/Export),
  Generate-All, Preview transport, API-key field.
- External drag-and-drop of `.mid` into FL via `performExternalDragDropOfFiles`.

### Known issues / not yet done
- Not yet compiled on a real toolchain — expect minor JUCE API fixups on first
  build (that's the immediate next step once Xcode CLT + CMake are installed).
- Solo / Preview-per-instrument / Regenerate-vs-Generate distinction: buttons
  exist conceptually but Phase 1 wires Generate+Lock+Mute+Drag+Export only.
- Version history, presets, humanization dials in UI: Phase 4.
- Music-theory validation is basic (scale-snap + overlap). Voice-leading is Phase 3.

### Next steps
1. Install Xcode Command Line Tools + CMake (see README).
2. `cmake -B build -G Xcode` then build the `AIMidiGen_Standalone` target first
   (fastest iteration; no DAW needed).
3. Fix any compiler errors/warnings; commit.
4. Load the VST3/AU in FL Studio on Mac; verify drag-to-playlist.
