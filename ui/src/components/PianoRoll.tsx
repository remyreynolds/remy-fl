import { useEffect, useRef } from 'react'
import { buildPreviewModel } from '../previewModel'
import type { Generation, MidiNote } from '../types'

export function PianoRoll({
  generation,
  playhead = 0,
  compact = false,
  ghostNotes = [],
}: {
  generation: Generation
  playhead?: number
  compact?: boolean
  ghostNotes?: MidiNote[]
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const noteCount = generation.tracks.reduce((sum, track) => sum + track.notes.length, 0)
  const maxTick = generation.meta.loop_bars * 4 * generation.meta.ppq

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const ratio = window.devicePixelRatio || 1
    const bounds = canvas.getBoundingClientRect()
    const width = Math.max(1, bounds.width)
    const height = Math.max(1, bounds.height)
    canvas.width = Math.round(width * ratio)
    canvas.height = Math.round(height * ratio)
    const context = canvas.getContext('2d')
    if (!context) return
    context.scale(ratio, ratio)

    context.fillStyle = '#10131a'
    context.fillRect(0, 0, width, height)
    const beats = generation.meta.loop_bars * 4
    for (let beat = 0; beat <= beats; beat += 1) {
      const x = (beat / beats) * width
      context.strokeStyle = beat % 4 === 0 ? '#333a46' : '#1e232c'
      context.lineWidth = beat % 4 === 0 ? 1 : 0.5
      context.beginPath()
      context.moveTo(x, 0)
      context.lineTo(x, height)
      context.stroke()
    }
    for (let row = 1; row < 12; row += 1) {
      const y = (row / 12) * height
      context.strokeStyle = '#1a1f27'
      context.beginPath()
      context.moveTo(0, y)
      context.lineTo(width, y)
      context.stroke()
    }

    const combinedPitches = [
      ...generation.tracks.flatMap((track) => track.notes.map((note) => note.pitch)),
      ...ghostNotes.map((note) => note.pitch),
    ]
    const pitchBounds: [number, number] | undefined = combinedPitches.length
      ? [Math.min(...combinedPitches), Math.max(...combinedPitches)]
      : undefined
    if (ghostNotes.length) {
      const ghost = buildPreviewModel(
        {
          ...generation,
          tracks: [{
            name: 'Input',
            role: generation.tracks[0]?.role ?? 'chords',
            notes: ghostNotes,
          }],
        },
        width,
        height,
        pitchBounds,
      )
      for (const note of ghost) {
        context.fillStyle = 'rgba(120, 135, 160, 0.28)'
        context.fillRect(note.x, note.y - (compact ? 2 : 4), note.width, compact ? 3 : 7)
      }
    }
    const model = buildPreviewModel(generation, width, height, pitchBounds)
    for (const note of model) {
      context.fillStyle = `rgba(110, 168, 255, ${note.opacity})`
      context.fillRect(note.x, note.y - (compact ? 2 : 4), note.width, compact ? 3 : 7)
    }
    if (playhead > 0) {
      context.fillStyle = '#cfe0ff'
      context.fillRect(playhead * width, 0, 1.5, height)
    }
  }, [compact, generation, ghostNotes, playhead])

  return (
    <canvas
      ref={canvasRef}
      className={compact ? 'piano-roll piano-roll--compact' : 'piano-roll'}
      data-testid="piano-roll"
      data-note-count={noteCount}
      data-max-tick={maxTick}
      aria-label={`Piano roll preview with ${noteCount} notes`}
    />
  )
}

