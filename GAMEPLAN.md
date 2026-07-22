# GAMEPLAN — UI/Structure Cleanup & Roadmap

**Problem:** the plugin window currently stacks ~60 controls with no hierarchy —
duplicate generate buttons, three overlapping pack/MIDI combos, provider/model
pickers, sample browser, docs buttons, chat, chord dashboard and drum panel all
visible at once. Nothing has a home. This document is the single source of
truth for what stays, what goes, and where everything lives.

**Design law (non-negotiable):**
1. One primary action per screen. In this app that is **Generate**.
2. Anything used less than once per session goes into **Settings**, not the main window.
3. Every control must answer "which zone do I live in?" — if none, it gets cut.
4. The React companion's discipline ("four surfaces only") is the model. The
   plugin adopts the same surface thinking.

---

## 1. The four surfaces (plugin)

```
┌──────────────────────────────────────────────────────────┐
│ HEADER  logo · [GENERATE] [BROWSE] [CHAT] [⚙] · ● API    │
├──────────────────────────────────────────────────────────┤
│                                                          │
│                   ACTIVE SURFACE                         │
│                                                          │
├──────────────────────────────────────────────────────────┤
│ FOOTER  key/scale · BPM · bars · ▶ preview · vol · drag  │
└──────────────────────────────────────────────────────────┘
```

Header = navigation only (4 tabs + API status dot). Footer = the global
musical context that applies to every surface (key, scale, BPM, bars,
transport, master drag-all). Everything else lives inside one surface.

### Surface 1 — GENERATE (default)
The whole reason the plugin exists. One screen, three zones:

- **Left rail — instrument stack (7 rows):** Drums, Bass, Chords, Melody,
  Counter, Arp, Pad. Each row: name · mini piano-roll thumbnail ·
  [↻ regen] [🔒 lock] [M mute] [drag-handle]. Click a row to focus it.
- **Center — focused instrument:** full MidiRollView of the selected row;
  drums swap in the DrumKitPanel (per-piece regen/lock) here.
- **Right rail — generation controls:** Genre combo · Energy slider ·
  Complexity slider · Seed (dice = random, field = repeatable) ·
  **[GENERATE ALL]** (the one big button) · [Vary] · [Undo].

Cut from this surface: everything about providers, models, API keys, docs,
sample folders, packs. None of it is a per-generation decision.

### Surface 2 — BROWSE (library)
Everything that is "stuff on disk" merges here — the current MIDI-pack combo,
MIDI-kit combo, sample filter, sounds folder, load buttons collapse into ONE
browser: source dropdown (MIDI packs / kits / samples) · search field ·
list w/ preview-on-click · [Load into slot] · [Drag to DAW].

### Surface 3 — CHAT (brain)
ChatPanel gets its own surface instead of squatting on the main window.
Prompt box, history, context chip, and "apply to project" actions. This is
where the cognitive brain talks. Focus combo lives here (what the chat edits).

### Surface 4 — SETTINGS (the ⚙)
API key field + status, provider/model combos, docs buttons, sounds-folder
picker, theme. Seen once, then never again — exactly why it leaves the
main window.

---

## 2. Control inventory — keep / merge / cut

| Verdict | Controls |
|---|---|
| **KEEP (footer)** | rootCombo, scaleCombo, bpm −/value/+, bars, volSlider, previewButton, dragAllBtn |
| **KEEP (Generate)** | genreCombo, generateAllBtn, varyBtn, undoButton, lockBtn, muteBtn, per-instrument generateBtn/dragBtn, midiRoll, drumKitPanel |
| **MERGE → Browse** | midiPackCombo + midiCombo + midiKitCombo + packCombo (4 combos → 1 browser), sampleFilterCombo, soundCombo, loadMidiKitButton, addMidiButton, addSoundsButton, soundsFolderButton |
| **MOVE → Chat** | chatPanel, sendButton, newChatButton, contextChip, focusCombo, continueBtn |
| **MOVE → Settings** | apiKeyField/Label, apiStatusLabel (dot stays in header), providerCombo, modelCombo, openDocsButton, addDocsButton |
| **CUT (duplicates)** | generateAllButton *or* generateAllBtn (keep one), exportAllButton (dragAll covers it), exportBtn per-row (drag covers it), headerLabel/subheaderLabel/subtitle decoration labels, minimizeBtn |
| **DECIDE** | chordDashboard → becomes the Chords row's focused-center view, not a separate always-on panel |

Net result: main window goes from ~60 visible controls to **~20**.

---

## 3. Audio cleanup

- **PreviewSynth stays** but gets one owner: the footer transport. No
  per-panel play buttons; Space = play/stop focused pattern, Shift+Space =
  full stack. (Matches the companion's keyboard map.)
- Per-instrument **mute** feeds one mixer struct in PluginProcessor —
  currently mute state is scattered per-panel.
- One master **volSlider** in footer; kill per-panel volume rows.
- Preview sounds: map each InstrumentType to one fixed internal patch
  (kick/clap/hat samples for drums, simple subtractive patch per pitched
  role). Configurable later via Browse, not v1.

## 4. New things (after cleanup, in order)

1. **Seed & history strip** — last 5 generations per instrument as
   thumbnails; click to restore. (Data already exists at generation time.)
2. **Takes A/B/C** — companion already does this; plugin parity.
3. **Humanize/swing macro knob** in the right rail (params exist in engine).
4. **Brain feedback buttons** (👍/👎 per part) wired to POST /feedback —
   this is what makes generations improve over time.
5. **Preset system** — save/load full project state (params + all parts).

## 5. Execution order (each step compiles & pushes)

1. **Scaffold surfaces:** header tab bar + footer bar + surface container;
   move existing panels unchanged into their surfaces. (Pure re-parenting.)
2. **Cut duplicates** listed above; one Generate button, one export path.
3. **Build the instrument stack rail** (left) + focused-roll center.
4. **Merge the 4 combos into the Browse surface.**
5. **Settings surface** (API key, provider/model, docs).
6. **Audio ownership pass** (footer transport, single mixer).
7. Then and only then: new features from §4.

Rule for every step: the plugin must build and be usable at each commit —
no big-bang rewrite.
