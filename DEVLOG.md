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

## Phase 1.1 — Live build verified; Drums reorganized into a real kit

### Status
First successful compile + launch of `AIMidiGen_Standalone` confirmed on
target Mac (Unix Makefiles generator, no full Xcode required — Command Line
Tools + Apple Clang is sufficient). Live Claude Messages API integration
confirmed working end-to-end with a user-supplied key.

### Fixed
- Missing `#include <juce_events/juce_events.h>` in `AIClient.cpp`/`.h` —
  `juce::MessageManager` lives in a separate module from `juce_core`.

### Changed — Drums is now a kit, not a blob
User feedback after first successful MIDI export: dragging "Drums" produced
one file with kick/snare/clap/hats all interleaved — no way to generate,
lock, mute, or drag any single piece independently, unlike every other
instrument.

- `MusicInstructions.h`: new `DrumPiece` enum (Kick/Snare/Clap/ClosedHat/
  OpenHat) + `toString()` + `drumPieceMidiNote()` (GM drum map, ch. 10).
- `MidiGenerator`: `generateDrumKit()` builds all 5 pieces as independent,
  individually-validated `GeneratedPart`s; `generateDrumPiece()` regenerates
  just one; `generateDrums()` is now a thin "flatten the kit onto one
  timeline" convenience used for the combined loop export.
- `AIMidiGenProcessor`: owns a `drumKit` array alongside the existing
  `parts` array. `generateDrumKit()`/`generateDrumPiece()` respect per-piece
  locks; `rebuildDrumMasterPart()` keeps `part(Drums)` as a mute-aware merged
  view for the "drag full loop" shortcut. Preview playback now walks the 5
  drum-piece sequences directly (live per-piece mute) instead of one merged
  Drums sequence.
- New `DrumKitPanel` (GUI): replaces the single "Drums" `InstrumentPanel`
  cell with 5 rows (Kick/Snare/Clap/Closed Hat/Open Hat), each with its own
  Generate/Lock/Mute/Drag, plus a "Generate All" / "Drag Full Loop" pair.
- `PluginEditor`: skips building a generic `InstrumentPanel` for
  `InstrumentType::Drums`; wires `DrumKitPanel` into the same grid cell.

### Known follow-ups
- AI prompt schema still only knows the coarse `"Drums"` instrument name —
  asking Claude to "just change the hi-hats" won't yet target one piece.
  Fine-grained AI control over individual drum pieces is a later iteration.
- API key still not persisted across restarts (Keychain — tracked separately).

## Phase 1.2 — Remy CRM UI pass

### Source
Pulled a design handoff from the `crm-agent` so the plugin follows the same
Remy CRM visual language instead of generic audio-plugin styling.

### Changed
- Reworked `CustomLookAndFeel` around a near-black, monochrome-first token set:
  base background, raised surfaces, muted text, hairline dividers, and restrained
  active-state highlights.
- Standardized controls on the Remy CRM shape language: flat fills, 6px corner
  radius, subtle hover/pressed states, and divider outlines instead of bevels,
  shadows, gradients, or saturated colors.
- Updated the editor header into a compact SaaS dashboard-style control band
  with title, subheader, Claude API status, primary actions, and a circular
  "Parts ready" meter inspired by the CRM dial panel.
- Flattened chat, instrument, and drum-kit panels to use consistent spacing,
  small bold labels, low-contrast dividers, and compact row layouts.

### Validation
- Could not run CMake in this container because `cmake` is not installed here.
  Build validation still needs to happen on the Mac toolchain.

## Phase 1.3 — Modern house style engine (8 sub-genre presets, 10-piece kit)

### Why
"House" isn't one groove in 2026. The engine had a single hardcoded pattern
(four-on-floor + offbeat bass + i-VI-III-VII). This pass makes style a
first-class, data-driven concept so the plugin generates idiomatic ideas in
the lanes that are actually hot right now.

### Added
- `engine/StylePresets.h` (JUCE-free, header-only): 8 presets —
  Tech House, Bass House, Afro House, Melodic House, Deep House,
  Organic House, UK Garage, Classic House. Each carries bpm, swing, default
  scale, chord voicing depth (triad→11th), chord rhythm mode, bass style,
  a 4-bar progression, and per-piece 16-step drum groove templates with
  per-step trigger probability. `findStyle()`/`findStyleOrNull()` match
  names, aliases and artist cues ("john summit"→Tech House,
  "keinemusik"→Afro House, "anyma"→Melodic House, "2-step"→UK Garage).
- `DrumPiece` extended 5→10: + Ride, Shaker, Rim, CongaHi, CongaLo (GM
  notes 51/70/37/63/64). All processor/preview/panel code scales off the
  enum, so the kit UI and per-piece lock/mute/drag picked them up unchanged.
- `MidiGenerator`:
  - Drums are now groove-template driven (velocity steps × probability ×
    energy dial) instead of hardcoded; UK Garage gets a real 2-step kick.
  - 5 bass behaviours (offbeat 8ths, rolling tech-house 16ths with octave
    pops, sustained subs, afro syncopated stabs, UKG 2-step sub with
    root→5th→b7 movement).
  - 4 chord modes (stabs / offbeat piano stabs / sustained / plucked) with
    voicings up to 11ths via the extended `theory::diatonicChord(tones)`.
  - Arp follows the style's progression; pad always renders the sustained
    voicing (no longer derived by filtering the chord part).
  - 1/16 shuffle swing added in `validate()` (needed for UKG/afro feel).
- `AIClient`: system prompt now builds its genre list directly from
  `allStyles()` (prompt and engine can't drift); offline fallback detects
  styles by keyword and adopts the preset's bpm/swing/scale.
- `PluginEditor`: Style combo-box in the header — switching a style adopts
  its defaults and regenerates all unlocked parts; selection stays in sync
  when the AI changes genre via chat.
- `install-mac.sh`: one-command clone+build+install for the Mac
  (`bash <(curl -fsSL …/install-mac.sh)`).

### Validation
- Engine headers compile clean with `g++ -std=c++17 -Wall -Wextra` (Linux)
  and a sanity harness verifies: 8 styles, valid bpm/swing/progressions,
  kick patterns present, chord builds per style scale, keyword lookups, and
  the 10-piece GM mapping. Full JUCE build still happens on the Mac.
