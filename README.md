# AI MIDI Gen — VST3/AU MIDI generator (macOS build)

"ChatGPT for MIDI." Describe a vibe in chat → get playable, in-key MIDI per
instrument → drag straight into FL Studio. The plugin outputs **MIDI only**;
your own instruments make the sound.

---

## 0. One-time install (from zero, macOS)

You said nothing's installed yet. Do these three things once:

```bash
# 1. Xcode Command Line Tools (clang compiler + git). ~2 GB, one prompt.
xcode-select --install

# 2. Homebrew (package manager) — skip if you already have `brew`.
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 3. CMake (the build system).
brew install cmake
```

Verify:
```bash
clang --version   # should print Apple clang
cmake --version   # should print 3.22 or newer
```

> You do **not** need to download JUCE — CMake fetches it automatically the
> first time you configure (needs internet; first configure takes a few min).

---

## 1. Build

From the `ai-midi-gen/` folder:

```bash
# Configure (downloads JUCE on first run) and generate an Xcode project:
cmake -B build -G Xcode

# Build the standalone app first — fastest to iterate, no DAW needed:
cmake --build build --target AIMidiGen_Standalone --config Debug

# …or build everything (VST3 + AU + Standalone):
cmake --build build --config Debug
```

Artifacts land in `build/AIMidiGen_artefacts/`. With
`COPY_PLUGIN_AFTER_BUILD` on, the VST3/AU are also copied into your user
plugin folders (`~/Library/Audio/Plug-Ins/VST3` and `/Components`).

---

## 2. Use the AI (Claude backend)

Give it your Anthropic API key one of two ways:

- **Env var (recommended for dev):**
  ```bash
  export ANTHROPIC_API_KEY="sk-ant-..."
  ```
  Launch the standalone/DAW from that same terminal and the key auto-loads.
- **In-plugin:** paste the key into the "Claude API key" field (top-right).

No key? It still runs — a local keyword fallback interprets simple prompts.

---

## 3. Load in FL Studio (Mac)

1. FL Studio → Options → Manage plugins → **Find plugins** (scans VST3/AU).
2. Add **AI MIDI Gen** to a channel.
3. Type a prompt (e.g. *"deep house, 122 bpm, warm chords + bouncy bass"*),
   hit **Generate All** or send a chat message.
4. **Drag MIDI** from any instrument panel → drop into the FL playlist or a
   channel's piano roll. Route that channel to Serum/Omnisphere/etc.

---

## Project layout

```
ai-midi-gen/
├── CMakeLists.txt          # JUCE via FetchContent, plugin target
├── DEVLOG.md               # architecture decisions + phase tracking
└── src/
    ├── PluginProcessor.*   # state, preview player, save/load
    ├── PluginEditor.*      # UI shell, drag container
    ├── ai/AIClient.*       # Claude Messages API + offline fallback
    ├── engine/
    │   ├── MusicTheory.h        # scales, chords, scale-snapping (no JUCE)
    │   ├── MusicInstructions.h  # structured AI→engine schema
    │   └── MidiGenerator.*      # note generation + validation + .mid writer
    └── gui/
        ├── CustomLookAndFeel.h  # dark theme
        ├── ChatPanel.*          # AI chat window
        └── InstrumentPanel.*    # per-instrument controls + drag/export
```

See **DEVLOG.md** for what's done, known issues, and the phase roadmap.
