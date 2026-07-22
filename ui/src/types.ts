export type Role = 'chords' | 'bass' | 'melody' | 'arp' | 'perc'
export type SoundType = 'pluck' | 'pad' | 'keys' | 'stab' | 'sub' | 'lead'
export type Surface = 'generate' | 'history' | 'brain' | 'references'

export interface MidiNote {
  pitch: number
  start_tick: number
  length_ticks: number
  velocity: number
}

export interface MidiTrack {
  name: string
  role: Role
  channel?: number
  notes: MidiNote[]
}

export interface MidiMeta {
  bpm: number
  key: string
  ppq: number
  loop_bars: number
  swing_pct: number
}

export interface Generation {
  meta: MidiMeta
  tracks: MidiTrack[]
  generation_id?: number
  trace_id?: string
  influence_citation?: string
  description?: string
  variation_offers?: string[]
  warnings?: string[]
  plan_summary?: string
  collab_type?: 'fix_groove' | 'reharmonize' | 'continue' | 'bass_under'
  input_notes?: MidiNote[]
  sound_type?: SoundType
}

export interface SessionSlot {
  role: Role
  label: string
  locked: boolean
  generation?: Generation
}

export interface SessionState {
  session_id: string
  key: string
  bpm: number
  bars: number
  slots: SessionSlot[]
}

export interface HistoryItem {
  id: number
  session_id?: string
  created_at: string
  prompt: string
  element: Role
  plan_summary: string
  kept: boolean
  rejected: boolean
  generation: Generation
}

export interface TasteBelief {
  id: string
  label: string
  confidence: number
}

export interface Heuristic {
  id: number
  rule: string
  trigger: string
  evidence: number
  active: boolean
}

export interface BrainState {
  taste: TasteBelief[]
  heuristics: Heuristic[]
  report: string
}

export interface Fingerprint {
  id: number
  track: string
  subgenre: string
  bpm: number
  key: string
  progression: string[]
  groove: string
  kept_rate: number
  confidence?: number
}

export interface ReferenceAnalysis {
  fingerprint: Omit<Fingerprint, 'id' | 'kept_rate'>
}

