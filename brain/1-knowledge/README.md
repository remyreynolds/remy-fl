# 1-knowledge — WHAT THE BRAIN KNOWS (edit these!)

This folder is the brain's taste. Every generation retrieves from these four
files. Better entries here = better music out. **This is the folder you edit.**

## Files

### `fingerprints.json` — reference-track DNA ⭐ (highest impact)
One entry per killer track. The brain retrieves the 3 closest fingerprints for
every generation and pulls the music toward them. Template for a new entry:

```json
{
  "track": "Artist — Title",
  "subgenre": "deep house",
  "bpm": 122,
  "key": "F minor",
  "mode": "dorian",
  "progression": ["Fm9", "Ebmaj7"],
  "harmonic_rhythm": "2 bars",
  "bass_archetype": "sub_hold",
  "swing_pct": 56,
  "density": { "chords": "med", "melody": "low", "perc": "med" },
  "signature_moves": ["lazy offbeat Rhodes stab"]
}
```

- `bass_archetype`: `sub_hold` | `offbeat` | `rolling` | `stab`
- `swing_pct`: 50 = straight, 54–58 = house shuffle territory
- `signature_moves`: the ONE thing that makes the track special, in plain words

### `genre_cards.json` — subgenre defaults
Eight cards (deep_house, tech_house, classic, french, prog_melodic, afro,
garage, piano_house). Sets default BPM/key/swing/harmony AND the `avoid`
field — write what you never want ("overfilled melodies", "straight hats").

### `mood_table.json` — mood word → musical settings
Maps words like "dark", "dreamy", "driving" to concrete parameters so the
chat understands vibe language. Add your own vocabulary here.

### `sound_types.json` — instrument/sound-role definitions
What counts as a stab, pad, pluck, etc. Rarely needs editing.

### `guides/` — music theory & production cheat sheets ⭐
Shared corpus for the **Python generator** and the **plugin chatbot**.

```
guides/
  *.md           curated agent cheat sheets
  full/*.md      full text extracted from your PDFs
  pdfs/*.pdf     original PDF binaries (also copied into the plugin knowledge folder)
```

After editing, restart the brain API and relaunch the plugin so KnowledgeBase
re-syncs. Chatbot may also use Claude’s own knowledge in addition to these docs.

### `midi-examples/` — cheat-sheet MIDI
Reference `.mid` loops; measured DNA is already merged into
`fingerprints.json` (`source: guide_midi`).

## Rules
- Valid JSON only — run `python -m json.tool <file>` after editing.
- After editing, restart the brain API (it seeds from these files).
- Never delete an entry the DB already learned from; add better ones instead.
