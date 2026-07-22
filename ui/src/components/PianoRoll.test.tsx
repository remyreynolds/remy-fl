import { render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { buildPreviewModel } from '../previewModel'
import type { Generation } from '../types'
import { PianoRoll } from './PianoRoll'

const fixture: Generation = {
  meta: { bpm: 124, key: 'F minor', ppq: 960, loop_bars: 1, swing_pct: 56 },
  tracks: [{
    name: 'Bass',
    role: 'bass',
    notes: [
      { pitch: 29, start_tick: 0, length_ticks: 960, velocity: 64 },
      { pitch: 36, start_tick: 1920, length_ticks: 480, velocity: 127 },
    ],
  }],
}

describe('PianoRoll', () => {
  it('maps backend ticks to exact horizontal positions', () => {
    const model = buildPreviewModel(fixture, 800, 200)
    expect(model[0].x).toBe(0)
    expect(model[0].width).toBe(200)
    expect(model[1].x).toBe(400)
    expect(model[1].width).toBe(100)
    expect(model[1].opacity).toBe(1)
  })

  it('exposes fixture timing metadata on the canvas', () => {
    render(<PianoRoll generation={fixture} />)
    expect(screen.getByTestId('piano-roll')).toHaveAttribute('data-note-count', '2')
    expect(screen.getByTestId('piano-roll')).toHaveAttribute('data-max-tick', '3840')
  })
})

