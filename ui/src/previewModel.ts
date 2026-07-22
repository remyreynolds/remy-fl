import type { Generation, MidiNote } from './types'

export interface PreviewNote extends MidiNote {
  x: number
  y: number
  width: number
  opacity: number
}

export function buildPreviewModel(
  generation: Generation,
  width: number,
  height: number,
  pitchBounds?: [number, number],
): PreviewNote[] {
  const notes = generation.tracks.flatMap((track) => track.notes)
  if (!notes.length) return []
  const maxTick = generation.meta.loop_bars * 4 * generation.meta.ppq
  const minPitch = pitchBounds?.[0] ?? Math.min(...notes.map((note) => note.pitch))
  const maxPitch = pitchBounds?.[1] ?? Math.max(...notes.map((note) => note.pitch))
  const pitchSpan = Math.max(12, maxPitch - minPitch + 4)
  return notes.map((note) => ({
    ...note,
    x: (note.start_tick / maxTick) * width,
    width: Math.max(2, (note.length_ticks / maxTick) * width),
    y: height - ((note.pitch - minPitch + 2) / pitchSpan) * height,
    opacity: 0.35 + (note.velocity / 127) * 0.65,
  }))
}

