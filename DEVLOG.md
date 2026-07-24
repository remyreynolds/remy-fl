# AI MIDI Gen — Development Log

## 2026-07-23 — Brain-grounding audit, two new Serum-style sounds, hum-to-chords

**Audit: "MIDIs suck" — is generation actually routing through the Brain?**
Traced every generation entry point (`generatePreferredAll`, `generatePreferredLane`,
`newIdeaPreferred` in PluginProcessor.cpp, and `AIClient::requestMidiPattern`).
Confirmed: whenever offline mode is off and an API key is present, every
request goes through Claude with `KnowledgeBase::retrieveForQuery()` +
`masterPromptText()` injected into the system prompt — grounding is wired
correctly, no routing bug found. The likely real cause of "MIDIs suck" is
generating **without** a Claude key configured (or with Offline mode on),
which silently falls back to the local rule-based `SongPlan`/`MidiGenerator`
engine — competent and always in-key, but inherently simpler than Claude's
output. The honest status badge from the previous entry now makes this
visible at a glance ("Local engine" vs "Claude ready").

**More sounds (Serum-style palette expansion).** Added two new `PartTimbre`
values built on the existing 7-voice-unison + resonant SVF engine (the same
machinery behind `SuperSaw`):
- `GrowlBass` — sub sine + detuned saw through an LFO-wobbled resonant
  filter (~5.2 Hz wobble), classic Serum "growl bass" character.
- `AiryPad` — wider-detune unison pad, slow attack, smooth non-resonant
  top-end filter, longer register-dependent release.
Both wired into `PreviewSynth::startNote/renderNextBlock/stopNote` and added
as selectable variants (not defaults) for House/Techno Bass and Pad parts in
`PreviewSounds.cpp`.

**Hum-to-chords.** New capability: hum a melody into the mic and get an
in-key chord progression on the Chords lane.
- `engine/PitchDetector.h` — small JUCE-free RMS-gated autocorrelation
  pitch tracker (70–1000 Hz), unit tested against silence and a synthesized
  220 Hz tone.
- `engine/HumToChords.h` — pure function: each held hummed note snaps to
  the project scale, becomes a diatonic chord (3/7/9-tone by
  `chordComplexity`) voice-led from the previous chord via the existing
  `theory::voiceLead`. Unit tested for in-key output and empty-input safety.
- `HumCaptureCallback.h` + a "Hum chords" toggle button in the Generate
  rail: opens a **standalone** `juce::AudioDeviceManager` mic input
  (independent of the host's audio graph — no changes to the plugin's own
  `BusesProperties`/`processBlock`, so no host-compatibility risk), tracks
  live note segments, and on stop converts the captured melody into a
  `GeneratedPart` applied straight to `processor.part (InstrumentType::Chords)`.

### Validation
- `EngineTests` gained "Hum-to-chords produces in-key chord progressions"
  and "PitchDetector detects tones and rejects silence"; full native suite
  (`MidiPatternTests`, `EngineTests`, `GeneratorAuditTests`) green.
- Full CMake+Ninja build (VST3/Standalone) green after the sounds and
  editor/processor changes.

## 2026-07-23 — Ease-of-use pass: key testing, help overlay, genre-smart tempo

Three strategic usability items, aimed at a beginner producer's first session:

1. **API key test + honest engine badge.** New `AIClient::testConnection()`
   fires a 1-token live request off-thread (alive-flag guarded) and reports a
   plain-English verdict: "Connected — <model> is ready", key rejected (401/403),
   rate-limited (429), or no internet. Settings gained a "Test key" button and
   a colour-coded status line under the key field; result is also echoed to
   chat. The header badge now shows three truthful states — "Claude ready" /
   "Offline mode" / "Local engine" — and repaints when the key or offline
   toggle changes (before, it claimed "Connected" merely because a key string
   existed).
2. **Help overlay.** New "?" title-bar button opens a dimmed overlay
   (quick-start steps, Space-bar preview, instant chat commands). Auto-shows
   once on true first run via a `helpSeen` flag persisted in plugin state;
   dismiss by button or clicking outside (deferred delete — never
   mid-callback).
3. **Genre-smart tempo.** Switching genre now lands BPM + swing on that
   style's sweet spot (`findStyle` preset) so "pick Trap, hit Generate" no
   longer plays at house tempo. Skipped when host-sync is on or the user set
   their own BPM this session (`setBpmFromUser` marks the override from the
   BPM well, +/- buttons, and chat "128 bpm" intents; not persisted). Chat
   announces e.g. "Trap → 140 BPM (genre sweet spot — edit the BPM well to
   take over)". Restore/DNA/chat-intent paths pass `applyTempo=false` so a
   saved project's tempo is never stomped.

Verified: full ninja build (VST3 + Standalone), MidiPatternTests,
EngineTests, GeneratorAuditTests (144 parts, 0 failures) all green.

## 2026-07-23 — Deep audit: full Linux CI build, validator repairs, 39 findings fixed

### What ran
- **Full plugin build now works on the Linux dev box** (previously mac-only):
  JUCE via FetchContent, cmake+ninja from a pip venv, X11/ALSA/freetype/curl
  dev headers unpacked root-free into `/tmp/juce-sysroot`. VST3
  (`ComposerAI.so`) + Standalone link end-to-end. New CMake option
  `AIMIDIGEN_COPY_PLUGIN=OFF` skips the copy-to-user-plugin-folder step where
  that folder isn't writable (build servers).
- **New property-test target `GeneratorAuditTests`** (tests/GeneratorAuditTests.cpp):
  every style × 3 seeds × 3 keys/scales × 4/8 bars — asserts in-key pitches,
  notes inside the loop, no zero-length notes, velocities in (0,1], no
  same-pitch overlaps (stuck notes), bass below melody, real 3+-note chords,
  kick/hat presence, and seed determinism. 144 parts audited per run.
- Python brain suite: 51/51 green (fixed a stale seed-count assertion in
  `tests/test_cognitive_brain.py` — now derives expected counts from the seed
  JSON files instead of hardcoding).

### Engine bugs the audit caught (fixed in `MidiGenerator::validate`)
1. Pitch snapped to scale **before** clamping to 0–127, so extreme pitches
   could clamp back out of key. Now clamp → snap → octave-fold.
2. Zero/negative note lengths were never repaired. Now floored to 1/16.
3. Notes starting past the loop end survived; tails could spill past the loop.
   Now dropped/clamped, including after swing/humanize shifts.
4. Same-pitch duplicate hits at the same instant (drum ghost layers) survived
   de-overlap with a length floor that guaranteed continued overlap — the
   classic stuck-note bug. Now deduped (louder hit kept) and the de-overlap
   shortening can no longer re-overlap.

### Audit findings fixed across AI + GUI layers
- AI layer (18 findings): use-after-free risks in detached-thread/async
  lambdas (AIClient + processor completion callbacks), notes past loop end in
  Claude JSON accepted unclamped, multi-lane export dropping lanes, duplicate
  master-prompt injection (token waste), API key not trimmed, no 429 retry,
  master-system-prompt.md clobbered on startup, missing project context on
  regenerateFromAI, missing temperature on the pattern path.
- GUI layer (21 findings): stale preview/mute/lock state on editor reopen and
  after undo, dead controls (TrackDetail midi combo, discarded Export),
  lane-generate buttons bypassing Claude, sticky "Composing…" UI, failures
  invisible outside the Chat tab, chat auto-scroll fighting the user, fake
  "0.8 s" telemetry, hidden BPM minus button, unusable 28px volume slider,
  fake macOS traffic lights, space-bar stealing focus from controls.
- Companion UI (ui/): vitest config was missing the `@` path alias that
  vite.config.ts had, so `App.test.tsx` could not resolve imports; also fixed
  a stale "relay connected" assertion (badge now reads "connected").
  UI suite: 3/3 files, 8/8 tests green.

### Verification after the fix round
Full `ninja` rebuild clean; `MidiPatternTests` (49 assertions),
`EngineTests`, `GeneratorAuditTests` (144 parts, 0 failures), pytest 51/51,
and the React vitest suite all pass.



### Original cause
`SongPlan.h` built harmony from a single fixed `findStyle(p.genre).progression`
and ignored `MusicParams::seed`. Generate / New Idea / Regenerate always used
the local `MidiGenerator`, so every take in a genre repeated the same chords.
Claude + Brain were only on Vary / chat MIDI paths; API failures on the legacy
`sendPrompt` path could silently fall back to a local interpretation.

### Files changed
- `src/engine/SongPlan.h` — seed-selected progression templates (1/2/4/8),
  substitutions, inversion colour, turnarounds; shared plan fingerprint
- `src/engine/GenerationReport.h` — Claude / local / failure status labels
- `src/PluginProcessor.*` — preferred Generate / New Idea / Vary Chords paths,
  explicit “Use local generator” after Claude failure (no silent substitute)
- `src/PluginEditor.*` — Offline toggle, Use local button, clear chat status
- `src/ai/AIClient.*` — full Brain master prepended to every MIDI request;
  remove silent local fallback on API failure; local fingerprint memory hooks
- `src/ai/KnowledgeBase.*` — `masterPromptText()` from corpus or binary data
- `src/ai/MidiPattern.*` — require `progression` / `chords` metadata for chord JSON
- `tests/EngineTests.cpp`, `tests/MidiPatternTests.cpp` — seed harmony + source labels

### How Claude / Brain generation works now
With an API key and Offline mode off, Generate / New Idea call Claude with:
1. the full bundled master Brain prompt in the system message,
2. retrieved knowledge references,
3. project key/BPM/bars/genre locks,
4. the last eight chord fingerprints to avoid,
5. one automatic retry on an exact fingerprint repeat.
Success is reported as **Generated with Claude**.

### How offline generation is identified
No API key, Offline toggle on, focus-filter local regen, or an explicit
**Use local generator** click after a Claude error → **Generated locally — …**
and never labeled as Claude. Failed Claude requests leave MIDI unchanged and
show the exact API/parse error.

### Test results
- `EngineTests`: passed (seed-aware SongPlan, shared harmony, New Idea fingerprint escape, source labels, offline in-key)
- `MidiPatternTests`: passed (chord metadata required, fingerprint, MIDI sequence export)
- `ctest`: 2/2 passed
- VST3 + Standalone: built successfully

## 2026-07-22 — Efficient Brain + persistent reference MIDI

- Reorganized the runtime Brain into a short priority-based decision pipeline:
  locks → text brief → project state → references → defaults.
- `MIDI DNA` now saves a reusable local style profile with tempo, tonal center,
  density, velocity, syncopation, and drum-role coverage.
- The source MIDI and its note sequence are never stored in Brain memory, and
  the prompt explicitly forbids copying melodies, riffs, or note order.
- Retrieval blends no more than three reference-MIDI profiles per generation
  so added music informs the result without flooding or confusing Claude.

## 2026-07-22 — Brain-led harmony variation

- Bundled the full master Brain prompt into the plugin binary so Claude receives
  it regardless of the DAW's working directory.
- Every AI generation now carries a unique variation nonce and explicit rules
  that translate descriptive text into harmony, rhythm, register, voicing,
  tension, and density.
- Added an eight-result chord-progression memory. Recent harmonic fingerprints
  are sent to Claude as exclusions, and an exact repeat triggers one automatic
  recomposition request before the MIDI is accepted.

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
- API key is persisted via `~/Library/Application Support/AIMidiGen/` settings
  (env var `ANTHROPIC_API_KEY` still wins when set).

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

---

## Phase 1.4 — Song plan, critic, MIDI DNA (2026-07-22)

Closed the four real gaps from the arrangement critique: lanes could
disagree harmonically, nothing reviewed the arrangement as a whole, MIDI
packs were audition-only, and style choice gave no sound-design guidance.

### Added
- `engine/SongPlan.h` (JUCE-free): ONE deterministic harmonic skeleton per
  MusicParams — progression from the style preset, chords voice-led bar to
  bar (close-position around the previous chord's centre via new
  `theory::voiceLead`). Chords, pad, arp, and the critic all derive from
  the same plan, so separately-generated lanes can never disagree about
  the harmony. Only rhythm/feel uses the RNG.
- `engine/Critic.h` (JUCE-free): cross-part review + repair that runs after
  EVERY generation path (manual button, style switch, drum reroll, AI
  chat) — melody/counter-melody strong beats snapped to chord tones, bass
  clamped to E1..G3, bass notes nudged a 1/16 off kick hits for busy bass
  styles (sustained-sub/garage keep their downbeats), dense chord stacks
  flagged. Locked lanes are never touched. Summary posted to the chat
  panel so the user sees what was repaired.
- `engine/MidiDna.h/.cpp`: "MIDI packs as DNA". Load MIDI DNA button →
  analyze a .mid: channel-10 onsets become per-piece 16-step grooves that
  override the style preset for the pieces the loop covers; pitched notes
  vote a root + scale (major/minor/dorian, pitch-class weighted) which is
  adopted into the project key. Analysis only — the file's notes are never
  copied. Unlocked parts regenerate immediately with the learned feel.
- `MidiGenerator`: turnaround fills every 4th bar (velocity-ramped snare or
  clap on steps 13–16); hat/shaker/ride density now scales with the energy
  dial; DNA groove override in `generateDrumKit`.
- Style switch now posts the preset's recommended patches/kits ("Sound
  picks: …") to chat — the MIDI-first answer to "default kits per style":
  final tone lives in the DAW, so we tell the producer what to load there.

### Validation
- g++ harness extended: SongPlan determinism + voice-leading distance,
  chordAtBeat clamping, critic repairs (chord-tone snap, bass clamp, kick
  clash nudge) and lock-respect — all green on Linux. JUCE build on Mac.

---

## Phase 1.5 — Merge with the parallel UI/AI session (2026-07-22)

Merged this branch's musical engine (SongPlan, Critic, MIDI DNA, style-driven
generator) with the parallel session's product work: 4-surface UI
(Generate/Browse/Chat/Settings), undo snapshots, built-in preview synth +
genre timbres, sample & MIDI-loop libraries, host BPM sync + host MIDI out,
multi-track export, and the note-JSON AI path (Claude returns validated
patterns; chat converses by default). Resolutions of note:
- `MidiGenerator` keeps the style-preset engine (the randomized progression
  rewrite was dropped) but adopts the motif-based melody, per-part MIDI
  channels in `toSequence`, and `writeTempMultiTrackMidiFile`.
- The critic now also runs after `regenerateFromAI`, `transformPartWithAI`,
  and `handleChatTurn` pattern applies; its summary is posted to chat.
- MIDI DNA lives as a ghost button on the Generate surface rail; one undo
  step covers the whole DNA regenerate.
- Genre changes (combo or auto-detect in chat) post the style's "Sound
  picks" patch guidance to the chat panel.

---

## Phase 1.6 — Musicality & usability round 2 (2026-07-22)

Seven improvements across usability, sound, and melody↔chord sync:

1. **Humanize v2** — musical velocity accents (downbeats > offbeat 8ths >
   16th "e"/"a") layered under the random jitter, scaled by the humanize
   dial; ghost snare/rim ticks on the "a" before backbeats.
2. **Melody passing tones & resolution** — strong beats sit on chord tones
   (small chord-tone leaps), weak beats move stepwise through the scale,
   each bar's last note APPROACHES the next bar's chord tone by step, the
   loop's final note lands on the opening chord and rings; avoid-notes
   (semitone above a chord tone) are auto-shortened into passing colour.
3. **Call-and-response counter-melody** — counter lane rebuilds the melody
   with the same dice, finds its gaps (≥ 1 beat), and answers inside them
   with sparse chord-tone figures; no more two-leads-talking-over-each-other.
4. **8-bar A/B song plans** — `buildSongPlan` substitutes a deterministic
   cadential turnaround degree on the final bar of every 8-bar phrase
   (dominant, or subdominant when the progression already ends dominant);
   bass now reads plan degrees, so every lane follows the cadence.
5. **Sidechain pump in the preview** — the pitched mix ducks ~55% under
   every kick with a ~110 ms release, applied before drums are summed;
   previews now pump like a house record.
6. **"New idea" button** — one click: fresh seed → regenerate all unlocked
   lanes → critic pass → summary in chat; one undo step reverses it.
7. **Onboarding** — first-open quick-start message in chat (workflow,
   locks, drag/export, DNA, chat prompts) and a TooltipWindow so the
   existing per-control tooltips actually display.

### Validation
- g++ harness extended: 8-bar cadence (first half untouched, bar 8 swaps
  to the cadence degree, 4-bar loops unaffected, cadence bar still
  voice-leads within an octave for all 8 styles) — all green on Linux.
  Full JUCE build on the Mac via install-mac.sh.

---

## Phase 1.7 — Local-first chat director (2026-07-22)

The chat feature was slow and unreliable because EVERY message — even
"make it faster" — did a full network round-trip to the LLM and asked it
to type raw MIDI note JSON. Redesigned as a two-layer director:

**Layer 1 — instant local commands (0 ms, no network).**
New JUCE-free `src/engine/ChatDirector.h`: `parseChatIntent()` maps chat
text onto a `ChatIntent` (action + target lanes/drum pieces + param
changes). Recognised vocabulary:
- generate/vary verbs + lane words (bass, melody/lead/hook, chords/keys,
  pad, arp, counter, drums/beat) and drum pieces (kick, snare, clap,
  open/closed hats, ride, shaker, rim, congas, percussion)
- "generate everything", "new idea"/"surprise me", "undo", "help"
- tempo ("128 bpm", "at 122", "faster"/"slower"), bars ("8 bars")
- key ("in f# minor", "eb dorian", bare mode names, "darker"/"brighter")
- style-preset names/keywords (whole-word matched; lane-colliding
  keywords like "bassline" are excluded so they never mis-switch genre)
- feel dials: harder/chill (energy), busier/simpler (density),
  swing/shuffle, tight/quantized vs loose/human (humanize)
`AIMidiGenProcessor::tryLocalChatCommand()` executes the intent through
the deterministic engine (fresh seed, undo snapshot, critic pass) and
replies instantly ("⚡ style -> Afro House, regenerated bass + kick.").
Param-only changes regenerate unlocked lanes when musical (key/style/
bars/dials); BPM-only just retimes playback.

**Layer 2 — the AI only for real conversation, now grounded.**
Questions ("how does…", "why does…"), advice-seeking ("feedback",
"suggestions" — never regenerates even when a lane is named), and
anything unrecognised falls through to the LLM. Before every AI turn the
processor injects `buildProjectContextBrief()` — style, key, BPM, bars,
feel dials, per-lane note counts + lock/mute state, drum-kit contents,
DNA status, last critic pass — via `AIClient::setProjectContext()`, so
answers reference the actual project instead of guessing. The same brief
rides along on the AI MIDI-generation path.

### Validation
- Harness now versioned at `tests/EngineTests.cpp` (plain g++, no JUCE):
  30+ intent parses asserted — lanes, pieces, tempo/bars/key/genre/dials,
  question vs command routing, "a minor" article disambiguation,
  advice-seeking safety, opposing-word cancellation. All green on Linux;
  full JUCE build on the Mac via install-mac.sh.

## Phase 1.8 — Deep audit: correctness, thread-safety, music quality (2026-07-23)

Ran the standing 11:30pm deep-audit order: four parallel read-only audits
(processor, engine, AI layer, editor) produced ~65 findings; everything
critical or high-value was fixed, committed granularly, and pushed.

### What was actually broken (now fixed)
- **Installer never verified a successful build** — install-mac.sh checked
  for the old "AI MIDI Gen" bundle names while CMake produces
  "ComposerAI.vst3/.component". Fixed + stale-bundle cleanup.
- **State didn't persist** — swing/humanize/densities/seed/toggles,
  per-lane notes/locks/mutes, drum-kit lanes, and the critic summary were
  all dropped on save/reload. getState/setState now round-trips the whole
  project (flat note arrays), then resyncs sounds, samples, and sequences.
- **Audio-thread races** — host-tempo adoption mutated params and warm-up
  rebuilt sequences on the audio thread. Now: atomic bpm/bars mirrors,
  AsyncUpdater for adoption/rebuilds, try-locks in the MIDI collectors.
- **Undo was lossy and mis-ordered** — snapshots now include genre mode,
  timbres, and gains; exactly one snapshot is taken *before* a chat
  command mutates params; undo resyncs preview sounds.
- **AI drums all landed on the kick** — AI-generated drum lanes are now
  split by GM pitch into the right kit pieces (locks respected); insane
  AI bpm/bars values are rejected.
- **Music-theory bugs** — snapToScale used non-circular pitch-class
  distance (notes below the root snapped the wrong way), diatonicChord
  folded upper extensions back into clusters (%12), voiceLead deleted
  collided tones (9th/11th chords shrank to triads). All corrected;
  engine tests still green.
- **AI client races + bad model ids** — background request threads read
  live provider/key/model members the UI could mutate mid-flight; they
  now capture an immutable Endpoint snapshot by value. Default model
  "claude-sonnet-5" (invalid) → claude-sonnet-4-5, with settings
  migration and real ids in the picker. Truncated (max_tokens) replies
  get a clear error instead of "could not parse"; out-of-range
  velocities clamp instead of rejecting whole patterns; HTTP errors are
  provider-neutral; knowledge retrieval only biases groove docs on the
  generation path and no longer emits a dangling MASTER banner.
- **Editor races** — the 2.4s "Composing…" auto-clear re-enabled buttons
  while async AI requests were still running (callbacks own clearing
  now); per-piece drum mutes were clobbered by lane solo/mute (tracked
  separately, OR-combined now); failed vary/continue left the previous
  success line in the last-run meta.

### Music-quality cleanup
- Drum-kit generation is seeded (deterministic per seed like every lane).
- Melody resolution note no longer rings past the loop boundary.
- Critic drops bass notes clashing with the kick instead of shoving them
  onto an occupied 16th (which doubled the low end).
- Arp accents downbeat 16ths (0.8/0.6) instead of flat 0.7; ghost notes
  audible (0.20–0.35); pads end at 3.95 beats for clean bar retriggers.
- PreviewSynth: integer-LCG noise, ScopedNoDenormals, ~3ms sidechain
  attack ramp (no gain-step click), SR-independent attack envelopes,
  higher polyphony (chords/pad 24, leads 12, drums 16) so voice-stealing
  stops hard-cutting held voicings.

### Validation
- `tests/EngineTests.cpp` (plain g++) green after every engine change;
  critic test updated for the new drop-not-move clash semantics.
- JUCE-side changes can't compile on this Linux box (no JUCE checkout) —
  a static review agent verified all signatures/call sites across the
  three JUCE commits. Full build happens on the Mac via install-mac.sh.
- Commits: 5d78c2d, 86f36ad, 5cae477, 55d7ee9, 38c4943, 015ecbf, a667d77
  — all pushed to remyreynolds/remy-fl `ai-midi-gen-plugin`.
