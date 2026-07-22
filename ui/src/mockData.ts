import type { BrainState, Fingerprint, Generation, HistoryItem, SessionState } from './types'

const chordNotes = Array.from({ length: 8 }, (_, bar) =>
  [53, 56, 60, 63].map((pitch, voice) => ({
    pitch: pitch + (bar % 2 ? -1 : 0),
    start_tick: bar * 3840 + 720,
    length_ticks: 560,
    velocity: 78 + voice * 4 + (bar % 3) * 2,
  })),
).flat()

export const mockGeneration: Generation = {
  meta: { bpm: 124, key: 'F minor', ppq: 960, loop_bars: 8, swing_pct: 56 },
  tracks: [{ name: 'Chords', role: 'chords', notes: chordNotes }],
  generation_id: 42,
  trace_id: 'mock-trace',
  description: '8-bar deep house chords in F minor at 124 BPM.',
  plan_summary: 'F minor · Fm9–D♭maj7 loop · Rhodes stabs · 56% swing',
  influence_citation: 'groove: fingerprints 1,17; harmony: deep_house',
  variation_offers: ['Try a sparser, more spacious variation?', 'Push the swing further?'],
  warnings: [],
}

export const mockBassGeneration: Generation = {
  ...mockGeneration,
  generation_id: 43,
  description: '8-bar rolling bass in F minor at 124 BPM.',
  plan_summary: 'F minor · rolling 16ths · low density · 56% swing',
  tracks: [{
    name: 'Bass',
    role: 'bass',
    notes: Array.from({ length: 32 }, (_, index) => ({
      pitch: index % 8 === 6 ? 32 : 29,
      start_tick: Math.floor(index / 4) * 3840 + (index % 4) * 960 + 240,
      length_ticks: 180,
      velocity: 82 + (index % 5) * 4,
    })),
  }],
}

export const emptySession: SessionState = {
  session_id: 'studio-session',
  key: 'F minor',
  bpm: 124,
  bars: 8,
  slots: [
    { role: 'chords', label: 'Chords', locked: false },
    { role: 'bass', label: 'Bass', locked: false },
    { role: 'melody', label: 'Melody', locked: false },
    { role: 'arp', label: 'Arp', locked: false },
    { role: 'perc', label: 'Perc', locked: false },
  ],
}

export const mockSession: SessionState = {
  ...emptySession,
  slots: emptySession.slots.map((slot) => (
    slot.role === 'chords'
      ? { ...slot, locked: true, generation: mockGeneration }
      : slot.role === 'bass'
        ? { ...slot, generation: mockBassGeneration }
        : slot
  )),
}

export const mockHistory: HistoryItem[] = [
  {
    id: 43,
    session_id: 'studio-session',
    created_at: new Date(Date.now() - 4 * 60_000).toISOString(),
    prompt: 'rolling deep house bass, sparse and warm',
    element: 'bass',
    plan_summary: mockBassGeneration.plan_summary!,
    kept: false,
    rejected: false,
    generation: mockBassGeneration,
  },
  {
    id: 42,
    session_id: 'studio-session',
    created_at: new Date(Date.now() - 12 * 60_000).toISOString(),
    prompt: 'deep house rhodes chords, 8 bars, F minor',
    element: 'chords',
    plan_summary: mockGeneration.plan_summary!,
    kept: true,
    rejected: false,
    generation: mockGeneration,
  },
]

export const mockBrain: BrainState = {
  taste: [
    { id: 'keys', label: 'prefers F / A♭ minor', confidence: 0.92 },
    { id: 'swing', label: 'swing +2', confidence: 0.76 },
    { id: 'bass', label: 'bass: sparse', confidence: 0.84 },
    { id: 'brightness', label: 'leans dark', confidence: 0.63 },
  ],
  heuristics: [
    { id: 1, trigger: 'Tech house · bass', rule: 'Use ≤6 distinct pitches', evidence: 7, active: true },
    { id: 2, trigger: 'Deep house · chords', rule: 'Leave beat 1 open', evidence: 4, active: true },
    { id: 3, trigger: 'Melody', rule: 'End bar 8 with a full-beat breath', evidence: 3, active: true },
  ],
  report: `# Weekly brain report

## What improved
- Bass generations needed **31% fewer revisions**.
- Sparse chord fingerprints gained the highest kept-rate.

## Weakest musical axis
- Repetition fatigue averaged **2.9 / 5**. The brain will prioritize longer motif cycles.

## Coverage gaps
- Afro house has only two fingerprints.
- Add one darker piano-house reference.`,
}

export const mockFingerprints: Fingerprint[] = [
  { id: 1, track: 'Deep Reference A', subgenre: 'deep_house', bpm: 122, key: 'F minor', progression: ['Fm9', 'E♭maj7'], groove: 'lazy offbeat Rhodes stab', kept_rate: 0.78 },
  { id: 4, track: 'Tech Reference A', subgenre: 'tech_house', bpm: 126, key: 'G minor', progression: ['Gm', 'F'], groove: 'rolling 16th bass', kept_rate: 0.72 },
  { id: 12, track: 'Garage Reference A', subgenre: 'garage', bpm: 130, key: 'A minor', progression: ['Am7', 'G7'], groove: 'chopped chord shuffle', kept_rate: 0.66 },
]

